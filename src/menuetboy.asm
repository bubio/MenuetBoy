;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;   MenuetBoy - GAME BOY (DMG) emulator for MenuetOS 64bit
;
;   Build: scripts/build.sh
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Memory map (application address space)
;
;   0x000000 - image_end   code and data
;   0x110000 - 0x169FFF    LCD output buffer (320x288, 0x00RRGGBB)
;   0x180000 - 0x183FFF    Audio buffer handed to syscall 117
;   0x200000 -             Emulator core state (GB_STATE_SIZE bytes)
;   0x7FFFF0               stack top
;   0x800000 - 0xFFFFFF    ROM buffer (8MB)

APP_MEMORY  = 0x1000000
STACK_TOP   = 0x7FFFF0
LCD_RGB     = 0x110000
GB_STATE    = 0x200000
ROM_BUF     = 0x800000
gb_rom      = ROM_BUF
ROM_MAX     = 0x800000
AUDIO_BUF   = 0x180000
AUDIO_BUF_MAX = APU_RING          ; Bytes: a buffer is at most 1/4 of the ring

LCD_W       = 160
LCD_H       = 144
VIEW_W      = LCD_W*2
VIEW_H      = LCD_H*2

VIEW_X      = 10                  ; LCD position in window
VIEW_Y      = 30
LINE_H      = 16
INFO_Y      = VIEW_Y+VIEW_H+8
INFO_LINES  = 8
WIN_W       = VIEW_W+VIEW_X*2
WIN_H       = INFO_Y+LINE_H*INFO_LINES+8

BG_COLOR    = 0xFFFFFF
TEXT_COLOR  = 0x000000

FRAME_US    = 16743               ; 70224 cycles / 4194304 Hz
PACE_MAX    = 800                 ; Max frame interval adjustment (us)
SAVE_DELAY  = 60                  ; Frames without cartridge RAM writes before saving
SYS_MS_TIMER = 48*8               ; ms counter in syscall 26 general data

use64

    org   0x0

    db    'MENUET64'              ; Header identifier
    dq    0x01                    ; Version
    dq    START                   ; Start of code
    dq    image_end               ; Size of image
    dq    APP_MEMORY              ; Memory for app
    dq    STACK_TOP               ; Rsp
    dq    param                   ; Prm
    dq    0x00                    ; Icon

START:

    call  read_timer_ms
    mov   [clk_edge_ms] , rax
    mov   rax , 3
    mov   rbx , 1
    int   0x60
    mov   [clk_rtc] , rax

    call  load_rom

    mov   rax , 241               ; Get scheduler frequency
    mov   rbx , 1
    int   0x60
    mov   rdi , txt_sched.hz
    mov   rcx , 6
    call  write_dec

    call  audio_init
    call  run_frame
    call  draw_window
    call  get_time_us
    mov   [next_frame_us] , rax

main_loop:

    call  audio_update
    call  get_time_us
    mov   rbx , [next_frame_us]
    sub   rbx , rax
    jg    .wait

    sub   [late_us] , rbx         ; How late this frame starts
    call  frame
    mov   rax , 11                ; Check for event
    int   0x60
    jmp   .dispatch

  .wait:
    imul  rbx , [clk_rate]        ; Wall clock us -> timer us
    xor   edx , edx
    mov   rax , rbx
    mov   rcx , 1000
    div   rcx
    mov   rbx , rax
    mov   rax , 223               ; Wait for event with timeout (us)
    int   0x60

  .dispatch:
    test  rax , 1                 ; Window redraw
    jnz   window_event
    test  rax , 2                 ; Keyboard press
    jnz   key_event
    test  rax , 4                 ; Button press
    jnz   button_event
    jmp   main_loop

window_event:

    call  draw_window
    jmp   main_loop

key_event:

    mov   rax , 2                 ; Get key
    int   0x60
    test  rax , rax
    jnz   main_loop

    mov   r12 , rcx
    mov   r13 , rbx
    call  map_key
    mov   rax , r13
    mov   rdi , txt_key.flags
    mov   rcx , 2
    call  write_hex
    mov   rax , r12
    mov   rdi , txt_key.code
    mov   rcx , 8
    call  write_hex

    mov   rdi , txt_key.text      ; Key code bytes as characters
    mov   rcx , 8
  .key_char:
    mov   al , r12l
    cmp   al , 0x20
    jb    .key_dot
    cmp   al , 0x7E
    jbe   .key_put
  .key_dot:
    mov   al , '.'
  .key_put:
    mov   [rdi] , al
    shr   r12 , 8
    inc   rdi
    dec   rcx
    jnz   .key_char

    call  draw_info
    jmp   key_event

