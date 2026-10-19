; SM83 (GB CPU) interpreter.
;
; Opcode handlers are called with ebx = opcode. They may clobber
; rax, rcx, rdx, rsi, rdi and add extra cycles to [op_cycles].
;
; Each memory access (cpu_read / cpu_write / cpu_idle) is preceded by
; 4 T-cycles, which the hardware catches up with lazily (hw_sync);
; cycles left over when an instruction ends are counted then.
;
; 8-bit register index (opcode bits): 0 B, 1 C, 2 D, 3 E, 4 H, 5 L, 6 (HL), 7 A
; 16-bit register index (opcode bits 4-5): 0 BC, 1 DE, 2 HL, 3 SP (AF for push/pop)


; Execute one instruction or interrupt dispatch.
; Out: eax = T-cycles

cpu_step:

    mov   dword [cpu_ticked] , 0
    movzx eax , byte [gb_ie]
    and   al , [gb_io+IO_IF]
    and   eax , 0x1F
    jz    .no_irq
    mov   byte [cpu_halted] , 0
    cmp   byte [cpu_ime] , 0
    je    .no_irq

    mov   byte [cpu_ime] , 0
    mov   byte [cpu_ei_pending] , 0
    mov   dword [op_cycles] , 20
    call  cpu_idle                ; 2 internal cycles
    call  cpu_idle
    dec   word [cpu_sp]
    movzx edx , word [cpu_sp]
    mov   al , byte [cpu_pc+1]
    call  cpu_write
    movzx eax , byte [gb_ie]      ; The PC high byte push may have hit IE
    and   al , [gb_io+IO_IF]
    and   eax , 0x1F
    jz    @f                      ; Cancelled: jump to 0000
    bsf   ecx , eax               ; Highest priority = lowest bit
    btr   dword [gb_io+IO_IF] , ecx
    lea   eax , [0x40+rcx*8]
  @@:
    mov   ecx , eax
    dec   word [cpu_sp]
    movzx edx , word [cpu_sp]
    mov   al , byte [cpu_pc]
    call  cpu_write
    mov   [cpu_pc] , cx
    jmp   .finish

  .no_irq:
    mov   dword [op_cycles] , 4
    cmp   byte [cpu_halted] , 0
    jne   .halted

    mov   al , [cpu_ei_pending]
    mov   [cpu_ei_now] , al
    mov   byte [cpu_ei_pending] , 0

    call  fetch8
    cmp   byte [cpu_halt_bug] , 0
    je    @f
    mov   byte [cpu_halt_bug] , 0
    dec   word [cpu_pc]           ; HALT bug: this byte is read again
  @@:
    mov   ebx , eax
    movzx ecx , byte [cycle_table+rbx]
    mov   [op_cycles] , ecx
    call  qword [op_table+rbx*8]

    cmp   byte [cpu_ei_now] , 0
    je    .finish
    mov   byte [cpu_ime] , 1
    jmp   .finish

  .halted:                        ; Nothing can wake the CPU before the next
    mov   eax , [sync_left]       ; hardware event: skip to it (or to the
    cmp   eax , [frame_left]      ; end of the frame)
    cmova eax , [frame_left]
    cmp   eax , 4
    jbe   .finish
    mov   [op_cycles] , eax

  .finish:
    mov   eax , [op_cycles]
    sub   eax , [cpu_ticked]
    jbe   @f
    add   [sync_pending] , eax
    sub   [sync_left] , eax
    jg    @f
    call  hw_sync
  @@:
    mov   eax , [op_cycles]
    ret


; Bus access with timing. Same interface as mem_read / mem_write.

cpu_read:

    call  cpu_idle
    jmp   mem_read

cpu_write:

    call  cpu_idle
    jmp   mem_write

cpu_idle:                         ; Advance 4 T-cycles. Preserves all but flags

    add   dword [cpu_ticked] , 4
    add   dword [sync_pending] , 4
    sub   dword [sync_left] , 4
    jle   hw_sync
    ret


