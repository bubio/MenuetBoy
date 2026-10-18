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
    cmp   eax , 0x05
    jb    .mbc_none
    mov   cl , MBC_2
    cmp   eax , 0x06
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
    cmp   eax , 0x0F
    je    @f
    cmp   eax , 0x10
    jne   .rtc_set
  @@:
    mov   byte [rtc_present] , 1
  .rtc_set:

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
    cmp   byte [mbc_type] , MBC_2
    jne   @f
    mov   eax , 0x200             ; MBC2: built-in 512 x 4 bits
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

    cmp   byte [dma_active] , 0
    jne   @f
    cmp   byte [dma_delay] , 0
    je    .no_dma
  @@:
    call  dma_tick
  .no_dma:
    cmp   byte [rtc_present] , 0
    je    @f
    call  rtc_tick
  @@:
    add   [apu_pending] , eax
    mov   ecx , eax
    shr   ecx , 2
    jz    .ppu
  .timer:
    call  timer_mcycle
    dec   ecx
    jnz   .timer

  .ppu:
    test  byte [gb_io+IO_LCDC] , 0x80
    jz    .done
    add   [ppu_dot] , eax
    mov   edx , [ppu_dot]
    cmp   edx , [ppu_next_dot]    ; STAT only changes at mode boundaries
    jb    .done
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


; OAM DMA: start after the delay, then copy one byte per M-cycle.
; In: eax = T-cycles   Preserves rax

dma_tick:

    push  rax rcx rdx
    mov   ecx , eax
    shr   ecx , 2
    jz    .done
  .cycle:
    cmp   byte [dma_delay] , 0
    je    .copy
    dec   byte [dma_delay]
    jnz   .copy
    mov   eax , [dma_next_src]    ; A restarted DMA takes over here
    mov   [dma_src] , eax
    mov   dword [dma_pos] , 0
    mov   byte [dma_active] , 1
    jmp   .next
  .copy:
    cmp   byte [dma_active] , 0
    je    .next
    mov   edx , [dma_src]
    add   edx , [dma_pos]
    call  mem_read
    mov   edx , [dma_pos]
    mov   [gb_oam+rdx] , al
    inc   edx
    mov   [dma_pos] , edx
    cmp   edx , 0xA0
    jb    .next
    mov   byte [dma_active] , 0
  .next:
    dec   ecx
    jnz   .cycle
  .done:
    pop   rdx rcx rax
    ret


; MBC3 clock, counted in emulated time. Out-of-range values wrap at
; their bit width without carrying, as on hardware.
; In: eax = T-cycles   Preserves rax, rcx

rtc_tick:

    test  byte [rtc_regs+4] , 0x40
    jnz   .done
    add   [rtc_cycles] , eax
    cmp   dword [rtc_cycles] , CYCLES_PER_SEC
    jb    .done
    sub   dword [rtc_cycles] , CYCLES_PER_SEC
    push  rax
    mov   al , [rtc_regs]
    inc   al
    and   al , 0x3F
    cmp   al , 60
    jne   .s
    mov   byte [rtc_regs] , 0
    mov   al , [rtc_regs+1]
    inc   al
    and   al , 0x3F
    cmp   al , 60
    jne   .m
    mov   byte [rtc_regs+1] , 0
    mov   al , [rtc_regs+2]
    inc   al
    and   al , 0x1F
    cmp   al , 24
    jne   .h
    mov   byte [rtc_regs+2] , 0
    inc   byte [rtc_regs+3]
    jnz   .pop
    xor   byte [rtc_regs+4] , 1   ; Day bit 8; 511 -> 0 sets the carry
    test  byte [rtc_regs+4] , 1
    jnz   .pop
    or    byte [rtc_regs+4] , 0x80
    jmp   .pop
  .s:
    mov   [rtc_regs] , al
    jmp   .pop
  .m:
    mov   [rtc_regs+1] , al
    jmp   .pop
  .h:
    mov   [rtc_regs+2] , al
  .pop:
    pop   rax
  .done:
    ret