; In: r12d = key code, r13 = key flags (bit 0: 0/1 = down/up)

map_key:

    mov   rsi , key_map
  .entry:
    mov   eax , [rsi]
    test  eax , eax
    jz    .done
    cmp   eax , r12d
    je    .found
    add   rsi , 8
    jmp   .entry
  .found:
    mov   al , [rsi+4]
    mov   edx , r13d
    and   edx , 1
    xor   edx , 1
    call  gb_set_button
  .done:
    ret

button_event:

    mov   rax , 17                ; Get pressed button
    int   0x60

    cmp   rbx , 0x10000001        ; Close button
    jne   main_loop
    cmp   qword [save_due] , 0
    je    @f
    call  write_save
  @@:
    cmp   byte [audio_on] , 0
    je    .exit
    mov   rax , 117               ; Stop and free audio
    mov   rbx , 5
    int   0x60
    mov   rax , 117
    mov   rbx , 255
    int   0x60
  .exit:
    mov   rax , 512               ; Terminate process
    int   0x60


; Load ROM from the start parameter, or from the first readable
; default path when started without a parameter.

load_rom:

    cmp   [param+8] , byte 0
    je    .defaults
    mov   r9 , param+8
    call  try_load
    jmp   .result

  .defaults:
    mov   r12 , rom_paths
  .next:
    mov   r9 , [r12]
    test  r9 , r9
    jz    .result
    call  try_load
    test  rax , rax
    jz    .result
    add   r12 , 8
    jmp   .next

  .result:
    mov   rsi , [rom_path]
    mov   rdi , txt_rom.path
    mov   rcx , ROM_PATH_LEN
  .copy_path:
    mov   al , [rsi]
    test  al , al
    jz    .path_done
    mov   [rdi] , al
    inc   rsi
    inc   rdi
    dec   rcx
    jnz   .copy_path
  .path_done:
    mov   [rdi] , byte 0

    cmp   qword [rom_error] , 0
    jne   .error

    mov   rsi , ROM_BUF+0x134     ; Cartridge title
    mov   rdi , txt_cart.title
    mov   rcx , 16
  .title:
    mov   al , [rsi]
    cmp   al , 0x20
    jb    .title_space
    cmp   al , 0x7E
    jbe   .title_char
  .title_space:
    mov   al , ' '
  .title_char:
    mov   [rdi] , al
    inc   rsi
    inc   rdi
    dec   rcx
    jnz   .title

    movzx rax , byte [ROM_BUF+0x147]
    mov   rdi , txt_cart.type
    mov   rcx , 2
    call  write_hex
    mov   rax , [rom_size]
    mov   rdi , txt_cart.size
    mov   rcx , 8
    call  write_hex

    mov   qword [txt_status] , txt_cart
    mov   byte [rom_loaded] , 1
    call  gb_reset
    call  load_save
    ret

  .error:
    mov   qword [txt_status] , txt_error
    ret


; In : r9 - path (asciiz)
; Out: rax - 0 success / error code

try_load:

    mov   [rom_path] , r9
    mov   rax , 58                ; Read the first block for the file size
    mov   rbx , 0
    mov   rcx , 0
    mov   rdx , 1
    mov   r8  , ROM_BUF
    int   0x60
    test  rax , rax
    jnz   .done
    cmp   rbx , ROM_MAX
    jbe   @f
    mov   rbx , ROM_MAX
  @@:
    lea   rdx , [rbx+511]         ; Then read just the blocks of the file
    shr   rdx , 9
    mov   rax , 58
    mov   rbx , 0
    mov   rcx , 0
    mov   r8  , ROM_BUF
    int   0x60
  .done:
    mov   [rom_error] , rax
    mov   [rom_size] , rbx
    mov   rdi , [error_log_pos]   ; Append the code to the error line
    cmp   rdi , txt_error.end
    jae   .logged
    push  rax
    mov   rcx , 2
    call  write_hex
    pop   rax
    add   qword [error_log_pos] , 3
  .logged:
    ret


