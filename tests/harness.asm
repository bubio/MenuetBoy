;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;   Test harness: runs the emulator core as a Linux x86-64 program
;   (under blink on macOS) and reports serial output.
;
;   Usage: harness <rom> [frames] [screen.pgm] [audio.raw]
;   Exit:  0 "Passed" seen, 1 "Failed" seen, 2 timeout, 3 ROM error
;
;   Results come from serial output or, for dmg_sound, from the
;   signature DE B0 61 at A001 (A000 = result, text from A004).
;   When screen.pgm is given, the final LCD is written as a PGM image.
;   When audio.raw is given, sound is written as s16le stereo 48kHz.
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

format ELF64 executable 3
entry start

SYS_READ    = 0
SYS_WRITE   = 1
SYS_OPEN    = 2
SYS_CLOSE   = 3
SYS_EXIT    = 60
O_WRONLY_CREAT_TRUNC = 0x241

ROM_MAX         = 0x800000
AUDIO_MAX       = 48000*4*120     ; 2 minutes
DEFAULT_FRAMES  = 60*120

GB_STATE = gb_state_area

segment readable executable

start:

    mov   rdi , [rsp+16]          ; argv[1]
    test  rdi , rdi
    jz    .rom_error
    mov   rax , DEFAULT_FRAMES
    mov   [frames_left] , rax
    mov   rsi , [rsp+24]          ; argv[2]
    test  rsi , rsi
    jz    @f
    call  parse_dec
    mov   [frames_left] , rax
    mov   rax , [rsp+32]          ; argv[3]
    mov   [dump_path] , rax
    test  rax , rax
    jz    @f
    mov   rax , [rsp+40]          ; argv[4]
    mov   [audio_path] , rax
  @@:

    mov   eax , SYS_OPEN
    xor   esi , esi
    xor   edx , edx
    syscall
    test  rax , rax
    js    .rom_error
    mov   r12 , rax
    mov   r13 , gb_rom
  .read:
    mov   eax , SYS_READ
    mov   rdi , r12
    mov   rsi , r13
    mov   edx , 0x100000
    syscall
    test  rax , rax
    jle   .loaded
    add   r13 , rax
    jmp   .read
  .loaded:
    cmp   r13 , gb_rom+0x150
    jb    .rom_error

    call  gb_reset
  .frame:
    call  gb_run_frame
    call  drain_audio
    call  check_cart_result
    movzx edi , byte [test_result]
    test  edi , edi
    jnz   .finish
    dec   qword [frames_left]
    jnz   .frame
    call  dump_screen
    call  write_audio
    mov   rsi , msg_timeout
    mov   edx , msg_timeout_len
    call  print
    mov   edi , 2
    jmp   exit

  .finish:
    call  dump_screen
    call  write_audio
    mov   rsi , msg_newline
    mov   edx , 1
    call  print
    movzx edi , byte [test_result]
    dec   edi                     ; 1 Passed -> 0, 2 Failed -> 1
    jmp   exit

  .rom_error:
    mov   rsi , msg_rom
    mov   edx , msg_rom_len
    call  print
    mov   edi , 3

exit:

    mov   eax , SYS_EXIT
    syscall


print:                            ; In: rsi = buffer, edx = length

    mov   eax , SYS_WRITE
    mov   edi , 1
    syscall
    ret


; Write gb_lcd as PGM (shade 0 = white) to [dump_path], if set.

dump_screen:

    mov   rdi , [dump_path]
    test  rdi , rdi
    jz    .done
    xor   ecx , ecx
  @@:
    mov   al , [gb_lcd+rcx]
    mov   dl , 3
    sub   dl , al
    imul  edx , edx , 85
    mov   [pgm_pixels+rcx] , dl
    inc   ecx
    cmp   ecx , 160*144
    jb    @b

    mov   eax , SYS_OPEN
    mov   esi , O_WRONLY_CREAT_TRUNC
    mov   edx , 0644o
    syscall
    test  rax , rax
    js    .done
    mov   r12 , rax
    mov   eax , SYS_WRITE
    mov   rdi , r12
    mov   rsi , pgm_header
    mov   edx , pgm_header_len + 160*144
    syscall
    mov   eax , SYS_CLOSE
    mov   rdi , r12
    syscall
  .done:
    ret


; Blargg's memory-based result protocol.

