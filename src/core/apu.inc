; Sound (DMG APU).
;
; Channels run lazily: gb_tick only adds to apu_pending, and apu_sync
; catches up before any sound register access, before DIV is reset and
; at the end of each frame. The frame sequencer is clocked by the
; falling edge of DIV bit 12 (512Hz), as on hardware.
;
; Output is signed 16-bit stereo (L low word, R high word) at APU_RATE
; into the ring apu_ring. apu_wpos counts frames written; the frontend
; keeps its own read position.

APU_SAMPLE_STEP = (4194304 shl 16) / APU_RATE   ; T-cycles per sample, 16.16
APU_GAIN        = 64              ; Max mix 4ch * 15 * 8 -> 30720
APU_NYQUIST_STEP = (2*4194304) / (APU_RATE*32)  ; Wave step at or below this
                                  ; many cycles: pattern above Nyquist

CH_ON    = 0
CH_DAC   = 1
CH_VOL   = 2
CH_ENV   = 3                      ; Envelope countdown
CH_POS   = 4                      ; Duty step / wave sample index
CH_LEN   = 8                      ; Length counter
CH_TIMER = 12                     ; Cycles to the next waveform step
CH_LFSR  = 16
CH_AREA  = 20                     ; Wave/noise output * cycles * 32 since the
                                  ; last sample (box filter)
CH_SIZE  = 32

; Channel n registers are NRn0-NRn4 at 0x10+5*n (n = 0..3 for ch1-ch4).


apu_reset:

    push  rsi
    mov   rsi , apu_boot_regs
    xor   ecx , ecx
  @@:
    mov   al , [rsi+rcx]
    mov   [gb_io+0x10+rcx] , al
    inc   ecx
    cmp   ecx , 0x17
    jb    @b
    pop   rsi
    mov   byte [apu_ch+CH_ON] , 1     ; Boot beep: ch1 on, volume 0
    mov   byte [apu_ch+CH_DAC] , 1
    mov   dword [apu_next] , APU_SAMPLE_STEP
    ret


; Run the APU up to the current CPU time. Preserves all registers.

apu_sync:

    cmp   dword [apu_pending] , 0
    jne   @f
    ret
  @@:
    push  rax rbx rcx rdx rsi r8 r9 r10 r11 r12 r13
    mov   r12d , [apu_pending]
    mov   dword [apu_pending] , 0
    mov   r13d , [div_counter]
    sub   r13d , r12d             ; DIV counter at the start
  .loop:
    mov   eax , [apu_next]        ; Cycles to the next sample (rounded up)
    add   eax , 0xFFFF
    shr   eax , 16
    mov   ecx , r13d              ; Cycles to the next DIV bit 12 fall
    and   ecx , 0x1FFF
    mov   edx , 0x2000
    sub   edx , ecx
    cmp   eax , edx
    cmova eax , edx
    cmp   eax , r12d
    cmova eax , r12d
    mov   r8d , eax
    add   [apu_area_cycles] , eax

    test  byte [gb_io+IO_NR52] , 0x80
    jz    @f
    call  apu_run
  @@:
    add   r13d , r8d
    test  r13d , 0x1FFF
    jnz   @f
    test  byte [gb_io+IO_NR52] , 0x80
    jz    @f
    call  apu_frame_seq
  @@:
    mov   eax , r8d
    shl   eax , 16
    sub   [apu_next] , eax
    jg    @f
    add   dword [apu_next] , APU_SAMPLE_STEP
    call  apu_emit
  @@:
    sub   r12d , r8d
    jnz   .loop
    pop   r13 r12 r11 r10 r9 r8 rsi rdx rcx rbx rax
    ret


; Advance channel waveforms. In: r8d = T-cycles
; Square and wave advance all elapsed steps at once; noise steps one by
; one since each step shifts the LFSR. Wave and noise also add their
; output over the cycles to CH_AREA, so a sample is the mean over its
; interval instead of a point sample, which aliases badly when the wave
; channel plays sampled sound.