; Battery-backed cartridge RAM is kept in /FD/1/<ROM name>.SAV. /FD/1 is the
; in-memory copy of the boot floppy, so saves last until MenuetOS restarts.

load_save:

    movzx eax , byte [ROM_BUF+0x147]
    mov   rdi , battery_types
    mov   rcx , battery_types.count
    repne scasb
    jne   .done
    cmp   dword [ram_size] , 0
    je    .done
    mov   byte [save_on] , 1

    mov   rsi , [rom_path]        ; Name: ROM file name up to the dot
    mov   rdi , rsi
  .find_name:
    lodsb
    test  al , al
    jz    .name_found
    cmp   al , '/'
    jne   .find_name
    mov   rdi , rsi
    jmp   .find_name
  .name_found:
    mov   rsi , rdi
    mov   rdi , save_name
    mov   rcx , 8
  .copy_name:
    lodsb
    test  al , al
    jz    .name_done
    cmp   al , '.'
    je    .name_done
    stosb
    dec   rcx
    jnz   .copy_name
  .name_done:
    mov   dword [rdi] , '.SAV'
    mov   byte [rdi+4] , 0

    mov   eax , [ram_size]
    lea   rdx , [rax+511]
    shr   rdx , 9
    mov   rax , 58                ; Read file
    mov   rbx , 0
    mov   rcx , 0
    mov   r8  , gb_cart_ram
    mov   r9  , save_path
    int   0x60
    mov   rsi , txt_save_loaded
    test  rax , rax
    jz    save_status
    mov   rdi , txt_save_new.code
    mov   rcx , 2
    call  write_hex
    mov   rsi , txt_save_new
    jmp   save_status
  .done:
    ret


; Save once cartridge RAM has not been written for SAVE_DELAY frames.

check_save:

    cmp   byte [save_on] , 0
    je    .done
    cmp   byte [cart_ram_dirty] , 0
    je    .due
    mov   byte [cart_ram_dirty] , 0
    mov   rax , [frame_count]
    add   rax , SAVE_DELAY
    mov   [save_due] , rax
    ret
  .due:
    mov   rax , [save_due]
    test  rax , rax
    jz    .done
    cmp   rax , [frame_count]
    ja    .done
    call  write_save
  .done:
    ret


write_save:

    mov   qword [save_due] , 0
    mov   rax , 58                ; Delete file (writing fails if it exists)
    mov   rbx , 2
    mov   r9  , save_path
    int   0x60
    mov   rax , 58                ; Write file
    mov   rbx , 1
    mov   rcx , 0
    mov   edx , [ram_size]
    mov   r8  , gb_cart_ram
    mov   r9  , save_path
    int   0x60
    mov   rsi , txt_save_saved
    test  rax , rax
    jz    save_status
    mov   rdi , txt_save_error.code
    mov   rcx , 2
    call  write_hex
    mov   rsi , txt_save_error
    jmp   save_status


copy_str:                         ; Copy asciiz rsi to rdi, rdi ends at the 0

    lodsb
    stosb
    test  al , al
    jnz   copy_str
    dec   rdi
    ret


save_status:                      ; In: rsi = status text. Line: path + status

    push  rsi
    mov   rdi , txt_save.text
    mov   rsi , save_path
    call  copy_str
    mov   word [rdi] , '  '
    add   rdi , 2
    pop   rsi
    call  copy_str
    ret


; Start double-buffered playback with format 0 (48kHz 16bit stereo,
; the format every driver supports). The core renders at the same rate.