check_cart_result:

    cmp   byte [gb_cart_ram+1] , 0xDE
    jne   .done
    cmp   byte [gb_cart_ram+2] , 0xB0
    jne   .done
    cmp   byte [gb_cart_ram+3] , 0x61
    jne   .done
    movzx eax , byte [gb_cart_ram]
    cmp   eax , 0x80
    je    .done                   ; Still running
    push  rax
    mov   rsi , gb_cart_ram+4
    xor   edx , edx
  @@:
    cmp   byte [rsi+rdx] , 0
    je    @f
    inc   edx
    cmp   edx , 0x1000
    jb    @b
  @@:
    call  print
    pop   rax
    mov   byte [test_result] , 1
    test  eax , eax
    jz    .done
    mov   byte [test_result] , 2
  .done:
    ret


; Append new frames of apu_ring to audio_buf.

drain_audio:

    cmp   qword [audio_path] , 0
    je    .done
    mov   ecx , [audio_rpos]
  .frame:
    cmp   ecx , [apu_wpos]
    je    .saved
    mov   eax , ecx
    and   eax , APU_RING-1
    mov   eax , [apu_ring+rax*4]
    mov   rdx , [audio_len]
    cmp   rdx , AUDIO_MAX
    jae   @f
    mov   [audio_buf+rdx] , eax
    add   qword [audio_len] , 4
  @@:
    inc   ecx
    jmp   .frame
  .saved:
    mov   [audio_rpos] , ecx
  .done:
    ret


write_audio:

    mov   rdi , [audio_path]
    test  rdi , rdi
    jz    .done
    mov   eax , SYS_OPEN
    mov   esi , O_WRONLY_CREAT_TRUNC
    mov   edx , 0644o
    syscall
    test  rax , rax
    js    .done
    mov   r12 , rax
    mov   eax , SYS_WRITE
    mov   rdi , r12
    mov   rsi , audio_buf
    mov   rdx , [audio_len]
    syscall
    mov   eax , SYS_CLOSE
    mov   rdi , r12
    syscall
  .done:
    ret


parse_dec:                        ; In: rsi = string   Out: rax

    xor   eax , eax
  @@:
    movzx ecx , byte [rsi]
    sub   ecx , '0'
    cmp   ecx , 9
    ja    @f
    imul  rax , rax , 10
    add   rax , rcx
    inc   rsi
    jmp   @b
  @@:
    ret


; Echo serial output and watch for Blargg's result strings and Mooneye's result bytes.

platform_serial:

    mov   [serial_char] , al
    shl   qword [serial_tail] , 8
    mov   [serial_tail] , al
    mov   rsi , serial_char
    mov   edx , 1
    call  print

    mov   rax , [serial_tail]
    mov   rcx , 0xFFFFFFFFFFFF
    and   rax , rcx
    mov   rcx , 'Passed'
    bswap rcx
    shr   rcx , 16
    cmp   rax , rcx
    jne   @f
    mov   byte [test_result] , 1
  @@:
    mov   rcx , 'Failed'
    bswap rcx
    shr   rcx , 16
    cmp   rax , rcx
    jne   @f
    mov   byte [test_result] , 2
  @@:
    mov   rcx , 0x0305080D1522    ; Mooneye: Fibonacci bytes pass, 0x42 fail
    cmp   rax , rcx
    jne   @f
    mov   byte [test_result] , 1
  @@:
    mov   rcx , 0x424242424242
    cmp   rax , rcx
    jne   @f
    mov   byte [test_result] , 2
  @@:
    ret


include '../src/core/gb.inc'


msg_timeout:    db  10,'TIMEOUT',10
msg_timeout_len = $ - msg_timeout
msg_rom:        db  'cannot load ROM',10
msg_rom_len = $ - msg_rom
msg_newline:    db  10


segment readable writeable

frames_left:    dq  ?
dump_path:      dq  ?
audio_path:     dq  ?
audio_len:      dq  ?
audio_rpos:     dd  ?
pgm_header:     db  'P5 160 144 255',10
pgm_header_len = $ - pgm_header
pgm_pixels:     rb  160*144
serial_tail:    dq  ?
serial_char:    db  ?
test_result:    db  ?

align 16
gb_state_area:  rb  GB_STATE_SIZE
align 16
gb_rom:         rb  ROM_MAX
audio_buf:      rb  AUDIO_MAX
