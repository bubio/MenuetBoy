; GB emulator state.
;
; The frontend defines:
;   GB_STATE  - address of a writable area of GB_STATE_SIZE bytes
;   gb_rom    - address of the loaded ROM image (up to 8MB)

IO_P1       = 0x00
IO_SB       = 0x01
IO_SC       = 0x02
IO_DIV      = 0x04
IO_TIMA     = 0x05
IO_TMA      = 0x06
IO_TAC      = 0x07
IO_IF       = 0x0F
IO_LCDC     = 0x40
IO_STAT     = 0x41
IO_SCY      = 0x42
IO_SCX      = 0x43
IO_LY       = 0x44
IO_LYC      = 0x45
IO_DMA      = 0x46
IO_BGP      = 0x47
IO_OBP0     = 0x48
IO_OBP1     = 0x49
IO_WY       = 0x4A
IO_WX       = 0x4B
IO_NR52     = 0x26

MBC_NONE    = 0
MBC_1       = 1
MBC_2       = 2
MBC_3       = 3
MBC_5       = 5

CYCLES_PER_LINE  = 456
CYCLES_PER_SEC   = 4194304
CYCLES_PER_FRAME = 70224

APU_RATE    = 48000               ; Output sample rate
APU_RING    = 16384               ; Output ring size in frames (power of two)

virtual at GB_STATE

  ; CPU registers. Pairs are little-endian words: BC at cpu_c,
  ; DE at cpu_e, HL at cpu_l, AF at cpu_f.
  cpu_c           db ?
  cpu_b           db ?
  cpu_e           db ?
  cpu_d           db ?
  cpu_l           db ?
  cpu_h           db ?
  cpu_f           db ?
  cpu_a           db ?
  cpu_sp          dw ?
  cpu_pc          dw ?
  cpu_ime         db ?
  cpu_ei_pending  db ?            ; EI executed, IME set after next instruction
  cpu_ei_now      db ?
  cpu_halted      db ?
  cpu_halt_bug    db ?            ; Next opcode fetch does not advance PC
  op_cycles       dd ?            ; T-cycles of the current instruction
  cpu_ticked      dd ?            ; Of those, already passed to gb_tick

  div_counter     dd ?            ; DIV is bits 8-15
  tima_state      db ?            ; 0 counting, 1 overflowed (TIMA=00),
                                  ; 2 reloaded in this M-cycle
  ppu_dot         dd ?            ; Dot within the current line
  ppu_next_dot    dd ?            ; Next dot where STAT may change, 0 = now
  ppu_mode        db ?
  stat_line       db ?            ; STAT interrupt line, for edge detection
  win_line        db ?            ; Window internal line counter
  pal_bg          rb 4            ; Color index -> shade
  pal_obj0        rb 4
  pal_obj1        rb 4            ; Must follow pal_obj0

  mbc_type        db ?
  ram_enable      db ?
  mbc1_mode       db ?
  mbc_bank_lo     dd ?
  mbc_bank_hi     dd ?
  rom_bank_mask   dd ?
  ram_size        dd ?
  cart_ram_dirty  db ?            ; Set on cartridge RAM writes, cleared by the frontend
  rtc_present     db ?            ; MBC3 with timer (cartridge types 0F, 10)
  rtc_latch_prev  db ?            ; Last value written to 6000-7FFF
  rtc_regs        rb 5            ; Counting S, M, H, DL, DH (bit 0 day bit 8,
                                  ; bit 6 halt, bit 7 day carry)
  rtc_latched     rb 5            ; Read through A000-BFFF (banks 08-0C)
  rtc_cycles      dd ?            ; T-cycles into the current second
  rom_off0        dq ?            ; ROM offset mapped at 0000-3FFF
  rom_offn        dq ?            ; ROM offset mapped at 4000-7FFF
  ram_off         dq ?            ; Cartridge RAM offset mapped at A000-BFFF

  joy_buttons     db ?            ; 1=pressed: 0 right 1 left 2 up 3 down
                                  ;            4 A 5 B 6 select 7 start
  gb_ie           db ?

  dma_active      db ?            ; OAM DMA copying (OAM blocked for the CPU)
  dma_delay       db ?            ; M-cycles until a written DMA starts
  dma_pos         dd ?            ; Next byte 0-159
  dma_src         dd ?
  dma_next_src    dd ?

  apu_pending     dd ?            ; T-cycles not yet run by apu_sync
  apu_next        dd ?            ; T-cycles to the next sample, 16.16
  apu_wpos        dd ?            ; Frames written to apu_ring
  fs_step         db ?            ; Next frame sequencer step 0-7
  ch1_sweep_timer db ?
  ch1_sweep_en    db ?
  ch1_neg_used    db ?            ; Negate used since trigger
  ch1_shadow      dd ?
  align 8
  apu_hpf_l       dq ?            ; High-pass capacitors, 16.16
  apu_hpf_r       dq ?
  apu_ch          rb 32*4         ; Channel state, see apu.inc

  align 16
  gb_io           rb 0x80
  gb_hram         rb 0x80
  gb_vram         rb 0x2000
  gb_wram         rb 0x2000
  gb_oam          rb 0xA0
  align 16
  gb_cart_ram     rb 0x20000
  gb_lcd          rb 160*144      ; Shade 0-3 per pixel
  bg_idx          rb 160          ; BG/window color index of the current line
  obj_line        rb 160          ; Object pixel of the current line, see ppu.inc
  obj_keys        rd 10
  align 16
  apu_ring        rd APU_RING     ; L low word, R high word

  GB_STATE_SIZE = $ - GB_STATE

end virtual
