; Scanline renderer (DMG). Called when a line enters HBlank.
; Writes shades 0-3 for line LY into gb_lcd.

ppu_render_line:

    push  rbx r12 r13 r14 r15

    movzx r8d , byte [gb_io+IO_LY]
    cmp   r8d , 144
    jae   .done
    test  r8d , r8d
    jnz   @f
    mov   byte [win_line] , 0
  @@:
    imul  edi , r8d , 160
    add   rdi , gb_lcd
    movzx r9d , byte [gb_io+IO_LCDC]

    movzx eax , byte [gb_io+IO_BGP]
    mov   rsi , pal_bg
    call  decode_palette
    movzx eax , byte [gb_io+IO_OBP0]
    mov   rsi , pal_obj0
    call  decode_palette
    movzx eax , byte [gb_io+IO_OBP1]
    mov   rsi , pal_obj1
    call  decode_palette

    ; Background (LCDC bit 0 off: BG and window show color 0)
    test  r9d , 0x01
    jnz   .bg
    xor   ecx , ecx
    mov   al , [pal_bg]
  @@:
    mov   [rdi+rcx] , al
    mov   byte [bg_idx+rcx] , 0
    inc   ecx
    cmp   ecx , 160
    jb    @b
    jmp   .objects

  .bg:
    mov   r10d , 0x1800
    test  r9d , 0x08
    jz    @f
    mov   r10d , 0x1C00
  @@:
    movzx r11d , byte [gb_io+IO_SCY]
    add   r11d , r8d
    movzx r13d , byte [gb_io+IO_SCX]
    xor   ecx , ecx
    call  render_tiles

    ; Window
    test  r9d , 0x20
    jz    .objects
    movzx eax , byte [gb_io+IO_WY]
    cmp   r8d , eax
    jb    .objects
    movzx ecx , byte [gb_io+IO_WX]
    cmp   ecx , 166
    ja    .objects
    sub   ecx , 7                 ; First screen x (may be negative)
    xor   r13d , r13d
    test  ecx , ecx
    jns   @f
    neg   ecx
    mov   r13d , ecx              ; Skip window columns left of the screen
    xor   ecx , ecx
  @@:
    mov   r10d , 0x1800
    test  r9d , 0x40
    jz    @f
    mov   r10d , 0x1C00
  @@:
    movzx r11d , byte [win_line]
    call  render_tiles
    inc   byte [win_line]

  .objects:
    test  r9d , 0x02
    jz    .done
    call  render_objects

  .done:
    pop   r15 r14 r13 r12 rbx
    ret


; In: al = palette register, rsi = 4-byte shade table to fill

decode_palette:

    mov   ecx , 4
  @@:
    mov   dl , al
    and   dl , 3
    mov   [rsi] , dl
    shr   al , 2
    inc   rsi
    dec   ecx
    jnz   @b
    ret


; Render BG or window tiles from screen x to the end of the line.
; In: ecx = first screen x, r13d = source x at that pixel,
;     r11d = source y, r10d = tile map offset in VRAM,
;     r9d = LCDC, rdi = LCD line

render_tiles:

  .pixel:
    cmp   ecx , 160
    jae   .done
    mov   eax , r13d
    and   eax , 0xFF
    mov   edx , r11d
    and   edx , 0xFF

    mov   esi , edx               ; Tile map entry
    shr   esi , 3
    shl   esi , 5
    mov   ebx , eax
    shr   ebx , 3
    add   esi , ebx
    add   esi , r10d
    movzx ebx , byte [gb_vram+rsi]
    test  r9d , 0x10
    jnz   @f
    movsx ebx , bl                ; 8800 mode: signed index from 9000
    add   ebx , 0x100
  @@:
    shl   ebx , 4
    and   edx , 7
    lea   ebx , [ebx+edx*2]
    movzx esi , byte [gb_vram+rbx]
    movzx ebx , byte [gb_vram+rbx+1]

    and   eax , 7
    xor   eax , 7                 ; Bit 7 is the leftmost pixel
    xor   edx , edx
    bt    esi , eax
    adc   edx , 0
    bt    ebx , eax
    jnc   @f
    or    edx , 2
  @@:
    mov   [bg_idx+rcx] , dl
    mov   al , [pal_bg+rdx]
    mov   [rdi+rcx] , al

    inc   ecx
    inc   r13d
    jmp   .pixel
  .done:
    ret


; Objects: up to 10 per line in OAM order, DMG priority is lower X
; first, then lower OAM index.