; Helpers. All clobber rax and rdx unless noted.

fetch8:                           ; Out: eax = byte at PC, PC+1

    movzx edx , word [cpu_pc]
    call  cpu_read
    inc   word [cpu_pc]
    ret

fetch16:                          ; Out: eax = word at PC, PC+2

    call  fetch8
    push  rax
    call  fetch8
    shl   eax , 8
    pop   rdx
    or    eax , edx
    ret

push16:                           ; In: ax = value. Includes the internal cycle

    call  cpu_idle
    sub   word [cpu_sp] , 2
    push  rax
    movzx edx , word [cpu_sp]
    inc   edx
    and   edx , 0xFFFF
    mov   al , ah
    call  cpu_write
    pop   rax
    movzx edx , word [cpu_sp]
    jmp   cpu_write

pop16:                            ; Out: eax = value

    movzx edx , word [cpu_sp]
    call  cpu_read
    push  rax
    inc   edx
    and   edx , 0xFFFF
    call  cpu_read
    shl   eax , 8
    pop   rdx
    or    eax , edx
    add   word [cpu_sp] , 2
    ret

read_r8:                          ; In: ecx = index   Out: eax   Preserves rcx

    cmp   ecx , 6
    je    .hl
    mov   rax , [r8_addr+rcx*8]
    movzx eax , byte [rax]
    ret
  .hl:
    movzx edx , word [cpu_l]
    jmp   cpu_read

write_r8:                         ; In: ecx = index, al = value

    cmp   ecx , 6
    je    .hl
    mov   rdx , [r8_addr+rcx*8]
    mov   [rdx] , al
    ret
  .hl:
    movzx edx , word [cpu_l]
    jmp   cpu_write

check_cond:                       ; In: ebx = opcode   Out: eax = 1 if taken

    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 3                 ; 0 NZ, 1 Z, 2 NC, 3 C
    movzx edx , byte [cpu_f]
    mov   eax , 0x80
    test  ecx , 2
    jz    @f
    mov   eax , 0x10
  @@:
    and   eax , edx
    setnz al
    movzx eax , al
    test  ecx , 1
    jnz   @f
    xor   eax , 1
  @@:
    ret


; 8-bit loads

op_ld_r_r:

    mov   ecx , ebx
    and   ecx , 7
    call  read_r8
    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 7
    jmp   write_r8

op_ld_r_n:

    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 7
    call  fetch8
    jmp   write_r8

op_ld_mbc_a:

    movzx edx , word [cpu_c]
    mov   al , [cpu_a]
    jmp   cpu_write

op_ld_mde_a:

    movzx edx , word [cpu_e]
    mov   al , [cpu_a]
    jmp   cpu_write

op_ld_mhli_a:

    movzx edx , word [cpu_l]
    inc   word [cpu_l]
    mov   al , [cpu_a]
    jmp   cpu_write

op_ld_mhld_a:

    movzx edx , word [cpu_l]
    dec   word [cpu_l]
    mov   al , [cpu_a]
    jmp   cpu_write

op_ld_a_mbc:

    movzx edx , word [cpu_c]
    call  cpu_read
    mov   [cpu_a] , al
    ret

op_ld_a_mde:

    movzx edx , word [cpu_e]
    call  cpu_read
    mov   [cpu_a] , al
    ret

op_ld_a_mhli:

    movzx edx , word [cpu_l]
    inc   word [cpu_l]
    call  cpu_read
    mov   [cpu_a] , al
    ret

op_ld_a_mhld:

    movzx edx , word [cpu_l]
    dec   word [cpu_l]
    call  cpu_read
    mov   [cpu_a] , al
    ret

op_ldh_mn_a:

    call  fetch8
    lea   edx , [eax+0xFF00]
    mov   al , [cpu_a]
    jmp   cpu_write