audio_init:

    mov   rax , 117               ; Reserve audio out
    mov   rbx , 1
    int   0x60
    mov   r13 , 1                 ; Failing step, shown on screen
    test  rax , rax
    jnz   .fail
    mov   rax , 117               ; Format 0 buffer size
    mov   rbx , 7
    mov   rcx , 0
    int   0x60
    mov   r13 , 7
    test  rax , rax
    jnz   .free
    mov   rax , rbx
    shr   rbx , 48
    jz    .free
    cmp   rbx , AUDIO_BUF_MAX
    ja    .free
    mov   [audio_bytes] , rbx
    mov   rax , 117               ; Select format 0. The result is not
    mov   rbx , 8                 ; checked, like the sample AUDIO.ASM:
    mov   rcx , 0                 ; it fails here while playback works
    int   0x60

    mov   rdi , AUDIO_BUF         ; Start with two silent buffers
    mov   rcx , [audio_bytes]
    xor   eax , eax
    cld
    rep   stosb
    xor   r12 , r12
  .clear:
    mov   rax , 117
    mov   rbx , 2
    mov   rcx , AUDIO_BUF
    mov   rdx , r12
    int   0x60
    inc   r12
    cmp   r12 , 2
    jb    .clear
    mov   rax , 117               ; Start play
    mov   rbx , 3
    int   0x60
    mov   r13 , 3
    test  rax , rax
    jnz   .free

    mov   byte [audio_on] , 1
    mov   eax , [apu_wpos]
    mov   [audio_rpos] , eax
    mov   rax , [audio_bytes]
    mov   rdi , txt_audio.bytes
    mov   rcx , 5
    call  write_dec
    mov   qword [txt_audio_status] , txt_audio
    ret
  .free:
    push  rax
    mov   rax , 117               ; Free device
    mov   rbx , 255
    int   0x60
    pop   rax
  .fail:
    mov   rdi , txt_audio_none.code
    mov   rcx , 16
    call  write_hex
    mov   rax , r13
    mov   rdi , txt_audio_none.step
    mov   rcx , 3
    call  write_dec
    mov   qword [txt_audio_status] , txt_audio_none
    ret


; When the card has moved to the buffer we filled last, fill the other
; one from apu_ring.
;
; A cushion of half a buffer is kept in the ring so that frame timing
; jitter does not starve the card. Until the clock is calibrated, or
; after an underrun until the cushion is rebuilt, silence is played.
; The cushion size after each fill also nudges the frame interval
; (pace_adjust_us), which locks emulation speed to the audio clock.

audio_update:

    cmp   byte [audio_on] , 0
    je    .done
    mov   rax , 117               ; Get current play buffer
    mov   rbx , 4
    int   0x60
    cmp   rbx , [audio_cur]
    jne   .done

    mov   rcx , [audio_bytes]
    shr   rcx , 2                 ; Frames per buffer
    mov   r8d , ecx
    shr   r8d , 1                 ; Cushion
    mov   eax , [apu_wpos]
    sub   eax , [audio_rpos]      ; Frames available

    cmp   qword [clk_seconds] , 2
    jae   @f
    mov   edx , [apu_wpos]        ; Clock not calibrated yet: discard
    mov   [audio_rpos] , edx
    jmp   .silence
  @@:
    cmp   byte [audio_primed] , 0
    jne   .play
    lea   edx , [ecx+r8d]
    cmp   eax , edx
    jb    .silence
    mov   byte [audio_primed] , 1

  .play:
    lea   edx , [ecx*2+ecx]
    cmp   eax , edx               ; Too far ahead: drop to buffer + cushion
    jbe   @f
    mov   edx , [apu_wpos]
    sub   edx , ecx
    sub   edx , r8d
    mov   [audio_rpos] , edx
  @@:
    mov   esi , [audio_rpos]
    mov   rdi , AUDIO_BUF
    mov   edx , [audio_last]
  .frame:
    cmp   esi , [apu_wpos]
    je    .underrun
    mov   eax , esi
    and   eax , APU_RING-1
    mov   edx , [apu_ring+rax*4]
    inc   esi
    jmp   .store
  .underrun:                      ; Repeat the last frame, rebuild cushion
    inc   qword [audio_underruns]
    mov   byte [audio_primed] , 0
  .store:
    mov   [rdi] , edx
    add   rdi , 4
    dec   ecx
    jnz   .frame
    mov   [audio_rpos] , esi
    mov   [audio_last] , edx

    mov   eax , [apu_wpos]        ; Cushion error -> frame interval
    sub   eax , esi
    sub   eax , r8d
    movsxd rax , eax
    sar   rax , 2                 ; 1000 frames off -> 250 us per frame
    cmp   rax , PACE_MAX
    jle   @f
    mov   rax , PACE_MAX
  @@:
    cmp   rax , -PACE_MAX
    jge   @f
    mov   rax , -PACE_MAX
  @@:
    mov   [pace_adjust_us] , rax
    jmp   .give

  .silence:
    mov   rdi , AUDIO_BUF
    xor   eax , eax
    cld
    rep   stosd
    mov   [audio_last] , eax
    mov   [pace_adjust_us] , rax

  .give:
    mov   rax , 117               ; Give data for the idle buffer
    mov   rbx , 2
    mov   rcx , AUDIO_BUF
    mov   rdx , [audio_cur]
    xor   rdx , 1
    int   0x60
    xor   qword [audio_cur] , 1
  .done:
    ret