apu_run:

    xor   ecx , ecx
    mov   rbx , apu_ch
  .channel:
    cmp   byte [rbx+CH_ON] , 0
    je    .next
    cmp   ecx , 2
    jb    .steps
    ja    .noise
    call  wave_area
  .steps:
    sub   [rbx+CH_TIMER] , r8d
    jg    .next
    call  apu_period
    mov   r9d , eax
    mov   eax , [rbx+CH_TIMER]
    neg   eax                     ; Cycles past the first step
    xor   edx , edx
    div   r9d
    inc   eax                     ; Steps taken
    imul  r9d , eax
    add   [rbx+CH_TIMER] , r9d
    add   al , [rbx+CH_POS]
    mov   dl , 7                  ; Square: 8 duty steps
    cmp   ecx , 2
    jne   @f
    mov   dl , 31                 ; Wave: 32 samples
  @@:
    and   al , dl
    mov   [rbx+CH_POS] , al
    jmp   .next
  .noise:
    call  noise_run
  .next:
    add   rbx , CH_SIZE
    inc   ecx
    cmp   ecx , 4
    jb    .channel
    ret


; Add the wave output over r8d cycles to CH_AREA. In: rbx = ch3 state
; Clobbers rax, rdx, rsi, r9, r10.

wave_area:

    push  rcx
    call  apu_period              ; ecx = 2
    mov   r9d , eax
    movzx ecx , byte [gb_io+0x1C]
    shr   ecx , 5
    and   ecx , 3
    jz    .done                   ; Volume code 0: mute
    dec   ecx                     ; cl = output shift
    cmp   r9d , APU_NYQUIST_STEP
    jbe   .pattern
    mov   r10d , r8d              ; Cycles left
    mov   eax , [rbx+CH_TIMER]    ; Cycles still at the current sample
    movzx esi , byte [rbx+CH_POS]
  .segment:
    cmp   eax , r10d
    cmova eax , r10d
    sub   r10d , eax
    mov   edx , esi
    shr   edx , 1
    movzx edx , byte [gb_io+0x30+rdx]
    test  esi , 1
    jnz   @f
    shr   edx , 4                 ; Even samples are the high nibble
  @@:
    and   edx , 0x0F
    shr   edx , cl
    imul  edx , eax
    shl   edx , 5
    add   [rbx+CH_AREA] , edx
    test  r10d , r10d
    jz    .done
    inc   esi
    and   esi , 31
    mov   eax , r9d
    jmp   .segment
  .pattern:                       ; Above Nyquist: mean of the 32 samples
    xor   edx , edx
    xor   esi , esi
  @@:
    movzx eax , byte [gb_io+0x30+rsi]
    mov   r10d , eax
    shr   eax , 4
    shr   eax , cl
    add   edx , eax
    and   r10d , 0x0F
    shr   r10d , cl
    add   edx , r10d
    inc   esi
    cmp   esi , 16
    jb    @b
    imul  edx , r8d
    add   [rbx+CH_AREA] , edx
  .done:
    pop   rcx
    ret


; Advance the noise LFSR over r8d cycles and add its output to CH_AREA.
; In: rbx = ch4 state, ecx = 3   Clobbers rax, rdx, r9, r10.

noise_run:

    mov   r10d , r8d              ; Cycles left
  .segment:
    xor   edx , edx               ; Output: volume while LFSR bit 0 is 0
    test  byte [rbx+CH_LFSR] , 1
    jnz   @f
    movzx edx , byte [rbx+CH_VOL]
  @@:
    mov   eax , [rbx+CH_TIMER]
    cmp   eax , r10d
    jbe   .step
    imul  edx , r10d
    shl   edx , 5
    add   [rbx+CH_AREA] , edx
    sub   [rbx+CH_TIMER] , r10d
    ret
  .step:
    imul  edx , eax
    shl   edx , 5
    add   [rbx+CH_AREA] , edx
    sub   r10d , eax
    mov   eax , [rbx+CH_LFSR]     ; 15-bit LFSR, optionally 7-bit
    mov   edx , eax
    shr   edx , 1
    xor   edx , eax
    and   edx , 1
    shr   eax , 1
    mov   r9d , edx
    shl   edx , 14
    or    eax , edx
    test  byte [gb_io+0x22] , 0x08
    jz    @f
    and   eax , not 0x40
    shl   r9d , 6
    or    eax , r9d
  @@:
    mov   [rbx+CH_LFSR] , eax
    call  apu_period
    mov   [rbx+CH_TIMER] , eax
    test  r10d , r10d
    jnz   .segment
    ret