op_ldh_a_mn:

    call  fetch8
    lea   edx , [eax+0xFF00]
    call  cpu_read
    mov   [cpu_a] , al
    ret

op_ld_mc_a:

    movzx edx , byte [cpu_c]
    add   edx , 0xFF00
    mov   al , [cpu_a]
    jmp   cpu_write

op_ld_a_mc:

    movzx edx , byte [cpu_c]
    add   edx , 0xFF00
    call  cpu_read
    mov   [cpu_a] , al
    ret

op_ld_mnn_a:

    call  fetch16
    mov   edx , eax
    mov   al , [cpu_a]
    jmp   cpu_write

op_ld_a_mnn:

    call  fetch16
    mov   edx , eax
    call  cpu_read
    mov   [cpu_a] , al
    ret


; 16-bit loads and arithmetic

op_ld_rr_nn:

    call  fetch16
    mov   ecx , ebx
    shr   ecx , 4
    mov   rdx , [rr_addr+rcx*8]
    mov   [rdx] , ax
    ret

op_ld_mnn_sp:

    call  fetch16
    mov   edx , eax
    push  rdx
    mov   al , byte [cpu_sp]
    call  cpu_write
    pop   rdx
    inc   edx
    and   edx , 0xFFFF
    mov   al , byte [cpu_sp+1]
    jmp   cpu_write

op_ld_sp_hl:

    mov   ax , word [cpu_l]
    mov   [cpu_sp] , ax
    ret

op_push:

    mov   ecx , ebx
    shr   ecx , 4
    and   ecx , 3
    mov   rdx , [rr_stack_addr+rcx*8]
    movzx eax , word [rdx]
    jmp   push16

op_pop:

    call  pop16
    mov   ecx , ebx
    shr   ecx , 4
    and   ecx , 3
    cmp   ecx , 3
    jne   @f
    and   al , 0xF0               ; Low nibble of F is always 0
  @@:
    mov   rdx , [rr_stack_addr+rcx*8]
    mov   [rdx] , ax
    ret

op_inc_rr:

    mov   ecx , ebx
    shr   ecx , 4
    mov   rdx , [rr_addr+rcx*8]
    inc   word [rdx]
    ret

op_dec_rr:

    mov   ecx , ebx
    shr   ecx , 4
    mov   rdx , [rr_addr+rcx*8]
    dec   word [rdx]
    ret

op_add_hl_rr:

    mov   ecx , ebx
    shr   ecx , 4
    mov   rdx , [rr_addr+rcx*8]
    movzx ecx , word [rdx]
    movzx eax , word [cpu_l]
    mov   esi , eax
    and   esi , 0x0FFF
    mov   edi , ecx
    and   edi , 0x0FFF
    add   esi , edi
    add   eax , ecx
    mov   word [cpu_l] , ax
    mov   dl , [cpu_f]
    and   dl , 0x80
    cmp   esi , 0x0FFF
    jbe   @f
    or    dl , 0x20
  @@:
    cmp   eax , 0xFFFF
    jbe   @f
    or    dl , 0x10
  @@:
    mov   [cpu_f] , dl
    ret

sp_plus_e:                        ; In: al = e8   Out: eax = SP+e8, F set

    movzx edx , word [cpu_sp]
    push  rax
    add   al , dl                 ; H and C come from the low byte
    lahf
    movzx eax , ah
    mov   al , [flag_table+rax]
    and   al , 0x30
    mov   [cpu_f] , al
    pop   rax
    movsx eax , al
    add   eax , edx
    and   eax , 0xFFFF
    ret

op_add_sp_e:

    call  fetch8
    call  sp_plus_e
    mov   [cpu_sp] , ax
    ret

op_ld_hl_sp_e:

    call  fetch8
    call  sp_plus_e
    mov   word [cpu_l] , ax
    ret


; 8-bit arithmetic and logic. ALU routines take the operand in al.

op_alu_r:

    mov   ecx , ebx
    and   ecx , 7
    call  read_r8
    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 7
    jmp   qword [alu_table+rcx*8]