; Out: rax - wall clock time in microseconds
;
; The system ms timer runs slow under the VM's emulated CPU (about
; 786 ms per second under TCG), while the RTC seconds stay correct.
; Time is whole RTC seconds plus the timer ms since the last second
; change, scaled by the timer rate measured over the last second.

get_time_us:

    call  read_timer_ms
    mov   rsi , rax
    mov   rax , 3                 ; Get time (RTC)
    mov   rbx , 1
    int   0x60
    cmp   rax , [clk_rtc]
    je    .scale
    mov   [clk_rtc] , rax
    mov   rax , rsi               ; New second
    sub   rax , [clk_edge_ms]
    mov   [clk_edge_ms] , rsi
    inc   qword [clk_seconds]
    cmp   qword [clk_seconds] , 2 ; The first interval is partial
    jb    .scale
    cmp   rax , 200
    jb    .scale
    cmp   rax , 2000
    ja    .scale
    mov   [clk_rate] , rax
  .scale:
    mov   rax , rsi
    sub   rax , [clk_edge_ms]
    imul  rax , rax , 1000000
    xor   edx , edx
    div   qword [clk_rate]
    cmp   rax , 999999
    jbe   @f
    mov   rax , 999999
  @@:
    imul  rbx , [clk_seconds] , 1000000
    add   rax , rbx
    ret


; Out: rax - system ms timer

read_timer_ms:

    mov   rax , 26                ; Get general system info
    mov   rbx , 11
    mov   rcx , SYS_MS_TIMER
    int   0x60
    ret


; Run one emulated frame and schedule the next one.

frame:

    call  get_time_us
    mov   r13 , rax
    call  run_frame
    call  blit_lcd
    call  check_save
    call  get_time_us
    mov   rbx , rax
    sub   rbx , r13
    add   [work_us] , rbx

    mov   rcx , [pace_adjust_us]
    add   rcx , FRAME_US
    add   [next_frame_us] , rcx
    cmp   [next_frame_us] , rax   ; Too far behind: resync
    jge   .scheduled
    mov   [next_frame_us] , rax
  .scheduled:
    call  update_fps
    ret


run_frame:

    inc   qword [frame_count]
    cmp   byte [rom_loaded] , 0
    je    .done
    call  gb_run_frame
  .done:
    ret


; Convert shades to RGB at 2x scale and display.

blit_lcd:

    mov   rsi , gb_lcd
    mov   rdi , LCD_RGB
    mov   rdx , LCD_H
  .row:
    mov   rcx , LCD_W
  .pixel:
    movzx rax , byte [rsi]
    mov   eax , [palette+rax*4]
    mov   [rdi] , eax
    mov   [rdi+4] , eax
    mov   [rdi+VIEW_W*4] , eax
    mov   [rdi+VIEW_W*4+4] , eax
    inc   rsi
    add   rdi , 8
    dec   rcx
    jnz   .pixel
    add   rdi , VIEW_W*4          ; Skip duplicated row
    dec   rdx
    jnz   .row

    mov   rax , 7                 ; Display image
    mov   rbx , VIEW_X shl 32 + VIEW_W
    mov   rcx , VIEW_Y shl 32 + VIEW_H
    mov   rdx , LCD_RGB
    mov   r8  , 0
    mov   r9  , 0x1000000
    mov   r10 , 4
    int   0x60
    ret


; Count frames per wall clock second.

update_fps:

    mov   rax , 3                 ; Get time
    mov   rbx , 1
    int   0x60
    cmp   rax , [last_time]
    je    .done
    mov   [last_time] , rax

    mov   rax , [clk_rate]        ; Timer ms per wall clock second
    mov   rdi , txt_clock.rate
    mov   rcx , 5
    call  write_dec

    mov   rax , [frame_count]
    mov   rbx , rax
    sub   rax , [last_frame_count]
    mov   [last_frame_count] , rbx
    mov   r12 , rax
    mov   rdi , txt_fps.fps
    mov   rcx , 3
    call  write_dec

    xor   rax , rax               ; Average work time per frame
    test  r12 , r12
    jz    .no_frames
    mov   rax , [work_us]
    xor   rdx , rdx
    div   r12
  .no_frames:
    mov   qword [work_us] , 0
    push  rax
    xor   rax , rax               ; Average lateness per frame
    test  r12 , r12
    jz    @f
    mov   rax , [late_us]
    xor   rdx , rdx
    div   r12
  @@:
    mov   qword [late_us] , 0
    mov   rdi , txt_clock.late
    mov   rcx , 6
    call  write_dec
    pop   rax
    mov   rdi , txt_fps.work
    mov   rcx , 6
    call  write_dec
    mov   rax , [audio_underruns]
    mov   rdi , txt_audio.underruns
    mov   rcx , 8
    call  write_dec
    mov   rax , [frame_count]
    mov   rdi , txt_fps.count
    mov   rcx , 8
    call  write_dec
    call  draw_info
  .done:
    ret


