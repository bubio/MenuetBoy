; Memory bus, cartridge MBC and I/O registers.
;
; mem_read  In: edx = address   Out: eax = byte        Preserves others
; mem_write In: edx = address, al = byte               Clobbers rax, rdx


mem_read:

    cmp   edx , 0x8000
    jb    .rom
    cmp   edx , 0xA000
    jb    .vram
    cmp   edx , 0xC000
    jb    .cart_ram
    cmp   edx , 0xFE00
    jb    .wram
    cmp   edx , 0xFEA0
    jb    .oam
    cmp   edx , 0xFF00
    jb    .unusable
    cmp   edx , 0xFF80
    jb    io_read
    cmp   edx , 0xFFFF
    jb    .hram
    movzx eax , byte [gb_ie]
    ret

  .rom:
    mov   eax , edx
    and   eax , 0x3FFF
    cmp   edx , 0x4000
    jae   .rom_n
    add   rax , [rom_off0]
    movzx eax , byte [gb_rom+rax]
    ret
  .rom_n:
    add   rax , [rom_offn]
    movzx eax , byte [gb_rom+rax]
    ret

  .vram:
    movzx eax , byte [gb_vram-0x8000+rdx]
    ret

  .cart_ram:
    cmp   byte [ram_enable] , 0
    je    .unusable
    cmp   dword [ram_size] , 0
    je    .unusable
    cmp   byte [mbc_type] , MBC_2
    je    .mbc2_ram
    mov   eax , edx
    and   eax , 0x1FFF
    add   rax , [ram_off]
    movzx eax , byte [gb_cart_ram+rax]
    ret
  .mbc2_ram:                      ; 4-bit cells, repeated over A000-BFFF
    mov   eax , edx
    and   eax , 0x1FF
    movzx eax , byte [gb_cart_ram+rax]
    or    eax , 0xF0
    ret

  .wram:
    mov   eax , edx
    and   eax , 0x1FFF            ; C000-DFFF and echo E000-FDFF
    movzx eax , byte [gb_wram+rax]
    ret

  .oam:
    cmp   byte [dma_active] , 0
    jne   .unusable
    movzx eax , byte [gb_oam-0xFE00+rdx]
    ret

  .unusable:
    mov   eax , 0xFF
    ret

  .hram:
    movzx eax , byte [gb_hram-0xFF80+rdx]
    ret


mem_write:

    cmp   edx , 0x8000
    jb    mbc_write
    cmp   edx , 0xA000
    jb    .vram
    cmp   edx , 0xC000
    jb    .cart_ram
    cmp   edx , 0xFE00
    jb    .wram
    cmp   edx , 0xFEA0
    jb    .oam
    cmp   edx , 0xFF00
    jb    .done
    cmp   edx , 0xFF80
    jb    io_write
    cmp   edx , 0xFFFF
    jb    .hram
    mov   [gb_ie] , al
  .done:
    ret

  .vram:
    mov   [gb_vram-0x8000+rdx] , al
    ret

  .cart_ram:
    cmp   byte [ram_enable] , 0
    je    .done
    cmp   dword [ram_size] , 0
    je    .done
    and   edx , 0x1FFF
    cmp   byte [mbc_type] , MBC_2
    jne   @f
    and   edx , 0x1FF
  @@:
    add   rdx , [ram_off]
    mov   [gb_cart_ram+rdx] , al
    ret

  .wram:
    and   edx , 0x1FFF
    mov   [gb_wram+rdx] , al
    ret

  .oam:
    cmp   byte [dma_active] , 0
    jne   .done
    mov   [gb_oam-0xFE00+rdx] , al
    ret

  .hram:
    mov   [gb_hram-0xFF80+rdx] , al
    ret


; ROM area writes control the MBC.