render_objects:

    mov   r12d , 8
    test  r9d , 0x04
    jz    @f
    mov   r12d , 16
  @@:

    ; Select objects on this line
    xor   r14d , r14d             ; Count
    xor   ecx , ecx               ; OAM index
  .scan:
    movzx eax , byte [gb_oam+rcx*4]
    sub   eax , 16
    mov   edx , r8d
    sub   edx , eax               ; Row within object
    cmp   edx , r12d
    jae   .next                   ; Also catches negative (unsigned)
    movzx eax , byte [gb_oam+rcx*4+1]
    shl   eax , 8
    or    eax , ecx               ; Sort key: X, then index
    mov   [obj_keys+r14*4] , eax
    inc   r14d
    cmp   r14d , 10
    je    .sort
  .next:
    inc   ecx
    cmp   ecx , 40
    jb    .scan

  .sort:                          ; Insertion sort, ascending key
    mov   ecx , 1
  .sort_outer:
    cmp   ecx , r14d
    jae   .draw
    mov   eax , [obj_keys+rcx*4]
    mov   edx , ecx
  .sort_inner:
    test  edx , edx
    jz    .sort_place
    mov   ebx , [obj_keys+rdx*4-4]
    cmp   ebx , eax
    jbe   .sort_place
    mov   [obj_keys+rdx*4] , ebx
    dec   edx
    jmp   .sort_inner
  .sort_place:
    mov   [obj_keys+rdx*4] , eax
    inc   ecx
    jmp   .sort_outer

  .draw:
    xor   eax , eax               ; Clear object line
    mov   ecx , 160
  @@:
    mov   [obj_line+rcx-1] , al
    dec   ecx
    jnz   @b

    xor   r15d , r15d             ; Highest priority first
  .object:
    cmp   r15d , r14d
    jae   .merge
    mov   ecx , [obj_keys+r15*4]
    and   ecx , 0xFF
    lea   rsi , [gb_oam+rcx*4]

    movzx eax , byte [rsi]
    sub   eax , 16
    mov   edx , r8d
    sub   edx , eax               ; Row
    movzx r10d , byte [rsi+3]     ; Attributes
    test  r10d , 0x40             ; Y flip
    jz    @f
    mov   eax , r12d
    dec   eax
    sub   eax , edx
    mov   edx , eax
  @@:
    movzx ebx , byte [rsi+2]      ; Tile
    cmp   r12d , 16
    jne   @f
    and   ebx , 0xFE
  @@:
    shl   ebx , 4
    lea   ebx , [ebx+edx*2]
    movzx r11d , byte [gb_vram+rbx]   ; Low bits
    movzx r13d , byte [gb_vram+rbx+1] ; High bits

    movzx ecx , byte [rsi+1]
    sub   ecx , 8                 ; Screen x of leftmost pixel
    xor   ebx , ebx               ; Pixel 0-7
  .obj_pixel:
    lea   eax , [ecx+ebx]
    cmp   eax , 160
    jae   .obj_next_pixel         ; Off screen (also negative)
    cmp   byte [obj_line+rax] , 0
    jne   .obj_next_pixel         ; Higher priority object already there
    mov   edx , ebx
    test  r10d , 0x20             ; X flip
    jnz   @f
    xor   edx , 7
  @@:
    push  rcx
    xor   ecx , ecx
    bt    r11d , edx
    adc   ecx , 0
    bt    r13d , edx
    jnc   @f
    or    ecx , 2
  @@:
    test  ecx , ecx
    jz    .obj_transparent
    test  r10d , 0x10             ; Palette
    jz    @f
    or    ecx , 4
  @@:
    test  r10d , 0x80             ; Behind BG colors 1-3
    jz    @f
    or    ecx , 8
  @@:
    or    ecx , 0x80
    mov   [obj_line+rax] , cl
  .obj_transparent:
    pop   rcx
  .obj_next_pixel:
    inc   ebx
    cmp   ebx , 8
    jb    .obj_pixel
    inc   r15d
    jmp   .object

  .merge:
    xor   ecx , ecx
  .merge_pixel:
    movzx eax , byte [obj_line+rcx]
    test  eax , eax
    jz    .merge_next
    test  eax , 8
    jz    @f
    cmp   byte [bg_idx+rcx] , 0
    jne   .merge_next
  @@:
    mov   edx , eax
    and   edx , 3
    test  eax , 4
    jz    @f
    add   edx , 4                 ; pal_obj1 follows pal_obj0
  @@:
    mov   al , [pal_obj0+rdx]
    mov   [rdi+rcx] , al
  .merge_next:
    inc   ecx
    cmp   ecx , 160
    jb    .merge_pixel
    ret