draw_window:

    mov   rax , 12                ; Beginning of window draw
    mov   rbx , 1
    int   0x60

    mov   rax , 0                 ; Draw window
    mov   rbx , 100 shl 32 + WIN_W
    mov   rcx , 80 shl 32 + WIN_H
    mov   rdx , BG_COLOR
    mov   r8  , 1
    mov   r9  , window_label
    mov   r10 , 0
    int   0x60

    call  blit_lcd
    call  draw_info

    mov   rax , 12                ; End of window draw
    mov   rbx , 2
    int   0x60
    ret


draw_info:

    mov   rax , 13                ; Clear info area
    mov   rbx , VIEW_X shl 32 + VIEW_W
    mov   rcx , INFO_Y shl 32 + LINE_H*INFO_LINES
    mov   rdx , BG_COLOR
    int   0x60

    mov   rax , 4                 ; Display text
    mov   rcx , VIEW_X
    mov   rdx , INFO_Y
    mov   rsi , TEXT_COLOR
    mov   r9  , 1
    mov   rbx , txt_rom
    int   0x60
    add   rdx , LINE_H
    mov   rbx , [txt_status]
    int   0x60
    add   rdx , LINE_H
    mov   rbx , txt_key
    int   0x60
    add   rdx , LINE_H
    mov   rbx , txt_fps
    int   0x60
    add   rdx , LINE_H
    mov   rbx , txt_sched
    int   0x60
    add   rdx , LINE_H
    mov   rbx , [txt_audio_status]
    int   0x60
    add   rdx , LINE_H
    mov   rbx , txt_clock
    int   0x60
    add   rdx , LINE_H
    mov   rbx , txt_save
    int   0x60
    ret


; In : rdi - destination, rax - value, rcx - digits

write_hex:

    lea   rdi , [rdi+rcx-1]
  .digit:
    mov   rdx , rax
    and   rdx , 0xF
    mov   dl , [hex_digits+rdx]
    mov   [rdi] , dl
    shr   rax , 4
    dec   rdi
    dec   rcx
    jnz   .digit
    ret


; In : rdi - destination, rax - value, rcx - width (space padded)

write_dec:

    lea   rdi , [rdi+rcx-1]
    mov   r8 , 10
  .digit:
    xor   rdx , rdx
    div   r8
    add   dl , '0'
    mov   [rdi] , dl
    dec   rdi
    dec   rcx
    jz    .done
    test  rax , rax
    jnz   .digit
  .pad:
    mov   [rdi] , byte ' '
    dec   rdi
    dec   rcx
    jnz   .pad
  .done:
    ret


; Serial output is not used by the frontend.

platform_serial:

    ret


include 'core/gb.inc'


; Data area

window_label:   db  'MenuetBoy',0

; Key code (ASCII or extended key name) -> joypad bit
key_map:        dd  'Righ' , 0x01
                dd  'Left' , 0x02
                dd  'Up-A' , 0x04
                dd  'Down' , 0x08
                dd  'x' , 0x10 , 'X' , 0x10
                dd  'z' , 0x20 , 'Z' , 0x20
                dd  'Back' , 0x40 , ' ' , 0x40
                dd  'Ente' , 0x80 , 13 , 0x80
                dd  0

hex_digits:     db  '0123456789ABCDEF'

palette:        dd  0xE0F8D0 , 0x88C070 , 0x346856 , 0x081820

; File browser shows the CD as /CD/1/. Other variants are fallbacks
; until reading from that path is confirmed.
rom_paths:      dq  rom_path_CD1 , rom_path_cd1 , rom_path_cd2 , rom_path_cd3 , rom_path_cd4
                dq  rom_path_cd1l , rom_path_cd2l , rom_path_cd3l , rom_path_cd4l , 0