op_alu_n:

    call  fetch8
    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 7
    jmp   qword [alu_table+rcx*8]

alu_add:

    mov   cl , al
    mov   al , [cpu_a]
    add   al , cl
alu_store:                        ; Store al to A, F from x86 flags, N=0
    lahf
    mov   [cpu_a] , al
    movzx eax , ah
    mov   al , [flag_table+rax]
    mov   [cpu_f] , al
    ret

alu_adc:

    mov   cl , al
    movzx edx , byte [cpu_f]
    mov   al , [cpu_a]
    bt    edx , 4
    adc   al , cl
    jmp   alu_store

alu_sub:

    mov   cl , al
    mov   al , [cpu_a]
    sub   al , cl
alu_store_n:                      ; Store al to A, F from x86 flags, N=1
    lahf
    mov   [cpu_a] , al
    movzx eax , ah
    mov   al , [flag_table+rax]
    or    al , 0x40
    mov   [cpu_f] , al
    ret

alu_sbc:

    mov   cl , al
    movzx edx , byte [cpu_f]
    mov   al , [cpu_a]
    bt    edx , 4
    sbb   al , cl
    jmp   alu_store_n

alu_and:

    and   al , [cpu_a]
    mov   [cpu_a] , al
    mov   dl , 0x20
    jmp   alu_logic_flags

alu_xor:

    xor   al , [cpu_a]
    mov   [cpu_a] , al
    xor   dl , dl
    jmp   alu_logic_flags

alu_or:

    or    al , [cpu_a]
    mov   [cpu_a] , al
    xor   dl , dl
alu_logic_flags:                  ; In: al = result, dl = H flag
    test  al , al
    jnz   @f
    or    dl , 0x80
  @@:
    mov   [cpu_f] , dl
    ret

alu_cp:

    mov   cl , al
    mov   al , [cpu_a]
    sub   al , cl
    lahf
    movzx eax , ah
    mov   al , [flag_table+rax]
    or    al , 0x40
    mov   [cpu_f] , al
    ret

op_inc_r:

    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 7
    call  read_r8
    inc   al
    mov   dl , [cpu_f]
    and   dl , 0x10               ; C unchanged
    test  al , 0x0F               ; H: carry from bit 3
    jnz   @f
    or    dl , 0x20
  @@:
    test  al , al
    jnz   @f
    or    dl , 0x80
  @@:
    mov   [cpu_f] , dl
    jmp   write_r8

op_dec_r:

    mov   ecx , ebx
    shr   ecx , 3
    and   ecx , 7
    call  read_r8
    dec   al
    mov   dl , [cpu_f]
    and   dl , 0x10               ; C unchanged
    or    dl , 0x40
    mov   ah , al
    and   ah , 0x0F               ; H: borrow from bit 4
    cmp   ah , 0x0F
    jne   @f
    or    dl , 0x20
  @@:
    test  al , al
    jnz   @f
    or    dl , 0x80
  @@:
    mov   [cpu_f] , dl
    jmp   write_r8

op_daa:

    movzx eax , byte [cpu_a]
    movzx ecx , byte [cpu_f]
    test  ecx , 0x40
    jnz   .sub
    test  ecx , 0x10
    jnz   .add_hi
    cmp   eax , 0x99
    jbe   .check_lo
  .add_hi:
    add   eax , 0x60
    or    ecx , 0x10
  .check_lo:
    test  ecx , 0x20
    jnz   .add_lo
    mov   edx , eax
    and   edx , 0x0F
    cmp   edx , 9
    jbe   .done
  .add_lo:
    add   eax , 6
    jmp   .done
  .sub:
    test  ecx , 0x10
    jz    @f
    sub   eax , 0x60
  @@:
    test  ecx , 0x20
    jz    .done
    sub   eax , 6
  .done:
    and   ecx , 0x50              ; Keep N and C, clear Z and H
    test  al , al
    jnz   @f
    or    ecx , 0x80
  @@:
    mov   [cpu_a] , al
    mov   [cpu_f] , cl
    ret