; Waveform step period in T-cycles. In: ecx = channel  Out: eax
; Clobbers edx.

apu_period:

    cmp   ecx , 3
    je    .noise
    lea   edx , [ecx*4+ecx]
    movzx eax , byte [gb_io+0x14+rdx]
    and   eax , 7
    shl   eax , 8
    or    al , [gb_io+0x13+rdx]
    neg   eax
    add   eax , 2048
    shl   eax , 2                 ; Square: (2048-f)*4
    cmp   ecx , 2
    jne   @f
    shr   eax , 1                 ; Wave: (2048-f)*2
  @@:
    ret
  .noise:
    push  rcx
    movzx ecx , byte [gb_io+0x22]
    mov   eax , ecx
    and   eax , 7
    shl   eax , 4
    jnz   @f
    mov   eax , 8                 ; Divisor code 0 is 8
  @@:
    shr   ecx , 4
    cmp   ecx , 14
    jb    @f
    mov   eax , 0x10000000        ; Shift 14-15: no clocks
    xor   ecx , ecx
  @@:
    shl   eax , cl
    pop   rcx
    ret


; Frame sequencer step: length on even steps, sweep on 2 and 6,
; envelope on 7.

apu_frame_seq:

    movzx eax , byte [fs_step]
    inc   byte [fs_step]
    and   byte [fs_step] , 7
    test  al , 1
    jnz   .odd
    push  rax
    call  apu_length_clock
    pop   rax
    cmp   al , 2
    je    apu_sweep_clock
    cmp   al , 6
    je    apu_sweep_clock
    ret
  .odd:
    cmp   al , 7
    je    apu_envelope_clock
    ret


apu_length_clock:

    xor   ecx , ecx
    mov   rbx , apu_ch
    mov   esi , 0x10
  .channel:
    test  byte [gb_io+rsi+4] , 0x40
    jz    .next
    cmp   dword [rbx+CH_LEN] , 0
    je    .next
    dec   dword [rbx+CH_LEN]
    jnz   .next
    mov   byte [rbx+CH_ON] , 0
  .next:
    add   rbx , CH_SIZE
    add   esi , 5
    inc   ecx
    cmp   ecx , 4
    jb    .channel
    ret


apu_envelope_clock:

    xor   ecx , ecx
    mov   rbx , apu_ch
    mov   esi , 0x10
  .channel:
    cmp   ecx , 2
    je    .next                   ; Wave has no envelope
    movzx eax , byte [gb_io+rsi+2]
    mov   edx , eax
    and   edx , 7
    jz    .next
    dec   byte [rbx+CH_ENV]
    jg    .next
    mov   [rbx+CH_ENV] , dl
    mov   dl , [rbx+CH_VOL]
    test  al , 0x08
    jz    .down
    cmp   dl , 15
    jae   .next
    inc   dl
    jmp   .set
  .down:
    test  dl , dl
    jz    .next
    dec   dl
  .set:
    mov   [rbx+CH_VOL] , dl
  .next:
    add   rbx , CH_SIZE
    add   esi , 5
    inc   ecx
    cmp   ecx , 4
    jb    .channel
    ret


apu_sweep_clock:

    dec   byte [ch1_sweep_timer]
    jnz   .done
    movzx eax , byte [gb_io+0x10]
    shr   eax , 4
    and   eax , 7
    mov   edx , eax
    test  edx , edx
    jnz   @f
    mov   edx , 8                 ; Period 0 counts as 8
  @@:
    mov   [ch1_sweep_timer] , dl
    test  eax , eax
    jz    .done
    cmp   byte [ch1_sweep_en] , 0
    je    .done
    cmp   byte [apu_ch+CH_ON] , 0
    je    .done
    call  sweep_calc
    cmp   eax , 2047
    ja    .done
    test  byte [gb_io+0x10] , 7
    jz    .done
    mov   [ch1_shadow] , eax
    mov   [gb_io+0x13] , al
    mov   dl , [gb_io+0x14]
    and   dl , 0xF8
    or    dl , ah
    mov   [gb_io+0x14] , dl
    call  sweep_calc              ; Overflow check with the new value
  .done:
    ret


; Out: eax = next sweep frequency. Disables ch1 on overflow.