rom_path_CD1:   db  '/CD/1/MENUETBOY/ROM.GB',0
rom_path_cd1:   db  '/cd/1/MENUETBOY/ROM.GB',0
rom_path_cd2:   db  '/cd/2/MENUETBOY/ROM.GB',0
rom_path_cd3:   db  '/cd/3/MENUETBOY/ROM.GB',0
rom_path_cd4:   db  '/cd/4/MENUETBOY/ROM.GB',0
rom_path_cd1l:  db  '/cd/1/menuetboy/rom.gb',0
rom_path_cd2l:  db  '/cd/2/menuetboy/rom.gb',0
rom_path_cd3l:  db  '/cd/3/menuetboy/rom.gb',0
rom_path_cd4l:  db  '/cd/4/menuetboy/rom.gb',0

ROM_PATH_LEN = 64

txt_rom:        db  'ROM: '
  .path:        times ROM_PATH_LEN+1 db 0

txt_cart:       db  'Title: '
  .title:       db  '                '
                db  '  Type: '
  .type:        db  '00'
                db  '  Size: '
  .size:        db  '00000000',0

txt_error:      db  'Load error:'     ; One code per tried path
  .codes:       times 10 db ' ','  '
  .end:         db  0

txt_key:        db  'Key: flags '
  .flags:       db  '00'
                db  '  code '
  .code:        db  '00000000'
                db  '  "'
  .text:        db  '        "',0

txt_fps:        db  'Frames: '
  .count:       db  '       0'
                db  '  FPS: '
  .fps:         db  '  0'
                db  '  Work: '
  .work:        db  '     0 us',0

txt_sched:      db  'Scheduler: '
  .hz:          db  '     0 Hz',0

txt_audio:      db  'Audio: 48kHz '
  .bytes:       db  '    0 B  underrun '
  .underruns:   db  '       0',0

txt_clock:      db  'Timer: '
  .rate:        db  '    0 ms/s  late '
  .late:        db  '     0 us',0

txt_audio_none: db  'Audio: off  117/'
  .step:        db  '  0 -> '
  .code:        db  '0000000000000000',0

txt_save:       db  'Save: '
  .text:        db  'none'
                times 48 db 0

txt_save_new:    db  'new (read error '
  .code:         db  '00)',0
txt_save_loaded: db  'loaded',0
txt_save_saved:  db  'saved',0
txt_save_error:  db  'write error '
  .code:         db  '00',0

; Cartridge types with a battery
battery_types:  db  0x03 , 0x06 , 0x09 , 0x0D , 0x0F , 0x10 , 0x13 , 0x1B , 0x1E
  .count = $ - battery_types

save_path:      db  '/FD/1/'
save_name:      times 13 db 0

txt_status:       dq  txt_error
txt_audio_status: dq  txt_audio_none
rom_path:         dq  rom_path_cd1
rom_error:        dq  0
error_log_pos:    dq  txt_error.codes+1
rom_size:         dq  0
frame_count:      dq  0
last_frame_count: dq  0
last_time:        dq  0
rom_loaded:       db  0
next_frame_us:    dq  0
work_us:          dq  0
late_us:          dq  0
clk_rtc:          dq  0
clk_edge_ms:      dq  0               ; Timer ms at the last RTC second change
clk_seconds:      dq  0               ; RTC second changes since start
clk_rate:         dq  1000            ; Timer ms per second
audio_on:         db  0
audio_bytes:      dq  0
audio_cur:        dq  0               ; Buffer to wait for, see audio_update
audio_rpos:       dd  0
audio_last:       dd  0
audio_primed:     db  0
pace_adjust_us:   dq  0               ; Signed, from audio_update
audio_underruns:  dq  0
save_on:          db  0
save_due:         dq  0               ; Frame to save at, 0 = nothing to save

param:          dq  ROM_PATH_LEN
                times ROM_PATH_LEN+1 db 0

image_end:

assert image_end <= LCD_RGB
assert LCD_RGB + VIEW_W*VIEW_H*4 <= AUDIO_BUF
assert AUDIO_BUF + AUDIO_BUF_MAX <= GB_STATE
assert GB_STATE + GB_STATE_SIZE <= STACK_TOP - 0x10000