op_cpl:

    not   byte [cpu_a]
    or    byte [cpu_f] , 0x60
    ret

op_scf:

    mov   al , [cpu_f]
    and   al , 0x80
    or    al , 0x10
    mov   [cpu_f] , al
    ret

op_ccf:

    mov   al , [cpu_f]
    and   al , 0x90
    xor   al , 0x10
    mov   [cpu_f] , al
    ret


; Rotates on A (Z always cleared)

op_rlca:

    mov   al , [cpu_a]
    rol   al , 1
    jmp   rot_a_store

op_rrca:

    mov   al , [cpu_a]
    ror   al , 1
    jmp   rot_a_store

op_rla:

    movzx edx , byte [cpu_f]
    mov   al , [cpu_a]
    bt    edx , 4
    rcl   al , 1
    jmp   rot_a_store

op_rra:

    movzx edx , byte [cpu_f]
    mov   al , [cpu_a]
    bt    edx , 4
    rcr   al , 1
rot_a_store:
    setc  dl
    shl   dl , 4
    mov   [cpu_a] , al
    mov   [cpu_f] , dl
    ret


; Jumps, calls and returns

op_jp:

    call  fetch16
    mov   [cpu_pc] , ax
    ret

op_jp_hl:

    mov   ax , word [cpu_l]
    mov   [cpu_pc] , ax
    ret

op_jp_cc:

    call  fetch16
    mov   esi , eax
    call  check_cond
    test  eax , eax
    jz    @f
    mov   [cpu_pc] , si
    add   dword [op_cycles] , 4
  @@:
    ret

op_jr:

    call  fetch8
    movsx eax , al
    add   [cpu_pc] , ax
    ret

op_jr_cc:

    call  fetch8
    movsx esi , al
    call  check_cond
    test  eax , eax
    jz    @f
    add   [cpu_pc] , si
    add   dword [op_cycles] , 4
  @@:
    ret

op_call:

    call  fetch16
    mov   esi , eax
    movzx eax , word [cpu_pc]
    call  push16
    mov   [cpu_pc] , si
    ret

op_call_cc:

    call  fetch16
    mov   esi , eax
    call  check_cond
    test  eax , eax
    jz    @f
    movzx eax , word [cpu_pc]
    call  push16
    mov   [cpu_pc] , si
    add   dword [op_cycles] , 12
  @@:
    ret

op_ret:

    call  pop16
    mov   [cpu_pc] , ax
    ret

op_reti:

    call  pop16
    mov   [cpu_pc] , ax
    mov   byte [cpu_ime] , 1
    ret

op_ret_cc:

    call  cpu_idle                ; Condition check
    call  check_cond
    test  eax , eax
    jz    @f
    call  pop16
    mov   [cpu_pc] , ax
    add   dword [op_cycles] , 12
  @@:
    ret

op_rst:

    movzx eax , word [cpu_pc]
    call  push16
    mov   eax , ebx
    and   eax , 0x38
    mov   [cpu_pc] , ax
    ret


; Control

op_nop:

    ret

op_stop:

    call  fetch8                  ; STOP is followed by one ignored byte
    ret

op_halt:                          ; With IME=0 and an interrupt pending, HALT
                                  ; does not halt and PC fails to advance once
    mov   al , [cpu_ime]
    or    al , [cpu_ei_now]
    jnz   .halt
    movzx eax , byte [gb_ie]
    and   al , [gb_io+IO_IF]
    and   eax , 0x1F
    jz    .halt
    mov   byte [cpu_halt_bug] , 1
    ret
  .halt:
    mov   byte [cpu_halted] , 1
    ret

op_di:

    mov   byte [cpu_ime] , 0
    mov   byte [cpu_ei_pending] , 0
    mov   byte [cpu_ei_now] , 0
    ret

