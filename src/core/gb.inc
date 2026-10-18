; MenuetBoy emulator core. Platform independent (no system calls).
;
; Entry points:
;   gb_reset      Initialize state for the ROM at gb_rom (post-boot-ROM state)
;   gb_run_frame  Emulate one frame (70224 T-cycles)
;   gb_set_button Press or release a joypad button
;
; Sound is written to apu_ring, see apu.inc.
;
; The frontend provides platform_serial (In: al = byte sent on the
; serial port). It may clobber rax, rcx, rdx, rsi, rdi, r8-r11.

include 'state.inc'


gb_reset:

    push  rdi
    push  rcx
    mov   rdi , GB_STATE
    mov   ecx , GB_STATE_SIZE
    xor   eax , eax
    cld
    rep   stosb
    pop   rcx
    pop   rdi

    mov   word [cpu_f] , 0x01B0   ; A=01 F=B0
    mov   word [cpu_c] , 0x0013   ; B=00 C=13
    mov   word [cpu_e] , 0x00D8   ; D=00 E=D8
    mov   word [cpu_l] , 0x014D   ; H=01 L=4D
    mov   word [cpu_sp] , 0xFFFE
    mov   word [cpu_pc] , 0x0100
    mov   dword [div_counter] , 0xABCC

    mov   byte [gb_io+IO_P1] , 0xCF
    mov   byte [gb_io+IO_SC] , 0x7E
    mov   byte [gb_io+IO_TAC] , 0xF8
    mov   byte [gb_io+IO_IF] , 0xE1
    mov   byte [gb_io+IO_LCDC] , 0x91
    mov   byte [gb_io+IO_STAT] , 0x85
    mov   byte [gb_io+IO_BGP] , 0xFC
    mov   byte [gb_io+IO_OBP0] , 0xFF
    mov   byte [gb_io+IO_OBP1] , 0xFF

    ; Cartridge type
    movzx eax , byte [gb_rom+0x147]
    mov   cl , MBC_NONE
    cmp   eax , 0x01
    jb    .mbc_set
    mov   cl , MBC_1
    cmp   eax , 0x03
    jbe   .mbc_set
    mov   cl , MBC_3
    cmp   eax , 0x0F
    jb    .mbc_none
    cmp   eax , 0x13
    jbe   .mbc_set
    mov   cl , MBC_5
    cmp   eax , 0x19
    jb    .mbc_none
    cmp   eax , 0x1E
    jbe   .mbc_set
  .mbc_none:
    mov   cl , MBC_NONE
  .mbc_set:
    mov   [mbc_type] , cl

    ; ROM size: 32KB shl n
    movzx ecx , byte [gb_rom+0x148]
    cmp   ecx , 8
    jbe   @f
    mov   ecx , 8
  @@:
    mov   eax , 2
    shl   eax , cl
    dec   eax
    mov   [rom_bank_mask] , eax

    ; RAM size
    movzx ecx , byte [gb_rom+0x149]
    xor   eax , eax
    cmp   ecx , 5
    ja    @f
    mov   eax , [ram_size_table+rcx*4]
  @@:
    mov   [ram_size] , eax

    mov   dword [mbc_bank_lo] , 1
    call  mbc_update
    call  apu_reset
    ret


gb_run_frame:

    push  rbx rbp r12 r13 r14 r15
    xor   r12d , r12d
  .step:
    call  cpu_step
    add   r12d , eax
    call  gb_tick
    cmp   r12d , CYCLES_PER_FRAME
    jb    .step
    call  apu_sync
    pop   r15 r14 r13 r12 rbp rbx
    ret


; In: al = button bit (see joy_buttons), dl = 1 pressed / 0 released

gb_set_button:

    test  dl , dl
    jz    .release
    test  [joy_buttons] , al
    jnz   .done
    or    [joy_buttons] , al
    or    byte [gb_io+IO_IF] , 0x10   ; Joypad interrupt on press
    ret
  .release:
    not   al
    and   [joy_buttons] , al
  .done:
    ret


; Advance timer and PPU.
; In: eax = T-cycles