mbc_write:

    push  rcx
    movzx ecx , byte [mbc_type]
    cmp   ecx , MBC_NONE
    je    .done
    movzx eax , al

    cmp   ecx , MBC_2
    jne   @f
    cmp   edx , 0x4000
    jae   .done
    test  edx , 0x100             ; Address bit 8 selects ROM bank / RAM enable
    jz    .ram_enable
    mov   [mbc_bank_lo] , eax
    jmp   .update
  @@:
    cmp   edx , 0x2000
    jae   .not_ram_enable
  .ram_enable:
    and   eax , 0x0F
    cmp   eax , 0x0A
    sete  [ram_enable]
    jmp   .done

  .not_ram_enable:
    cmp   edx , 0x4000
    jae   .not_bank_lo
    cmp   ecx , MBC_5
    je    .mbc5_bank_lo
    mov   [mbc_bank_lo] , eax     ; Masked in mbc_update
    jmp   .update
  .mbc5_bank_lo:
    cmp   edx , 0x3000
    jae   .mbc5_bank_bit8
    and   dword [mbc_bank_lo] , 0x100
    or    [mbc_bank_lo] , eax
    jmp   .update
  .mbc5_bank_bit8:
    and   eax , 1
    shl   eax , 8
    and   dword [mbc_bank_lo] , 0xFF
    or    [mbc_bank_lo] , eax
    jmp   .update

  .not_bank_lo:
    cmp   edx , 0x6000
    jae   .mode
    mov   [mbc_bank_hi] , eax
    jmp   .update

  .mode:
    cmp   ecx , MBC_1
    jne   .done
    and   eax , 1
    mov   [mbc1_mode] , al

  .update:
    call  mbc_update
  .done:
    pop   rcx
    ret


; Recompute ROM/RAM offsets from the MBC registers.

mbc_update:

    push  rcx
    push  rdx
    movzx ecx , byte [mbc_type]
    mov   eax , [mbc_bank_lo]
    mov   edx , [mbc_bank_hi]

    cmp   ecx , MBC_1
    jne   .not_mbc1
    and   eax , 0x1F
    jnz   @f
    inc   eax
  @@:
    and   edx , 3
    shl   edx , 5
    or    eax , edx
    and   eax , [rom_bank_mask]
    shl   rax , 14
    mov   [rom_offn] , rax
    xor   eax , eax
    cmp   byte [mbc1_mode] , 0
    je    @f
    mov   eax , edx
    and   eax , [rom_bank_mask]
    shl   rax , 14
  @@:
    mov   [rom_off0] , rax
    xor   eax , eax
    cmp   byte [mbc1_mode] , 0
    je    .ram_bank
    mov   eax , [mbc_bank_hi]
    and   eax , 3
    jmp   .ram_bank

  .not_mbc1:
    cmp   ecx , MBC_2
    jne   .not_mbc2
    and   eax , 0x0F
    jnz   @f
    inc   eax
  @@:
    and   eax , [rom_bank_mask]
    shl   rax , 14
    mov   [rom_offn] , rax
    xor   eax , eax
    jmp   .ram_bank

  .not_mbc2:
    cmp   ecx , MBC_3
    jne   .mbc5
    and   eax , 0x7F
    jnz   @f
    inc   eax
  @@:
    and   eax , [rom_bank_mask]
    shl   rax , 14
    mov   [rom_offn] , rax
    mov   eax , edx
    and   eax , 3                 ; RTC registers (8-C) are not emulated
    jmp   .ram_bank

  .mbc5:
    and   eax , 0x1FF
    and   eax , [rom_bank_mask]
    shl   rax , 14
    mov   [rom_offn] , rax
    mov   eax , edx
    and   eax , 0x0F

  .ram_bank:
    shl   eax , 13
    mov   edx , [ram_size]
    test  edx , edx
    jz    @f
    dec   edx
    and   eax , edx
  @@:
    mov   [ram_off] , rax
    pop   rdx
    pop   rcx
    ret


; I/O registers FF00-FF7F.