sweep_calc:

    push  rcx
    mov   eax , [ch1_shadow]
    movzx ecx , byte [gb_io+0x10]
    and   ecx , 7
    mov   edx , eax
    shr   edx , cl
    test  byte [gb_io+0x10] , 0x08
    jz    .add
    sub   eax , edx
    mov   byte [ch1_neg_used] , 1
    jmp   .check
  .add:
    add   eax , edx
  .check:
    cmp   eax , 2047
    jbe   @f
    mov   byte [apu_ch+CH_ON] , 0
  @@:
    pop   rcx
    ret


; Mix one output frame into apu_ring.

apu_emit:

    xor   r9d , r9d               ; Left
    xor   r10d , r10d             ; Right
    test  byte [gb_io+IO_NR52] , 0x80
    jz    .volume
    movzx r11d , byte [gb_io+0x25]    ; NR51 panning
    xor   ecx , ecx
    mov   rbx , apu_ch
  .channel:                       ; eax = output in 1/16 steps
    xor   eax , eax
    cmp   ecx , 2
    jae   .mean
    cmp   byte [rbx+CH_ON] , 0
    je    .mix
    lea   edx , [ecx*4+ecx]       ; Square: duty pattern bit
    movzx edx , byte [gb_io+0x11+rdx]
    shr   edx , 6
    movzx edx , byte [duty_table+rdx]
    movzx esi , byte [rbx+CH_POS]
    bt    edx , esi
    jnc   .mix
    movzx eax , byte [rbx+CH_VOL]
    shl   eax , 4
    jmp   .mix
  .mean:                          ; Wave, noise: mean over the interval
    mov   eax , [rbx+CH_AREA]
    mov   esi , [apu_area_cycles]
    test  esi , esi
    jz    .mix
    shl   esi , 1                 ; CH_AREA is in 1/32 steps
    xor   edx , edx
    div   esi
  .mix:
    bt    r11d , ecx
    jnc   @f
    add   r10d , eax
  @@:
    lea   edx , [ecx+4]
    bt    r11d , edx
    jnc   @f
    add   r9d , eax
  @@:
    add   rbx , CH_SIZE
    inc   ecx
    cmp   ecx , 4
    jb    .channel

  .volume:
    movzx eax , byte [gb_io+0x24]     ; NR50 master volume
    mov   edx , eax
    shr   edx , 4
    and   edx , 7
    inc   edx
    imul  r9d , edx
    and   eax , 7
    inc   eax
    imul  r10d , eax
    imul  r9d , r9d , APU_GAIN/16
    imul  r10d , r10d , APU_GAIN/16
    xor   eax , eax               ; Next interval
    mov   [apu_area_cycles] , eax
    mov   dword [apu_ch+2*CH_SIZE+CH_AREA] , eax
    mov   dword [apu_ch+3*CH_SIZE+CH_AREA] , eax

    mov   rbx , apu_hpf_l         ; High-pass filter removes DC
    call  .filter
    mov   r11 , r9
    mov   r9 , r10
    add   rbx , 8
    call  .filter
    shl   r9d , 16
    and   r11d , 0xFFFF
    or    r9d , r11d
    mov   eax , [apu_wpos]
    and   eax , APU_RING-1
    mov   [apu_ring+rax*4] , r9d
    inc   dword [apu_wpos]
    ret

  .filter:                        ; In/out: r9, rbx = capacitor (16.16)
    mov   rax , [rbx]
    sar   rax , 16
    sub   r9 , rax
    mov   rax , r9
    shl   rax , 7                 ; Charge 1/512 of the difference
    add   [rbx] , rax
    cmp   r9 , -32768
    jge   @f
    mov   r9 , -32768
  @@:
    cmp   r9 , 32767
    jle   @f
    mov   r9 , 32767
  @@:
    ret


; Sound register read. In: eax = index 0x10-0x3F  Out: eax

apu_read:

    call  apu_sync
    cmp   eax , 0x30
    jae   .wave
    cmp   eax , IO_NR52
    je    .nr52
    push  rcx
    movzx ecx , byte [apu_read_mask-0x10+rax]
    movzx eax , byte [gb_io+rax]
    or    eax , ecx
    pop   rcx
    ret
  .wave:
    movzx eax , byte [gb_io+rax]
    ret
  .nr52:
    movzx eax , byte [gb_io+IO_NR52]
    and   eax , 0x80
    or    eax , 0x70
    cmp   byte [apu_ch+CH_SIZE*0+CH_ON] , 0
    je    @f
    or    eax , 1
  @@:
    cmp   byte [apu_ch+CH_SIZE*1+CH_ON] , 0
    je    @f
    or    eax , 2
  @@:
    cmp   byte [apu_ch+CH_SIZE*2+CH_ON] , 0
    je    @f
    or    eax , 4
  @@:
    cmp   byte [apu_ch+CH_SIZE*3+CH_ON] , 0
    je    @f
    or    eax , 8
  @@:
    ret


