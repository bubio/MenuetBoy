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
INFO_LINES  = 6
WIN_W       = VIEW_W+VIEW_X*2
WIN_H       = INFO_Y+LINE_H*INFO_LINES+8

BG_COLOR    = 0xFFFFFF
TEXT_COLOR  = 0x000000

FRAME_US    = 16743               ; 70224 cycles / 4194304 Hz
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

    call  frame
    mov   rax , 11                ; Check for event
    int   0x60
    jmp   .dispatch

  .wait:
    mov   rax , rbx               ; Round up to ms
    add   rax , 999
    xor   rdx , rdx
    mov   rcx , 1000
    div   rcx
    mov   rbx , rax
    mov   rax , 123               ; Wait for event with timeout (ms)
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
    ret

  .error:
    mov   rax , [rom_error]
    mov   rdi , txt_error.code
    mov   rcx , 2
    call  write_hex
    mov   qword [txt_status] , txt_error
    ret


; In : r9 - path (asciiz)
; Out: rax - 0 success / error code

try_load:

    mov   [rom_path] , r9
    mov   rax , 58                ; Read file
    mov   rbx , 0
    mov   rcx , 0
    mov   rdx , ROM_MAX/512
    mov   r8  , ROM_BUF
    int   0x60
    mov   [rom_error] , rax
    mov   [rom_size] , rbx
    ret


; Start double-buffered playback with format 0 (48kHz 16bit stereo,
; the format every driver supports). The core renders at the same rate.

audio_init:

    mov   rax , 117               ; Reserve audio out
    mov   rbx , 1
    int   0x60
    test  rax , rax
    jnz   .fail
    mov   rax , 117               ; Format 0 buffer size
    mov   rbx , 7
    mov   rcx , 0
    int   0x60
    test  rax , rax
    jnz   .free
    shr   rbx , 48
    jz    .free
    cmp   rbx , AUDIO_BUF_MAX
    ja    .free
    mov   [audio_bytes] , rbx
    mov   rax , 117               ; Select format 0
    mov   rbx , 8
    mov   rcx , 0
    int   0x60
    test  rax , rax
    jnz   .free

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
    mov   rax , 117               ; Free device
    mov   rbx , 255
    int   0x60
  .fail:
    mov   qword [txt_audio_status] , txt_audio_none
    ret


; When the card has moved to the buffer we filled last, fill the other
; one from apu_ring. Missing frames repeat the last frame.

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
    mov   esi , [audio_rpos]
    mov   eax , [apu_wpos]
    sub   eax , esi
    lea   edx , [ecx*2+ecx]
    cmp   eax , edx               ; Too far behind: drop to one buffer
    jbe   @f
    mov   esi , [apu_wpos]
    sub   esi , ecx
  @@:
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
  .underrun:
    inc   qword [audio_underruns]
  .store:
    mov   [rdi] , edx
    add   rdi , 4
    dec   ecx
    jnz   .frame
    mov   [audio_rpos] , esi
    mov   [audio_last] , edx

    mov   rax , 117               ; Give data for the idle buffer
    mov   rbx , 2
    mov   rcx , AUDIO_BUF
    mov   rdx , [audio_cur]
    xor   rdx , 1
    int   0x60
    xor   qword [audio_cur] , 1
  .done:
    ret


; Out: rax - system time in microseconds (1 ms resolution)

get_time_us:

    mov   rax , 26                ; Get general system info
    mov   rbx , 11
    mov   rcx , SYS_MS_TIMER
    int   0x60
    imul  rax , rax , 1000
    ret


; Run one emulated frame and schedule the next one.

frame:

    call  get_time_us
    mov   r13 , rax
    call  run_frame
    call  blit_lcd
    call  get_time_us
    mov   rbx , rax
    sub   rbx , r13
    add   [work_us] , rbx

    add   qword [next_frame_us] , FRAME_US
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

txt_error:      db  'Load error: '
  .code:        db  '00',0

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

txt_audio:      db  'Audio: 48000 Hz  buffer '
  .bytes:       db  '    0 bytes  underrun frames '
  .underruns:   db  '       0',0

txt_audio_none: db  'Audio: unavailable',0

txt_status:       dq  txt_error
txt_audio_status: dq  txt_audio_none
rom_path:         dq  rom_path_cd1
rom_error:        dq  0
rom_size:         dq  0
frame_count:      dq  0
last_frame_count: dq  0
last_time:        dq  0
rom_loaded:       db  0
next_frame_us:    dq  0
work_us:          dq  0
audio_on:         db  0
audio_bytes:      dq  0
audio_cur:        dq  0               ; Buffer to wait for, see audio_update
audio_rpos:       dd  0
audio_last:       dd  0
audio_underruns:  dq  0

param:          dq  ROM_PATH_LEN
                times ROM_PATH_LEN+1 db 0

image_end:

assert image_end <= LCD_RGB
assert LCD_RGB + VIEW_W*VIEW_H*4 <= AUDIO_BUF
assert AUDIO_BUF + AUDIO_BUF_MAX <= GB_STATE
assert GB_STATE + GB_STATE_SIZE <= STACK_TOP - 0x10000