; Write the RTC register selected by RAM bank 08-0C. Writing the
; seconds restarts the current second.
; In: al = value   Clobbers rax, rdx

rtc_write:

    mov   edx , [mbc_bank_hi]
    sub   edx , 8
    cmp   edx , 4
    ja    .done
    and   al , [rtc_reg_mask+rdx]
    mov   [rtc_regs+rdx] , al
    mov   [rtc_latched+rdx] , al
    test  edx , edx
    jnz   .done
    mov   dword [rtc_cycles] , 0
  .done:
    ret


; Advance DIV by one M-cycle. TIMA counts falling edges of the DIV
; counter bit selected by TAC. TIMA reads 00 in the M-cycle it
; overflows and is reloaded from TMA in the next one (see tima_state).
; Clobbers rdx. Preserves rax, rcx.

timer_mcycle:

    mov   dl , [tima_state]
    test  dl , dl
    jz    .count
    mov   byte [tima_state] , 0
    cmp   dl , 1
    jne   .count
    mov   dl , [gb_io+IO_TMA]
    mov   [gb_io+IO_TIMA] , dl
    or    byte [gb_io+IO_IF] , 0x04
    mov   byte [tima_state] , 2

  .count:
    mov   edx , [div_counter]
    add   dword [div_counter] , 4
    test  byte [gb_io+IO_TAC] , 4
    jz    .done
    push  rax rcx
    movzx ecx , byte [gb_io+IO_TAC]
    and   ecx , 3
    mov   cl , [tima_shift+rcx]
    mov   eax , [div_counter]
    shr   eax , cl
    shr   edx , cl
    cmp   eax , edx
    je    @f
    call  tima_inc
  @@:
    pop   rcx rax
  .done:
    ret


tima_inc:                         ; Clobbers rdx

    inc   byte [gb_io+IO_TIMA]
    jnz   @f
    mov   byte [tima_state] , 1
  @@:
    ret


; TIMA / TMA / TAC writes. In: al = value. Clobbers rax, rdx.

tima_write:

    cmp   byte [tima_state] , 2   ; Ignored in the reload cycle
    je    @f
    mov   byte [tima_state] , 0   ; Cancels a pending reload
    mov   [gb_io+IO_TIMA] , al
  @@:
    ret

tma_write:

    mov   [gb_io+IO_TMA] , al
    cmp   byte [tima_state] , 2
    jne   @f
    mov   [gb_io+IO_TIMA] , al
  @@:
    ret

tac_write:                        ; Disabling or switching away from a set
                                  ; DIV bit is a falling edge
    push  rcx
    or    al , 0xF8
    mov   ah , [gb_io+IO_TAC]
    mov   [gb_io+IO_TAC] , al
    mov   al , ah
    call  tac_input
    mov   ah , dl
    mov   al , [gb_io+IO_TAC]
    call  tac_input
    cmp   ah , dl
    jbe   @f
    call  tima_inc
  @@:
    pop   rcx
    ret

tac_input:                        ; In: al = TAC   Out: dl = timer input bit

    xor   edx , edx
    test  al , 4
    jz    @f
    movzx ecx , al
    and   ecx , 3
    mov   cl , [tima_shift+rcx]
    dec   cl
    mov   edx , [div_counter]
    shr   edx , cl
    and   edx , 1
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
    mov   edx , [ppu_mode_end+rax*4]
    mov   [ppu_next_dot] , edx
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
rtc_reg_mask:   db  0x3F , 0x3F , 0x1F , 0xFF , 0xC1
stat_mode_bit:  db  0x08 , 0x10 , 0x20 , 0x00
ppu_mode_end:   dd  CYCLES_PER_LINE , CYCLES_PER_LINE , 80 , 252   ; Dot where mode 0-3 ends

include 'ppu.inc'
include 'mmu.inc'
include 'apu.inc'
include 'cpu.inc'