; Sound register write. In: edx = index 0x10-0x3F, al = value
; Clobbers rax, rdx.

apu_write:

    call  apu_sync
    cmp   edx , 0x30
    jae   .wave
    cmp   edx , IO_NR52
    je    apu_power
    ja    .ignore
    push  rbx rcx rsi
    movzx ecx , byte [apu_reg_slot-0x10+rdx]
    mov   esi , ecx
    and   esi , 0x0F              ; Register 0-4 within the channel
    shr   ecx , 4                 ; Channel, 4 = NR50/NR51
    imul  ebx , ecx , CH_SIZE
    add   rbx , apu_ch
    test  byte [gb_io+IO_NR52] , 0x80
    jz    .powered_off
    mov   ah , [gb_io+rdx]
    mov   [gb_io+rdx] , al
    cmp   ecx , 4
    je    .done
    call  qword [.by_reg+rsi*8]
  .done:
    pop   rsi rcx rbx
  .ignore:
    ret
  .wave:
    mov   [gb_io+rdx] , al
    ret
  .powered_off:                   ; DMG: length counters stay writable
    cmp   esi , 1
    jne   .done
    call  .nrx1
    jmp   .done

.by_reg:        dq  .nrx0 , .nrx1 , .nrx2 , .ret , .nrx4

  .nrx0:
    test  ecx , ecx
    jz    .nr10
    cmp   ecx , 2
    jne   .ret
    shr   al , 7                  ; NR30: DAC power
    mov   [rbx+CH_DAC] , al
    jnz   .ret
    mov   byte [rbx+CH_ON] , 0
    ret
  .nr10:                          ; Leaving negate mode after using it
    test  al , 0x08               ; disables the channel
    jnz   .ret
    cmp   byte [ch1_neg_used] , 0
    je    .ret
    mov   byte [rbx+CH_ON] , 0
    ret

  .nrx1:
    movzx eax , al
    mov   edx , 256
    cmp   ecx , 2
    je    @f
    and   eax , 0x3F
    mov   edx , 64
  @@:
    sub   edx , eax
    mov   [rbx+CH_LEN] , edx
    ret

  .nrx2:
    cmp   ecx , 2
    je    .ret
    test  al , 0xF8               ; DAC is on unless volume 0 and decrease
    setnz al
    mov   [rbx+CH_DAC] , al
    jnz   .ret
    mov   byte [rbx+CH_ON] , 0
  .ret:
    ret

  .nrx4:
    ; Enabling length when the next frame sequencer step does not clock
    ; length gives an extra clock.
    test  byte [fs_step] , 1
    jz    .trigger
    test  ah , 0x40
    jnz   .trigger
    test  al , 0x40
    jz    .trigger
    cmp   dword [rbx+CH_LEN] , 0
    je    .trigger
    dec   dword [rbx+CH_LEN]
    jnz   .trigger
    test  al , 0x80
    jnz   .trigger
    mov   byte [rbx+CH_ON] , 0
  .trigger:
    test  al , 0x80
    jz    .ret
    mov   dl , [rbx+CH_DAC]
    mov   [rbx+CH_ON] , dl
    cmp   dword [rbx+CH_LEN] , 0
    jne   .period
    mov   edx , 64
    cmp   ecx , 2
    jne   @f
    mov   edx , 256
  @@:
    test  al , 0x40
    jz    @f
    test  byte [fs_step] , 1
    jz    @f
    dec   edx
  @@:
    mov   [rbx+CH_LEN] , edx
  .period:
    call  apu_period
    mov   [rbx+CH_TIMER] , eax
    cmp   ecx , 2
    je    .trigger_wave
    lea   esi , [ecx*4+ecx]
    mov   al , [gb_io+0x12+rsi]   ; Envelope reload
    mov   dl , al
    shr   dl , 4
    mov   [rbx+CH_VOL] , dl
    and   al , 7
    mov   [rbx+CH_ENV] , al
    cmp   ecx , 3
    jne   @f
    mov   dword [rbx+CH_LFSR] , 0x7FFF
  @@:
    test  ecx , ecx
    jz    sweep_trigger
    ret
  .trigger_wave:
    mov   byte [rbx+CH_POS] , 0
    ret