io_read:

    mov   eax , edx
    and   eax , 0x7F
    cmp   eax , IO_P1
    je    .joypad
    cmp   eax , IO_DIV
    je    .div
    cmp   eax , IO_IF
    je    .if
    cmp   eax , 0x10
    jb    @f
    cmp   eax , 0x40
    jb    apu_read
  @@:
    push  rdx
    movzx edx , byte [io_unused_bits+rax]
    or    dl , [gb_io+rax]
    mov   eax , edx
    pop   rdx
    ret

  .div:
    movzx eax , byte [div_counter+1]
    ret

  .if:
    movzx eax , byte [gb_io+IO_IF]
    or    eax , 0xE0
    ret

  .joypad:
    push  rcx
    movzx eax , byte [gb_io+IO_P1]
    and   eax , 0x30
    mov   ecx , 0x0F              ; 1 = not pressed
    test  eax , 0x10
    jnz   @f
    mov   ch , [joy_buttons]
    not   ch
    and   cl , ch
  @@:
    test  eax , 0x20
    jnz   @f
    mov   ch , [joy_buttons]
    shr   ch , 4
    not   ch
    and   cl , ch
  @@:
    and   ecx , 0x0F
    or    eax , ecx
    or    eax , 0xC0
    pop   rcx
    ret


io_write:

    and   edx , 0x7F
    cmp   edx , IO_DIV
    je    .div
    cmp   edx , IO_TIMA
    je    tima_write
    cmp   edx , IO_TMA
    je    tma_write
    cmp   edx , IO_TAC
    je    tac_write
    cmp   edx , IO_SC
    je    .serial
    cmp   edx , IO_LY
    je    .done
    cmp   edx , IO_STAT
    je    .stat
    cmp   edx , IO_DMA
    je    .dma
    cmp   edx , IO_LCDC
    je    .lcdc
    cmp   edx , IO_LYC
    je    .lyc
    cmp   edx , 0x10
    jb    @f
    cmp   edx , 0x40
    jb    apu_write
  @@:
    mov   [gb_io+rdx] , al
  .done:
    ret

  .div:
    jmp   div_reset

  .lyc:
    mov   [gb_io+IO_LYC] , al
    mov   dword [ppu_next_dot] , 0
    ret

  .stat:
    mov   dword [ppu_next_dot] , 0
    and   al , 0x78
    mov   ah , [gb_io+IO_STAT]
    and   ah , 0x07
    or    al , ah
    mov   [gb_io+IO_STAT] , al
    ret

  .lcdc:
    mov   dword [ppu_next_dot] , 0
    mov   ah , [gb_io+IO_LCDC]
    mov   [gb_io+IO_LCDC] , al
    test  al , 0x80
    jnz   .done
    test  ah , 0x80
    jz    .done
    mov   byte [gb_io+IO_LY] , 0  ; LCD turned off
    mov   dword [ppu_dot] , 0
    mov   byte [ppu_mode] , 0
    and   byte [gb_io+IO_STAT] , 0xFC
    ret

  .dma:                           ; Copied one byte per M-cycle by dma_tick
    mov   [gb_io+IO_DMA] , al
    movzx edx , al
    shl   edx , 8
    cmp   edx , 0xE000            ; E000-FFFF read from WRAM
    jb    @f
    sub   edx , 0x2000
  @@:
    mov   [dma_next_src] , edx
    mov   byte [dma_delay] , 2
    ret

  .serial:
    mov   [gb_io+IO_SC] , al
    test  al , 0x80
    jz    .done
    movzx eax , byte [gb_io+IO_SB]
    push  rcx rsi rdi r8 r9 r10 r11
    call  platform_serial
    pop   r11 r10 r9 r8 rdi rsi rcx
    and   byte [gb_io+IO_SC] , 0x7F
    mov   byte [gb_io+IO_SB] , 0xFF
    or    byte [gb_io+IO_IF] , 0x08
    ret


; Bits that read as 1 (unused bits and registers). Joypad, DIV, IF and
; sound registers are handled separately.
io_unused_bits:
    db  0x00 , 0x00 , 0x7E , 0xFF , 0x00 , 0x00 , 0x00 , 0xF8   ; FF00
    db  0xFF , 0xFF , 0xFF , 0xFF , 0xFF , 0xFF , 0xFF , 0x00   ; FF08
    times 0x30 db 0                                              ; FF10 sound
    db  0x00 , 0x80 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00 , 0x00   ; FF40
    db  0x00 , 0x00 , 0x00 , 0x00                                ; FF48
    times 0x34 db 0xFF                                           ; FF4C-FF7F
assert $ - io_unused_bits = 0x80