gb_tick:

    add   [apu_pending] , eax
    mov   edx , [div_counter]
    add   [div_counter] , eax

    ; TIMA counts falling edges of the DIV counter bit selected by TAC
    movzx ecx , byte [gb_io+IO_TAC]
    test  ecx , 4
    jz    .ppu
    and   ecx , 3
    mov   cl , [tima_shift+rcx]
    push  rsi
    mov   esi , [div_counter]
    shr   esi , cl
    shr   edx , cl
    sub   esi , edx
    mov   edx , -1                ; Mask out the wrap of div_counter
    shr   edx , cl
    and   esi , edx
    jz    .tima_done
  .tima:
    call  tima_inc
    dec   esi
    jnz   .tima
  .tima_done:
    pop   rsi

  .ppu:
    test  byte [gb_io+IO_LCDC] , 0x80
    jz    .done
    add   [ppu_dot] , eax
  .line:
    cmp   dword [ppu_dot] , CYCLES_PER_LINE
    jb    .stat
    sub   dword [ppu_dot] , CYCLES_PER_LINE
    mov   dl , [gb_io+IO_LY]
    inc   dl
    cmp   dl , 154
    jb    @f
    xor   dl , dl
  @@:
    mov   [gb_io+IO_LY] , dl
    cmp   dl , 144
    jne   .line
    or    byte [gb_io+IO_IF] , 0x01   ; VBlank
    jmp   .line
  .stat:
    call  ppu_update_stat
  .done:
    ret


tima_inc:

    inc   byte [gb_io+IO_TIMA]
    jnz   @f
    mov   dl , [gb_io+IO_TMA]
    mov   [gb_io+IO_TIMA] , dl
    or    byte [gb_io+IO_IF] , 0x04
  @@:
    ret


; DIV write. Resetting the counter is a falling edge for any bit that
; was set, which can clock TIMA and the APU frame sequencer.
; Clobbers rax, rdx.

div_reset:

    call  apu_div_reset
    movzx edx , byte [gb_io+IO_TAC]
    test  edx , 4
    jz    .reset
    and   edx , 3
    push  rcx
    mov   cl , [tima_shift+rdx]
    dec   cl
    mov   eax , [div_counter]
    shr   eax , cl
    pop   rcx
    test  eax , 1
    jz    .reset
    call  tima_inc
  .reset:
    mov   dword [div_counter] , 0
    ret


; Update STAT mode/coincidence bits and raise the STAT interrupt on
; a rising edge of the combined interrupt line.

ppu_update_stat:

    movzx ecx , byte [gb_io+IO_LY]
    mov   eax , 1
    cmp   ecx , 144
    jae   .mode
    mov   edx , [ppu_dot]
    mov   eax , 2
    cmp   edx , 80
    jb    .mode
    mov   eax , 3
    cmp   edx , 252
    jb    .mode
    xor   eax , eax
  .mode:
    cmp   byte [ppu_mode] , 3
    jne   @f
    test  eax , eax
    jnz   @f
    push  rax rcx
    call  ppu_render_line         ; Entered HBlank
    pop   rcx rax
  @@:
    mov   [ppu_mode] , al

    mov   dl , [gb_io+IO_STAT]
    and   dl , 0xF8
    or    dl , al
    cmp   cl , [gb_io+IO_LYC]
    jne   @f
    or    dl , 0x04
  @@:
    mov   [gb_io+IO_STAT] , dl

    xor   ecx , ecx
    test  dl , 0x04
    jz    @f
    test  dl , 0x40
    jz    @f
    mov   ecx , 1
  @@:
    test  dl , [stat_mode_bit+rax]
    jz    @f
    mov   ecx , 1
  @@:
    cmp   cl , [stat_line]
    jbe   @f
    or    byte [gb_io+IO_IF] , 0x02
  @@:
    mov   [stat_line] , cl
    ret


tima_shift:     db  10 , 4 , 6 , 8   ; TIMA period 1024/16/64/256 T-cycles
ram_size_table: dd  0 , 0x800 , 0x2000 , 0x8000 , 0x20000 , 0x10000
stat_mode_bit:  db  0x08 , 0x10 , 0x20 , 0x00

include 'ppu.inc'
include 'mmu.inc'
include 'apu.inc'
include 'cpu.inc'