op_ei:

    mov   byte [cpu_ei_pending] , 1
    ret

op_illegal:                       ; Locks up real hardware; treated as NOP

    ret


; CB prefix

op_cb:

    call  fetch8
    mov   ebx , eax
    mov   ecx , ebx
    and   ecx , 7
    add   dword [op_cycles] , 4
    cmp   ecx , 6
    jne   .read
    mov   eax , 8                 ; (HL): BIT +4, others +8
    mov   edx , ebx
    shr   edx , 6
    cmp   edx , 1
    jne   @f
    mov   eax , 4
  @@:
    add   [op_cycles] , eax

  .read:
    call  read_r8
    mov   edx , ebx
    shr   edx , 6
    jz    .shift
    mov   esi , ebx
    shr   esi , 3
    and   esi , 7
    cmp   edx , 1
    je    .bit
    cmp   edx , 2
    je    .res
    bts   eax , esi
    jmp   write_r8
  .res:
    btr   eax , esi
    jmp   write_r8
  .bit:
    bt    eax , esi
    mov   al , [cpu_f]
    setc  dl
    and   al , 0x10
    or    al , 0x20
    test  dl , dl
    jnz   @f
    or    al , 0x80
  @@:
    mov   [cpu_f] , al
    ret

  .shift:
    mov   edx , ebx
    shr   edx , 3
    and   edx , 7
    jmp   qword [cb_shift_table+rdx*8]

cb_rlc:

    rol   al , 1
    setc  dl
    jmp   cb_store

cb_rrc:

    ror   al , 1
    setc  dl
    jmp   cb_store

cb_rl:

    bt    word [cpu_f] , 4
    rcl   al , 1
    setc  dl
    jmp   cb_store

cb_rr:

    bt    word [cpu_f] , 4
    rcr   al , 1
    setc  dl
    jmp   cb_store

cb_sla:

    shl   al , 1
    setc  dl
    jmp   cb_store

cb_sra:

    sar   al , 1
    setc  dl
    jmp   cb_store

cb_swap:

    rol   al , 4
    xor   dl , dl
    jmp   cb_store

cb_srl:

    shr   al , 1
    setc  dl
cb_store:                         ; In: al = result, dl = carry (0/1), ecx = index
    shl   dl , 4
    test  al , al
    jnz   @f
    or    dl , 0x80
  @@:
    mov   [cpu_f] , dl
    jmp   write_r8


; Tables

r8_addr:        dq  cpu_b , cpu_c , cpu_d , cpu_e , cpu_h , cpu_l , 0 , cpu_a
rr_addr:        dq  cpu_c , cpu_e , cpu_l , cpu_sp
rr_stack_addr:  dq  cpu_c , cpu_e , cpu_l , cpu_f

alu_table:      dq  alu_add , alu_adc , alu_sub , alu_sbc
                dq  alu_and , alu_xor , alu_or  , alu_cp

cb_shift_table: dq  cb_rlc , cb_rrc , cb_rl  , cb_rr
                dq  cb_sla , cb_sra , cb_swap , cb_srl

; x86 LAHF result (SF ZF - AF - PF - CF) -> GB flags Z H C
flag_table:
rept 256 x:0 { db ((((x) shr 6) and 1) shl 7) or ((((x) shr 4) and 1) shl 5) or (((x) and 1) shl 4) }