sweep_trigger:

    movzx eax , byte [gb_io+0x14]
    and   eax , 7
    shl   eax , 8
    or    al , [gb_io+0x13]
    mov   [ch1_shadow] , eax
    mov   byte [ch1_neg_used] , 0
    movzx eax , byte [gb_io+0x10]
    mov   edx , eax
    shr   edx , 4
    and   edx , 7                 ; Period
    and   eax , 7                 ; Shift
    mov   byte [ch1_sweep_en] , 0
    mov   ecx , edx
    or    ecx , eax
    jz    @f
    mov   byte [ch1_sweep_en] , 1
  @@:
    test  edx , edx
    jnz   @f
    mov   edx , 8
  @@:
    mov   [ch1_sweep_timer] , dl
    xor   ecx , ecx
    test  eax , eax
    jz    @f
    call  sweep_calc
  @@:
    ret


; NR52 write. In: al = value

apu_power:

    test  al , 0x80
    jnz   .on
    test  byte [gb_io+IO_NR52] , 0x80
    jz    .done
    push  rdi rcx                 ; Power off clears every sound register;
    mov   rdi , gb_io+0x10        ; length counters survive (DMG)
    mov   ecx , 0x17
    xor   eax , eax
    rep   stosb
    xor   ecx , ecx
  @@:
    mov   word [apu_ch+rcx+CH_ON] , 0     ; CH_ON and CH_DAC
    add   ecx , CH_SIZE
    cmp   ecx , CH_SIZE*4
    jb    @b
    mov   byte [ch1_sweep_en] , 0
    pop   rcx rdi
    ret
  .on:
    test  byte [gb_io+IO_NR52] , 0x80
    jnz   .done
    mov   byte [gb_io+IO_NR52] , 0x80
    mov   byte [fs_step] , 0
    mov   byte [apu_ch+CH_SIZE*0+CH_POS] , 0
    mov   byte [apu_ch+CH_SIZE*1+CH_POS] , 0
  .done:
    ret


; Called before DIV is reset: a falling DIV bit 12 clocks the frame
; sequencer. Clobbers rax, rdx.

apu_div_reset:

    call  apu_sync
    test  byte [div_counter+1] , 0x10
    jz    @f
    test  byte [gb_io+IO_NR52] , 0x80
    jz    @f
    push  rbx rcx rsi
    call  apu_frame_seq
    pop   rsi rcx rbx
  @@:
    ret


duty_table:     db  0x80 , 0x81 , 0xE1 , 0x7E   ; Bit n = duty step n high

; Post-boot values of FF10-FF26
apu_boot_regs:  db  0x80 , 0xBF , 0xF3 , 0xFF , 0xBF
                db  0xFF , 0x3F , 0x00 , 0xFF , 0xBF
                db  0x7F , 0xFF , 0x9F , 0xFF , 0xBF
                db  0xFF , 0xFF , 0x00 , 0x00 , 0xBF
                db  0x77 , 0xF3 , 0xF1

; Bits that read as 1 in FF10-FF2F
apu_read_mask:  db  0x80 , 0x3F , 0x00 , 0xFF , 0xBF
                db  0xFF , 0x3F , 0x00 , 0xFF , 0xBF
                db  0x7F , 0xFF , 0x9F , 0xFF , 0xBF
                db  0xFF , 0xFF , 0x00 , 0x00 , 0xBF
                db  0x00 , 0x00 , 0x70
                times 9 db 0xFF

; FF10-FF25 -> channel shl 4 + register
apu_reg_slot:   db  0x00 , 0x01 , 0x02 , 0x03 , 0x04
                db  0x10 , 0x11 , 0x12 , 0x13 , 0x14
                db  0x20 , 0x21 , 0x22 , 0x23 , 0x24
                db  0x30 , 0x31 , 0x32 , 0x33 , 0x34
                db  0x40 , 0x40