; Base T-cycles. Conditional jumps/calls/returns add the taken
; penalty in their handlers; CB adds its own cycles.
cycle_table:
    db  4,12, 8, 8, 4, 4, 8, 4,20, 8, 8, 8, 4, 4, 8, 4   ; 0x
    db  4,12, 8, 8, 4, 4, 8, 4,12, 8, 8, 8, 4, 4, 8, 4   ; 1x
    db  8,12, 8, 8, 4, 4, 8, 4, 8, 8, 8, 8, 4, 4, 8, 4   ; 2x
    db  8,12, 8, 8,12,12,12, 4, 8, 8, 8, 8, 4, 4, 8, 4   ; 3x
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; 4x
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; 5x
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; 6x
    db  8, 8, 8, 8, 8, 8, 4, 8, 4, 4, 4, 4, 4, 4, 8, 4   ; 7x
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; 8x
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; 9x
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; Ax
    db  4, 4, 4, 4, 4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 8, 4   ; Bx
    db  8,12,12,16,12,16, 8,16, 8,16,12, 4,12,24, 8,16   ; Cx
    db  8,12,12, 4,12,16, 8,16, 8,16,12, 4,12, 4, 8,16   ; Dx
    db 12,12, 8, 4, 4,16, 8,16,16, 4,16, 4, 4, 4, 8,16   ; Ex
    db 12,12, 8, 4, 4,16, 8,16,12, 8,16, 4, 4, 4, 8,16   ; Fx

op_table:
    ; 0x
    dq  op_nop      , op_ld_rr_nn , op_ld_mbc_a  , op_inc_rr , op_inc_r , op_dec_r , op_ld_r_n , op_rlca
    dq  op_ld_mnn_sp, op_add_hl_rr, op_ld_a_mbc  , op_dec_rr , op_inc_r , op_dec_r , op_ld_r_n , op_rrca
    ; 1x
    dq  op_stop     , op_ld_rr_nn , op_ld_mde_a  , op_inc_rr , op_inc_r , op_dec_r , op_ld_r_n , op_rla
    dq  op_jr       , op_add_hl_rr, op_ld_a_mde  , op_dec_rr , op_inc_r , op_dec_r , op_ld_r_n , op_rra
    ; 2x
    dq  op_jr_cc    , op_ld_rr_nn , op_ld_mhli_a , op_inc_rr , op_inc_r , op_dec_r , op_ld_r_n , op_daa
    dq  op_jr_cc    , op_add_hl_rr, op_ld_a_mhli , op_dec_rr , op_inc_r , op_dec_r , op_ld_r_n , op_cpl
    ; 3x
    dq  op_jr_cc    , op_ld_rr_nn , op_ld_mhld_a , op_inc_rr , op_inc_r , op_dec_r , op_ld_r_n , op_scf
    dq  op_jr_cc    , op_add_hl_rr, op_ld_a_mhld , op_dec_rr , op_inc_r , op_dec_r , op_ld_r_n , op_ccf
    ; 4x - 7x
    times 0x36 dq op_ld_r_r
    dq  op_halt
    times 0x09 dq op_ld_r_r
    ; 8x - Bx
    times 0x40 dq op_alu_r
    ; Cx
    dq  op_ret_cc   , op_pop      , op_jp_cc     , op_jp      , op_call_cc , op_push  , op_alu_n , op_rst
    dq  op_ret_cc   , op_ret      , op_jp_cc     , op_cb      , op_call_cc , op_call  , op_alu_n , op_rst
    ; Dx
    dq  op_ret_cc   , op_pop      , op_jp_cc     , op_illegal , op_call_cc , op_push  , op_alu_n , op_rst
    dq  op_ret_cc   , op_reti     , op_jp_cc     , op_illegal , op_call_cc , op_illegal , op_alu_n , op_rst
    ; Ex
    dq  op_ldh_mn_a , op_pop      , op_ld_mc_a   , op_illegal , op_illegal , op_push  , op_alu_n , op_rst
    dq  op_add_sp_e , op_jp_hl    , op_ld_mnn_a  , op_illegal , op_illegal , op_illegal , op_alu_n , op_rst
    ; Fx
    dq  op_ldh_a_mn , op_pop      , op_ld_a_mc   , op_di      , op_illegal , op_push  , op_alu_n , op_rst
    dq  op_ld_hl_sp_e , op_ld_sp_hl , op_ld_a_mnn , op_ei     , op_illegal , op_illegal , op_alu_n , op_rst

assert $ - op_table = 256*8
