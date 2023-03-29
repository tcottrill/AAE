; For assembly by NASM only
bits 32
;
; m6502 - V1.6 - Copyright 1998, Neil Bradley (neil@synthcom.com)
;
;

; Using stack calling conventions
; Non-zero page version (all zero page accesses through handlers)


		section	.data

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

		global	_m6502pc

		global	m6502pbBankSwitch_, _m6502pbBankSwitch

_m6502contextBegin:

; DO NOT CHANGE THE ORDER OF THE FOLLOWING REGISTERS!

_m6502Base	dd	0	; Base address for 6502 stuff
_m6502MemRead	dd	0	; Offset of memory read structure array
_m6502MemWrite	dd	0	; Offset of memory write structure array
_m6502af	dw	0	; A register and flags
_m6502pc:
m6502pc	dw	0	; 6502 Program counter
_m6502x		db	0	; X
_m6502y		db	0	; Y
_m6502s		db	0	; s
_irqPending	db	0	; Non-zero if an IRQ is pending

_m6502contextEnd:


times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

_tempAddr	dd	0	; Temporary address storage
dwElapsedTicks	dd 0	; Elapsed ticks!
dwElapsedTicks2	dd 0	; Elapsed ticks second counter!
cyclesRemaining	dd	0	; # Of cycles remaining
_altFlags	db	0	; Storage for I, D, and B

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

bit6502tox86:
		db	00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h, 00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h
		db	00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h, 00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h
		db	00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h, 00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h
		db	00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h, 00h, 01h, 40h, 41h, 02h, 03h, 42h, 43h
		db	10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h, 10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h
		db	10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h, 10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h
		db	10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h, 10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h
		db	10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h, 10h, 11h, 50h, 51h, 12h, 13h, 52h, 53h
		db	80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h, 80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h
		db	80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h, 80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h
		db	80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h, 80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h
		db	80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h, 80h, 81h, 0c0h, 0c1h, 82h, 83h, 0c2h, 0c3h
		db	90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h, 90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h
		db	90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h, 90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h
		db	90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h, 90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h
		db	90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h, 90h, 91h, 0d0h, 0d1h, 92h, 93h, 0d2h, 0d3h

bitx86to6502:
		db	00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h
		db	40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h
		db	00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h, 00h, 01h
		db	40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h, 40h, 41h
		db	02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h
		db	42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h
		db	02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h, 02h, 03h
		db	42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h, 42h, 43h
		db	80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h
		db	0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h
		db	80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h, 80h, 81h
		db	0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h, 0c0h, 0c1h
		db	82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h
		db	0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h
		db	82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h, 82h, 83h
		db	0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h, 0c2h, 0c3h

m6502regular:
		dd	RegInst00
		dd	RegInst01
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst05
		dd	RegInst06
		dd	invalidInsByte
		dd	RegInst08
		dd	RegInst09
		dd	RegInst0a
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst0d
		dd	RegInst0e
		dd	invalidInsByte
		dd	RegInst10
		dd	RegInst11
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst15
		dd	RegInst16
		dd	invalidInsByte
		dd	RegInst18
		dd	RegInst19
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst1d
		dd	RegInst1e
		dd	invalidInsByte
		dd	RegInst20
		dd	RegInst21
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst24
		dd	RegInst25
		dd	RegInst26
		dd	invalidInsByte
		dd	RegInst28
		dd	RegInst29
		dd	RegInst2a
		dd	invalidInsByte
		dd	RegInst2c
		dd	RegInst2d
		dd	RegInst2e
		dd	invalidInsByte
		dd	RegInst30
		dd	RegInst31
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst35
		dd	RegInst36
		dd	invalidInsByte
		dd	RegInst38
		dd	RegInst39
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst3d
		dd	RegInst3e
		dd	invalidInsByte
		dd	RegInst40
		dd	RegInst41
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst45
		dd	RegInst46
		dd	invalidInsByte
		dd	RegInst48
		dd	RegInst49
		dd	RegInst4a
		dd	invalidInsByte
		dd	RegInst4c
		dd	RegInst4d
		dd	RegInst4e
		dd	invalidInsByte
		dd	RegInst50
		dd	RegInst51
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst55
		dd	RegInst56
		dd	invalidInsByte
		dd	RegInst58
		dd	RegInst59
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst5d
		dd	RegInst5e
		dd	invalidInsByte
		dd	RegInst60
		dd	RegInst61
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst65
		dd	RegInst66
		dd	invalidInsByte
		dd	RegInst68
		dd	RegInst69
		dd	RegInst6a
		dd	invalidInsByte
		dd	RegInst6c
		dd	RegInst6d
		dd	RegInst6e
		dd	invalidInsByte
		dd	RegInst70
		dd	RegInst71
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst75
		dd	RegInst76
		dd	invalidInsByte
		dd	RegInst78
		dd	RegInst79
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst7d
		dd	RegInst7e
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst81
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst84
		dd	RegInst85
		dd	RegInst86
		dd	invalidInsByte
		dd	RegInst88
		dd	invalidInsByte
		dd	RegInst8a
		dd	invalidInsByte
		dd	RegInst8c
		dd	RegInst8d
		dd	RegInst8e
		dd	invalidInsByte
		dd	RegInst90
		dd	RegInst91
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst94
		dd	RegInst95
		dd	RegInst96
		dd	invalidInsByte
		dd	RegInst98
		dd	RegInst99
		dd	RegInst9a
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInst9d
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInsta0
		dd	RegInsta1
		dd	RegInsta2
		dd	invalidInsByte
		dd	RegInsta4
		dd	RegInsta5
		dd	RegInsta6
		dd	invalidInsByte
		dd	RegInsta8
		dd	RegInsta9
		dd	RegInstaa
		dd	invalidInsByte
		dd	RegInstac
		dd	RegInstad
		dd	RegInstae
		dd	invalidInsByte
		dd	RegInstb0
		dd	RegInstb1
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInstb4
		dd	RegInstb5
		dd	RegInstb6
		dd	invalidInsByte
		dd	RegInstb8
		dd	RegInstb9
		dd	RegInstba
		dd	invalidInsByte
		dd	RegInstbc
		dd	RegInstbd
		dd	RegInstbe
		dd	invalidInsByte
		dd	RegInstc0
		dd	RegInstc1
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInstc4
		dd	RegInstc5
		dd	RegInstc6
		dd	invalidInsByte
		dd	RegInstc8
		dd	RegInstc9
		dd	RegInstca
		dd	invalidInsByte
		dd	RegInstcc
		dd	RegInstcd
		dd	RegInstce
		dd	invalidInsByte
		dd	RegInstd0
		dd	RegInstd1
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInstd5
		dd	RegInstd6
		dd	invalidInsByte
		dd	RegInstd8
		dd	RegInstd9
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInstdd
		dd	RegInstde
		dd	invalidInsByte
		dd	RegInste0
		dd	RegInste1
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInste4
		dd	RegInste5
		dd	RegInste6
		dd	invalidInsByte
		dd	RegInste8
		dd	RegInste9
		dd	RegInstea
		dd	invalidInsByte
		dd	RegInstec
		dd	RegInsted
		dd	RegInstee
		dd	invalidInsByte
		dd	RegInstf0
		dd	RegInstf1
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInstf5
		dd	RegInstf6
		dd	invalidInsByte
		dd	RegInstf8
		dd	RegInstf9
		dd	invalidInsByte
		dd	invalidInsByte
		dd	invalidInsByte
		dd	RegInstfd
		dd	RegInstfe
		dd	invalidInsByte

		section	.text

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

; This is a generic read memory byte handler when a foreign
; handler is to be called

ReadMemoryByte:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Save our structure address
		and	edx, 0ffffh	; Only the lower 16 bits
		push	edx	; And our desired address
		call	dword [edi + 8]	; Go call our handler
		add	esp, 8	; Get the junk off the stack
		xor	ebx, ebx	; Zero X
		xor	ecx, ecx	; Zero Y
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		ret

;
; BRK
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst00:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		add	dword [dwElapsedTicks2], byte 7
		sub	esi, ebp	 ; Get our real program counter
		inc	si	; Move to the next position
		or	[_altFlags], byte 14h	; Indicate a break happened
		xor	edx, edx
		mov	dl, ah
		mov	ah, [bitx86to6502+edx]
		or		ah, [_altFlags]
		mov	dl, [_m6502s]	; Get our stack pointer
		add	dx, 0ffh		; Stack area is 100-1ffh
		mov	[edx+ebp], si	; Store our PC
		dec	dx		; Back up for flag storage
		mov	[edx+ebp], ah	; Store our flags
		dec	dx		; Back up for flag storage
		mov	[_m6502s], dl	; Store our new stack area
		xor	esi, esi	; Zero our program counter
		mov	si, [ebp+0fffeh]	; Get our break handler
		add	esi, ebp	; Add our base pointer back in
		xor	edx, edx
		mov	dl, ah
		mov	[_altFlags], dl
		and	[_altFlags], byte 3ch;
		mov	ah, [bit6502tox86+edx]
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst01:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop2:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead2
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr2		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine2

nextAddr2:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop2

callRoutine2:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit2

memoryRead2:
		mov	al, [ebp + edx]	; Get our data

readExit2:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop3:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead3
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr3		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine3

nextAddr3:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop3

callRoutine3:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit3

memoryRead3:
		mov	dh, [ebp + edx]	; Get our data

readExit3:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop4:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead4
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr4		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine4

nextAddr4:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop4

callRoutine4:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit4

memoryRead4:
		mov	dl, [ebp + edx]	; Get our data

readExit4:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst05:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop5:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead5
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr5		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine5

nextAddr5:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop5

callRoutine5:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit5

memoryRead5:
		mov	dl, [ebp + edx]	; Get our data

readExit5:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Asl
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst06:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop6:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead6
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr6		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine6

nextAddr6:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop6

callRoutine6:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit6

memoryRead6:
		mov	bh, [ebp + edx]	; Get our data

readExit6:
		pop	edx	; Restore our address
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shl	bh, 1	; Shift left by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop7:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite7	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr7	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine7	; If not, go call it!

nextAddr7:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop7

callRoutine7:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit7

memoryWrite7:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit7:
		xor	bh, bh	; Prevent us from being hosed later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; PHP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst08:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		xor	edx, edx
		mov	dl, ah
		mov	ah, [bitx86to6502+edx]
		or		ah, [_altFlags]
		inc	dh		; Stack page
		mov	dl, [_m6502s]	; Stack pointer
		mov	[ebp+edx], ah
		dec	byte [_m6502s]	; Decrement our stack pointer
		xor	edx, edx
		mov	dl, ah
		mov	[_altFlags], dl
		and	[_altFlags], byte 3ch;
		mov	ah, [bit6502tox86+edx]
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst09:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Asl
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst0a:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		sahf		; Restore flags
		mov	dl, ah	; Store our original flags
		shl	al, 1	; Shift left by 1
		lahf	; Load the flags back in
		and	dl, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, dl		; Or it into our flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst0d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop8:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead8
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr8		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine8

nextAddr8:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop8

callRoutine8:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit8

memoryRead8:
		mov	dl, [ebp + edx]	; Get our data

readExit8:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Asl
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst0e:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx	; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop9:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead9
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr9		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine9

nextAddr9:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop9

callRoutine9:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit9

memoryRead9:
		mov	bh, [ebp + edx]	; Get our data

readExit9:
		pop	edx	; Restore our address
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shl	bh, 1	; Shift left by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop10:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite10	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr10	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine10	; If not, go call it!

nextAddr10:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop10

callRoutine10:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit10

memoryWrite10:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit10:
		xor	bh, bh	; Prevent us from being hosed later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst10:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 80h	; Are we plus?
		jz		short takeJump10 ; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJump10:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst11:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop12:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead12
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr12		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine12

nextAddr12:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop12

callRoutine12:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit12

memoryRead12:
		mov	al, [ebp + edx]	; Get our data

readExit12:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop13:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead13
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr13		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine13

nextAddr13:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop13

callRoutine13:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit13

memoryRead13:
		mov	dh, [ebp + edx]	; Get our data

readExit13:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop14:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead14
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr14		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine14

nextAddr14:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop14

callRoutine14:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit14

memoryRead14:
		mov	dl, [ebp + edx]	; Get our data

readExit14:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst15:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop15:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead15
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr15		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine15

nextAddr15:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop15

callRoutine15:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit15

memoryRead15:
		mov	dl, [ebp + edx]	; Get our data

readExit15:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Asl
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst16:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		push	edx	; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop16:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead16
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr16		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine16

nextAddr16:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop16

callRoutine16:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit16

memoryRead16:
		mov	bh, [ebp + edx]	; Get our data

readExit16:
		pop	edx	; Restore our address
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shl	bh, 1	; Shift left by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop17:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite17	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr17	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine17	; If not, go call it!

nextAddr17:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop17

callRoutine17:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit17

memoryWrite17:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit17:
		xor	bh, bh	; Prevent us from being hosed later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CLC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst18:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		and	ah, 0feh	; No carry
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst19:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop18:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead18
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr18		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine18

nextAddr18:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop18

callRoutine18:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit18

memoryRead18:
		mov	dl, [ebp + edx]	; Get our data

readExit18:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Ora
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst1d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop19:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead19
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr19		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine19

nextAddr19:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop19

callRoutine19:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit19

memoryRead19:
		mov	dl, [ebp + edx]	; Get our data

readExit19:
		mov	dh, ah	; Get the flags
		or	al, dl		; OR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Asl
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst1e:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		add	dword [dwElapsedTicks2], byte 7
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		push	edx	; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop20:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead20
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr20		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine20

nextAddr20:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop20

callRoutine20:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit20

memoryRead20:
		mov	bh, [ebp + edx]	; Get our data

readExit20:
		pop	edx	; Restore our address
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shl	bh, 1	; Shift left by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop21:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite21	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr21	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine21	; If not, go call it!

nextAddr21:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop21

callRoutine21:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit21

memoryWrite21:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit21:
		xor	bh, bh	; Prevent us from being hosed later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; JSR
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst20:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		sub	esi, ebp	 ; Get our real program counter
		dec	si		; Our offset to return to
		sub	[_m6502s], byte 2	; Back up 2 byte for stack push
		mov	di, word [_m6502s]	; Our stack area
		and	edi, 0ffh	; Only the lower byte matters
		add	edi, 101h	; Make sure it's on the stack page
		mov	[edi+ebp], si	; Store our return address
		add	edx, ebp	; Our new address
		mov	esi, edx	; Put it in here for the fetch
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst21:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop23:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead23
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr23		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine23

nextAddr23:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop23

callRoutine23:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit23

memoryRead23:
		mov	al, [ebp + edx]	; Get our data

readExit23:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop24:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead24
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr24		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine24

nextAddr24:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop24

callRoutine24:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit24

memoryRead24:
		mov	dh, [ebp + edx]	; Get our data

readExit24:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop25:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead25
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr25		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine25

nextAddr25:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop25

callRoutine25:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit25

memoryRead25:
		mov	dl, [ebp + edx]	; Get our data

readExit25:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; BIT
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst24:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop26:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead26
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr26		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine26

nextAddr26:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop26

callRoutine26:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit26

memoryRead26:
		mov	dl, [ebp + edx]	; Get our data

readExit26:
		mov	dh, dl	; Save this, too
		and	ah, 2fh	; Kill sign, zero, and overflow
		mov	bh, ah	; Put it here for later
		and	dl, al	; And this value with the accumulator
		lahf			; Get our flags
		and	ah, 040h	; Only the zero matters
		or		ah, bh	; Get our other original flags back
		mov	dl, dh	; Get our value back
		and	dl, 80h	; Only the negative flag
		or		ah, dl	; OR It in with the original
		and	dh, 40h	; What we do with the 6th bit
		shr	dh, 2		; Save this for later
		or		ah, dh	; OR In our "overflow"
		xor	bh, bh	; Zero this!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst25:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop27:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead27
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr27		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine27

nextAddr27:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop27

callRoutine27:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit27

memoryRead27:
		mov	dl, [ebp + edx]	; Get our data

readExit27:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROL
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst26:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx		; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop28:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead28
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr28		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine28

nextAddr28:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop28

callRoutine28:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit28

memoryRead28:
		mov	dl, [ebp + edx]	; Get our data

readExit28:
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcl	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop29:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite29	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr29	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine29	; If not, go call it!

nextAddr29:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop29

callRoutine29:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit29

memoryWrite29:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit29:
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; PLP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst28:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		inc	dh		; Stack page
		inc	byte [_m6502s]	; Increment our stack pointer
		mov	dl, [_m6502s]	; Stack pointer
		mov	ah, [ebp+edx]
		xor	edx, edx
		mov	dl, ah
		mov	[_altFlags], dl
		and	[_altFlags], byte 3ch;
		mov	ah, [bit6502tox86+edx]
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst29:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROL
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst2a:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcl	al, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		al, al	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; BIT
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst2c:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop30:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead30
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr30		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine30

nextAddr30:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop30

callRoutine30:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit30

memoryRead30:
		mov	dl, [ebp + edx]	; Get our data

readExit30:
		mov	dh, dl	; Save this, too
		and	ah, 2fh	; Kill sign, zero, and overflow
		mov	bh, ah	; Put it here for later
		and	dl, al	; And this value with the accumulator
		lahf			; Get our flags
		and	ah, 040h	; Only the zero matters
		or		ah, bh	; Get our other original flags back
		mov	dl, dh	; Get our value back
		and	dl, 80h	; Only the negative flag
		or		ah, dl	; OR It in with the original
		and	dh, 40h	; What we do with the 6th bit
		shr	dh, 2		; Save this for later
		or		ah, dh	; OR In our "overflow"
		xor	bh, bh	; Zero this!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst2d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop31:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead31
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr31		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine31

nextAddr31:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop31

callRoutine31:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit31

memoryRead31:
		mov	dl, [ebp + edx]	; Get our data

readExit31:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROL
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst2e:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx		; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop32:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead32
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr32		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine32

nextAddr32:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop32

callRoutine32:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit32

memoryRead32:
		mov	dl, [ebp + edx]	; Get our data

readExit32:
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcl	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop33:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite33	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr33	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine33	; If not, go call it!

nextAddr33:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop33

callRoutine33:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit33

memoryWrite33:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit33:
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst30:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 80h	; Are we minus?
		jnz	short takeJump30	; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJump30:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst31:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop35:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead35
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr35		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine35

nextAddr35:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop35

callRoutine35:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit35

memoryRead35:
		mov	al, [ebp + edx]	; Get our data

readExit35:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop36:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead36
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr36		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine36

nextAddr36:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop36

callRoutine36:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit36

memoryRead36:
		mov	dh, [ebp + edx]	; Get our data

readExit36:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop37:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead37
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr37		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine37

nextAddr37:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop37

callRoutine37:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit37

memoryRead37:
		mov	dl, [ebp + edx]	; Get our data

readExit37:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst35:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop38:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead38
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr38		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine38

nextAddr38:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop38

callRoutine38:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit38

memoryRead38:
		mov	dl, [ebp + edx]	; Get our data

readExit38:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROL
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst36:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		push	edx		; Save our address
		mov	dl, [ebp+edx]	; Get our zero page byte
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcl	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	[ebp+edx], bh	; Set our zero page byte
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SEC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst38:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		or	ah, 01h	; Carry!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst39:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop39:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead39
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr39		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine39

nextAddr39:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop39

callRoutine39:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit39

memoryRead39:
		mov	dl, [ebp + edx]	; Get our data

readExit39:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; AND
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst3d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop40:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead40
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr40		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine40

nextAddr40:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop40

callRoutine40:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit40

memoryRead40:
		mov	dl, [ebp + edx]	; Get our data

readExit40:
		and	al, dl	; And it
		mov	bh, ah	; Save flags for later
		lahf			; Get the flags
		and	ah, 0c0h	; Only sign and zero flag
		and	bh, 03fh	; Kill sign and zero flag
		or	ah, bh	; Get our original (other) flags back
		xor	bh, bh	; Kill it so we don't screw X up for later
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROL
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst3e:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		add	dword [dwElapsedTicks2], byte 7
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		push	edx		; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop41:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead41
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr41		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine41

nextAddr41:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop41

callRoutine41:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit41

memoryRead41:
		mov	dl, [ebp + edx]	; Get our data

readExit41:
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcl	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop42:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite42	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr42	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine42	; If not, go call it!

nextAddr42:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop42

callRoutine42:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit42

memoryWrite42:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit42:
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; RTI
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst40:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [_m6502s]	; Get our stack pointer
		add	[_m6502s], byte 3	; Restore our stack
		inc	dh		; Increment to the stack page
		inc	dl		; And our pointer to the flags
		mov	ah, [ebp+edx]	; Get our flags
		inc	dl		; Next address
		mov	si, [ebp+edx]	; Get our new address
		mov	[_m6502pc], si	; Store our program counter
		or	ah, 20h	; This bit is always 1
		mov	[_m6502af + 1], ah	; Store our flags
		test	ah, 04h	; Interrupts reenabled?
		jnz	notEnabled
		cmp	[_irqPending], byte 0 ; IRQ pending?
		je		notEnabled
		push	eax		; Save this - we need it
		call	_m6502int	; Go do an interrupt
		pop	eax		; Restore this - we need it!
notEnabled:
		xor	esi, esi
		mov	si, [_m6502pc]	; Get our program counter
		add	esi, ebp ; So it properly points to the code
		xor	edx, edx
		mov	dl, ah
		mov	[_altFlags], dl
		and	[_altFlags], byte 3ch;
		mov	ah, [bit6502tox86+edx]
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst41:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop44:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead44
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr44		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine44

nextAddr44:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop44

callRoutine44:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit44

memoryRead44:
		mov	al, [ebp + edx]	; Get our data

readExit44:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop45:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead45
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr45		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine45

nextAddr45:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop45

callRoutine45:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit45

memoryRead45:
		mov	dh, [ebp + edx]	; Get our data

readExit45:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop46:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead46
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr46		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine46

nextAddr46:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop46

callRoutine46:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit46

memoryRead46:
		mov	dl, [ebp + edx]	; Get our data

readExit46:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst45:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop47:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead47
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr47		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine47

nextAddr47:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop47

callRoutine47:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit47

memoryRead47:
		mov	dl, [ebp + edx]	; Get our data

readExit47:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Lsr
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst46:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx		; Save our address away
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop48:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead48
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr48		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine48

nextAddr48:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop48

callRoutine48:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit48

memoryRead48:
		mov	bh, [ebp + edx]	; Get our data

readExit48:
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shr	bh, 1	; Shift right by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop49:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite49	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr49	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine49	; If not, go call it!

nextAddr49:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop49

callRoutine49:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit49

memoryWrite49:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit49:
		xor	bh, bh	; Zero the upper part so we don't host X!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; PHA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst48:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		inc	dh		; Stack page
		mov	dl, [_m6502s]	; Stack pointer
		mov	[ebp+edx], al
		dec	byte [_m6502s]	; Decrement our stack pointer
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst49:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Lsr
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst4a:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		sahf		; Restore flags
		mov	dl, ah	; Store our original flags
		shr	al, 1	; Shift right by 1
		lahf	; Load the flags back in
		and	dl, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, dl		; Or it into our flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; JMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst4c:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	edx, ebp	; Add in our base
		mov	esi, edx	; Put it here for execution
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst4d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop51:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead51
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr51		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine51

nextAddr51:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop51

callRoutine51:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit51

memoryRead51:
		mov	dl, [ebp + edx]	; Get our data

readExit51:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Lsr
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst4e:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx		; Save our address away
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop52:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead52
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr52		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine52

nextAddr52:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop52

callRoutine52:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit52

memoryRead52:
		mov	bh, [ebp + edx]	; Get our data

readExit52:
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shr	bh, 1	; Shift right by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop53:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite53	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr53	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine53	; If not, go call it!

nextAddr53:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop53

callRoutine53:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit53

memoryWrite53:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit53:
		xor	bh, bh	; Zero the upper part so we don't host X!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst50:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 10h	; Overflow not set?
		jz	short takeJump50 ; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJump50:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst51:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop55:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead55
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr55		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine55

nextAddr55:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop55

callRoutine55:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit55

memoryRead55:
		mov	al, [ebp + edx]	; Get our data

readExit55:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop56:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead56
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr56		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine56

nextAddr56:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop56

callRoutine56:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit56

memoryRead56:
		mov	dh, [ebp + edx]	; Get our data

readExit56:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop57:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead57
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr57		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine57

nextAddr57:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop57

callRoutine57:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit57

memoryRead57:
		mov	dl, [ebp + edx]	; Get our data

readExit57:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst55:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop58:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead58
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr58		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine58

nextAddr58:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop58

callRoutine58:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit58

memoryRead58:
		mov	dl, [ebp + edx]	; Get our data

readExit58:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Lsr
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst56:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		push	edx		; Save our address away
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop59:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead59
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr59		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine59

nextAddr59:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop59

callRoutine59:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit59

memoryRead59:
		mov	bh, [ebp + edx]	; Get our data

readExit59:
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shr	bh, 1	; Shift right by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop60:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite60	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr60	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine60	; If not, go call it!

nextAddr60:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop60

callRoutine60:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit60

memoryWrite60:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit60:
		xor	bh, bh	; Zero the upper part so we don't host X!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CLI
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst58:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		and	[_altFlags], byte 0fbh	; No interrupts!
		cmp	[_irqPending], byte 0 ; IRQ pending?
		je		near notEnabledCli
		sub	esi, ebp	 ; Get our real program counter
		mov	[_m6502pc], si
		xor	edx, edx
		mov	dl, ah
		mov	ah, [bitx86to6502+edx]
		or		ah, [_altFlags]
		mov	[_m6502af], ax	; Save this
		push	eax		; Save this - we need it
		call	_m6502int	; Go do an interrupt
		pop	eax		; Restore this - we need it!
		mov	si, [_m6502pc]	; Get our program counter
		and	esi, 0ffffh	; Only the lower 16 bits
		add	esi, ebp ; So it properly points to the code
		mov	ax, [_m6502af] ; Restore this
		xor	edx, edx
		mov	dl, ah
		mov	[_altFlags], dl
		and	[_altFlags], byte 3ch;
		mov	ah, [bit6502tox86+edx]
notEnabledCli:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst59:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop61:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead61
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr61		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine61

nextAddr61:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop61

callRoutine61:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit61

memoryRead61:
		mov	dl, [ebp + edx]	; Get our data

readExit61:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Eor
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst5d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop62:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead62
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr62		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine62

nextAddr62:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop62

callRoutine62:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit62

memoryRead62:
		mov	dl, [ebp + edx]	; Get our data

readExit62:
		mov	dh, ah	; Get the flags
		xor	al, dl		; XOR In our value
		lahf			; Get the flag settings
		and	ah, 0c0h	; Only sign and zero flag
		and	dh, 03fh	; Clear sign & zero flags
		or	ah, dh		; Merge the affected flags together
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Lsr
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst5e:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		add	dword [dwElapsedTicks2], byte 7
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		push	edx		; Save our address away
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop63:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead63
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr63		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine63

nextAddr63:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop63

callRoutine63:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	bh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit63

memoryRead63:
		mov	bh, [ebp + edx]	; Get our data

readExit63:
		sahf		; Restore flags
		mov	ch, ah	; Store our original flags
		shr	bh, 1	; Shift right by 1
		lahf	; Load the flags back in
		and	ch, 03eh	; No carry, zero, or sign
		and	ah, 0c1h	; Only carry, zero and sign
		or	ah, ch		; Or it into our flags
		xor	ch, ch	; Clear it!
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop64:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite64	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr64	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine64	; If not, go call it!

nextAddr64:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop64

callRoutine64:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit64

memoryWrite64:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit64:
		xor	bh, bh	; Zero the upper part so we don't host X!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; RTS
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst60:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		xor	esi, esi	; Zero ESI for later
		mov	dl, [_m6502s]	; Get our stack
		add	[_m6502s], byte 2	; Pop off a word
		inc	dl	; Increment our stack page
		inc	dh	; Our stack page
		mov	si, [ebp+edx]	; Get our stack area
		inc	si	; Increment!
		add	esi, ebp	; Add in our base address
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst61:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop66:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead66
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr66		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine66

nextAddr66:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop66

callRoutine66:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit66

memoryRead66:
		mov	al, [ebp + edx]	; Get our data

readExit66:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop67:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead67
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr67		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine67

nextAddr67:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop67

callRoutine67:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit67

memoryRead67:
		mov	dh, [ebp + edx]	; Get our data

readExit67:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop68:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead68
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr68		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine68

nextAddr68:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop68

callRoutine68:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit68

memoryRead68:
		mov	dl, [ebp + edx]	; Get our data

readExit68:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode0	; It's binary mode
		jmp	decimalMode0		; Yup - go handle dec mode
binaryMode0:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode0:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed0
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed0:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv0
		or		ah, 040h	; Set overflow
noOv0:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo0
		add	bh, 6		; Digit fixup
noOvTwo0:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry0
		or		ah, 01h	; Set carry
noCarry0:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst65:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop69:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead69
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr69		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine69

nextAddr69:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop69

callRoutine69:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit69

memoryRead69:
		mov	dl, [ebp + edx]	; Get our data

readExit69:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode1	; It's binary mode
		jmp	decimalMode1		; Yup - go handle dec mode
binaryMode1:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode1:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed1
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed1:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv1
		or		ah, 040h	; Set overflow
noOv1:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo1
		add	bh, 6		; Digit fixup
noOvTwo1:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry1
		or		ah, 01h	; Set carry
noCarry1:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROR
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst66:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx		; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop70:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead70
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr70		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine70

nextAddr70:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop70

callRoutine70:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit70

memoryRead70:
		mov	dl, [ebp + edx]	; Get our data

readExit70:
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcr	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop71:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite71	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr71	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine71	; If not, go call it!

nextAddr71:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop71

callRoutine71:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit71

memoryWrite71:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit71:
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; PLA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst68:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		inc	dh		; Stack page
		inc	byte [_m6502s]	; Increment our stack pointer
		mov	dl, [_m6502s]	; Stack pointer
		mov	al, [ebp+edx]
		mov	dl, ah
		and	dl, 03fh
		or		al, al
		lahf
		and	ah, 0c0h
		or		ah, dl
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst69:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode2	; It's binary mode
		jmp	decimalMode2		; Yup - go handle dec mode
binaryMode2:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode2:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed2
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed2:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv2
		or		ah, 040h	; Set overflow
noOv2:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo2
		add	bh, 6		; Digit fixup
noOvTwo2:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry2
		or		ah, 01h	; Set carry
noCarry2:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROR
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst6a:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcr	al, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		al, al	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; JMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst6c:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop72:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead72
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr72		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine72

nextAddr72:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop72

callRoutine72:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit72

memoryRead72:
		mov	al, [ebp + edx]	; Get our data

readExit72:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop73:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead73
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr73		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine73

nextAddr73:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop73

callRoutine73:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit73

memoryRead73:
		mov	dh, [ebp + edx]	; Get our data

readExit73:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	edx, ebp	; Add in our base
		mov	esi, edx	; Put it here for execution
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst6d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop75:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead75
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr75		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine75

nextAddr75:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop75

callRoutine75:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit75

memoryRead75:
		mov	dl, [ebp + edx]	; Get our data

readExit75:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode3	; It's binary mode
		jmp	decimalMode3		; Yup - go handle dec mode
binaryMode3:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode3:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed3
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed3:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv3
		or		ah, 040h	; Set overflow
noOv3:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo3
		add	bh, 6		; Digit fixup
noOvTwo3:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry3
		or		ah, 01h	; Set carry
noCarry3:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROR
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst6e:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx		; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop76:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead76
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr76		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine76

nextAddr76:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop76

callRoutine76:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit76

memoryRead76:
		mov	dl, [ebp + edx]	; Get our data

readExit76:
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcr	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop77:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite77	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr77	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine77	; If not, go call it!

nextAddr77:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop77

callRoutine77:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit77

memoryWrite77:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit77:
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst70:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 10h	; Overflow set?
		jnz	short takeJump70 ; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJump70:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst71:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop79:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead79
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr79		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine79

nextAddr79:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop79

callRoutine79:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit79

memoryRead79:
		mov	al, [ebp + edx]	; Get our data

readExit79:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop80:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead80
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr80		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine80

nextAddr80:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop80

callRoutine80:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit80

memoryRead80:
		mov	dh, [ebp + edx]	; Get our data

readExit80:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop81:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead81
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr81		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine81

nextAddr81:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop81

callRoutine81:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit81

memoryRead81:
		mov	dl, [ebp + edx]	; Get our data

readExit81:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode4	; It's binary mode
		jmp	decimalMode4		; Yup - go handle dec mode
binaryMode4:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode4:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed4
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed4:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv4
		or		ah, 040h	; Set overflow
noOv4:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo4
		add	bh, 6		; Digit fixup
noOvTwo4:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry4
		or		ah, 01h	; Set carry
noCarry4:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst75:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop82:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead82
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr82		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine82

nextAddr82:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop82

callRoutine82:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit82

memoryRead82:
		mov	dl, [ebp + edx]	; Get our data

readExit82:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode5	; It's binary mode
		jmp	decimalMode5		; Yup - go handle dec mode
binaryMode5:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode5:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed5
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed5:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv5
		or		ah, 040h	; Set overflow
noOv5:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo5
		add	bh, 6		; Digit fixup
noOvTwo5:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry5
		or		ah, 01h	; Set carry
noCarry5:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROR
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst76:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		push	edx		; Save our address
		mov	dl, [ebp+edx]	; Get our zero page byte
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcr	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	[ebp+edx], bh	; Set our zero page byte
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SEI
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst78:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		or		[_altFlags], byte 04h	; Interrupts!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst79:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop83:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead83
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr83		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine83

nextAddr83:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop83

callRoutine83:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit83

memoryRead83:
		mov	dl, [ebp + edx]	; Get our data

readExit83:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode6	; It's binary mode
		jmp	decimalMode6		; Yup - go handle dec mode
binaryMode6:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode6:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed6
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed6:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv6
		or		ah, 040h	; Set overflow
noOv6:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo6
		add	bh, 6		; Digit fixup
noOvTwo6:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry6
		or		ah, 01h	; Set carry
noCarry6:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ADC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst7d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop84:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead84
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr84		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine84

nextAddr84:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop84

callRoutine84:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit84

memoryRead84:
		mov	dl, [ebp + edx]	; Get our data

readExit84:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		je		binaryMode7	; It's binary mode
		jmp	decimalMode7		; Yup - go handle dec mode
binaryMode7:
		sahf		; Restore our flags for the adc
		adc	al, dl	; Add in our value
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode7:
		xor	di, di	; Zero DI for later
		sahf
		adc	di, 0		; Increment if carry is set
		and	ah, 03ch	; Knock out carry, zero, sign, and overflow
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	bl, al	; Get Accumulator
		mov	bh, dl	; Original value
		mov	cx, bx	; Put it here for later
		and	bx, 0f0fh	; Only the nibbles
		add	bl, bh	; Add it to the lower value
		add	bx, di	; Add in our carry
		shr	cx, 4		; Upper nibbles only
		and	cx, 0f0fh	; Only the lower nibbles now
		add	cl, ch	; Add in the original value
		mov	bh, cl	; Put our upper nibble in BH
		xor	cx, cx	; Zero the upper part
		cmp	bl, 9		; Digit overflow?
		jbe	notOverflowed7
		inc	bh			; Increment - we've overflowed
		add	bl, 6		; Fix the lower nibble
notOverflowed7:
		mov	cl, al	; Get the accumulator
		xor	cl, dl	; XOR It with the original value
		not	cl			; Invert & add 1
		mov	ch, bh	; Get our high BCD
		shl	ch, 4		; Move into position
		and	ch, cl	; And 'em together
		or		ch, ch	; See if we've overflowed
		jns	noOv7
		or		ah, 040h	; Set overflow
noOv7:
		cmp	bh, 9		; Greater than 9?
		jbe	noOvTwo7
		add	bh, 6		; Digit fixup
noOvTwo7:
		mov	al, bh	; Get most significant nibble
		shl	al, 4		; Put it into position
		and	bl, 0fh	; Only the lower nibble matters now
		or		al, bl	; Put it in the accumulator
		test	bh, 0f0h	; Carry?
		jz		noCarry7
		or		ah, 01h	; Set carry
noCarry7:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; ROR
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst7e:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		add	dword [dwElapsedTicks2], byte 7
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		push	edx		; Save our address
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop85:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead85
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr85		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine85

nextAddr85:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop85

callRoutine85:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit85

memoryRead85:
		mov	dl, [ebp + edx]	; Get our data

readExit85:
		mov	dh, ah	; Save off our original flags
		and	dh, 3eh	; No carry, zero, or sign
		sahf		; Restore flags
		rcr	dl, 1	; Through carry rotate
		lahf		; Get the current flags back
		mov	bh, ah ; Store it here for later
		and	bh, 1	; Only the carry matters
		or		dl, dl	; Set sign/zero
		lahf		; Get the flags
		and	ah, 0c0h	; Only sign and zero
		or		ah, bh	; Or In our carry
		or		ah, dh	; Or in our original flags
		mov	bh, dl	; Get our byte to write
		pop	edx		; Restore the address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop86:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite86	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr86	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine86	; If not, go call it!

nextAddr86:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop86

callRoutine86:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit86

memoryWrite86:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit86:
		xor	bh, bh	; Zero this so we don't screw up things
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst81:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop87:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead87
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr87		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine87

nextAddr87:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop87

callRoutine87:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit87

memoryRead87:
		mov	al, [ebp + edx]	; Get our data

readExit87:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop88:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead88
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr88		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine88

nextAddr88:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop88

callRoutine88:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit88

memoryRead88:
		mov	dh, [ebp + edx]	; Get our data

readExit88:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop89:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite89	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr89	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine89	; If not, go call it!

nextAddr89:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop89

callRoutine89:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit89

memoryWrite89:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit89:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst84:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop90:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite90	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr90	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine90	; If not, go call it!

nextAddr90:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop90

callRoutine90:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	ecx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit90

memoryWrite90:
		mov	[ebp + edx], cl ; Store the byte

writeMacroExit90:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst85:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop91:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite91	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr91	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine91	; If not, go call it!

nextAddr91:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop91

callRoutine91:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit91

memoryWrite91:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit91:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst86:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop92:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite92	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr92	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine92	; If not, go call it!

nextAddr92:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop92

callRoutine92:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit92

memoryWrite92:
		mov	[ebp + edx], bl ; Store the byte

writeMacroExit92:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; DEY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst88:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, ah	; Save flags
		dec	cl	; Decrement
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; TXA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst8a:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	al, bl	; A = X
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst8c:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop93:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite93	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr93	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine93	; If not, go call it!

nextAddr93:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop93

callRoutine93:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	ecx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit93

memoryWrite93:
		mov	[ebp + edx], cl ; Store the byte

writeMacroExit93:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst8d:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop94:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite94	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr94	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine94	; If not, go call it!

nextAddr94:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop94

callRoutine94:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit94

memoryWrite94:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit94:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst8e:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop95:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite95	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr95	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine95	; If not, go call it!

nextAddr95:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop95

callRoutine95:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit95

memoryWrite95:
		mov	[ebp + edx], bl ; Store the byte

writeMacroExit95:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst90:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 1h	; Carry not set?
		jz		short takeJump90 ; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJump90:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst91:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop97:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead97
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr97		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine97

nextAddr97:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop97

callRoutine97:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit97

memoryRead97:
		mov	al, [ebp + edx]	; Get our data

readExit97:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop98:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead98
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr98		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine98

nextAddr98:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop98

callRoutine98:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit98

memoryRead98:
		mov	dh, [ebp + edx]	; Get our data

readExit98:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop99:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite99	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr99	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine99	; If not, go call it!

nextAddr99:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop99

callRoutine99:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit99

memoryWrite99:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit99:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst94:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop100:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite100	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr100	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine100	; If not, go call it!

nextAddr100:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop100

callRoutine100:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	ecx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit100

memoryWrite100:
		mov	[ebp + edx], cl ; Store the byte

writeMacroExit100:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst95:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop101:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite101	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr101	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine101	; If not, go call it!

nextAddr101:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop101

callRoutine101:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit101

memoryWrite101:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit101:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst96:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, cl	; Add Y
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop102:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite102	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr102	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine102	; If not, go call it!

nextAddr102:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop102

callRoutine102:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit102

memoryWrite102:
		mov	[ebp + edx], bl ; Store the byte

writeMacroExit102:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; TYA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst98:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	al, cl	; A = Y
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst99:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop103:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite103	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr103	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine103	; If not, go call it!

nextAddr103:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop103

callRoutine103:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit103

memoryWrite103:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit103:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; TXS
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst9a:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	[_m6502s], bl	; X -> S
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; STA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInst9d:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop104:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite104	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr104	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine104	; If not, go call it!

nextAddr104:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop104

callRoutine104:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		push	eax	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit104

memoryWrite104:
		mov	[ebp + edx], al ; Store the byte

writeMacroExit104:
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta0:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	cl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dl, ah	; Save flags
		or	cl, cl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta1:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop105:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead105
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr105		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine105

nextAddr105:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop105

callRoutine105:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit105

memoryRead105:
		mov	al, [ebp + edx]	; Get our data

readExit105:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop106:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead106
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr106		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine106

nextAddr106:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop106

callRoutine106:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit106

memoryRead106:
		mov	dh, [ebp + edx]	; Get our data

readExit106:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop107:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead107
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr107		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine107

nextAddr107:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop107

callRoutine107:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit107

memoryRead107:
		mov	al, [ebp + edx]	; Get our data

readExit107:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta2:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	bl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta4:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop108:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead108
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr108		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine108

nextAddr108:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop108

callRoutine108:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit108

memoryRead108:
		mov	cl, [ebp + edx]	; Get our data

readExit108:
		mov	dl, ah	; Save flags
		or	cl, cl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta5:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop109:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead109
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr109		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine109

nextAddr109:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop109

callRoutine109:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit109

memoryRead109:
		mov	al, [ebp + edx]	; Get our data

readExit109:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta6:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop110:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead110
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr110		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine110

nextAddr110:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop110

callRoutine110:
		call	ReadMemoryByte	; Standard read routine
		mov	cl, [_m6502y]	; Get Y back
		mov	bl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit110

memoryRead110:
		mov	bl, [ebp + edx]	; Get our data

readExit110:
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; TAY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta8:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	cl, al	; Y = A
		mov	dl, ah	; Save flags
		or	cl, cl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsta9:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	al, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; TAX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstaa:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	bl, al	; X = A
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstac:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop111:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead111
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr111		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine111

nextAddr111:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop111

callRoutine111:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit111

memoryRead111:
		mov	cl, [ebp + edx]	; Get our data

readExit111:
		mov	dl, ah	; Save flags
		or	cl, cl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstad:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop112:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead112
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr112		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine112

nextAddr112:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop112

callRoutine112:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit112

memoryRead112:
		mov	al, [ebp + edx]	; Get our data

readExit112:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstae:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop113:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead113
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr113		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine113

nextAddr113:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop113

callRoutine113:
		call	ReadMemoryByte	; Standard read routine
		mov	cl, [_m6502y]	; Get Y back
		mov	bl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit113

memoryRead113:
		mov	bl, [ebp + edx]	; Get our data

readExit113:
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb0:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 1h	; Is carry set?
		jnz	short takeJumpb0	; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJumpb0:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb1:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop115:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead115
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr115		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine115

nextAddr115:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop115

callRoutine115:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit115

memoryRead115:
		mov	al, [ebp + edx]	; Get our data

readExit115:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop116:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead116
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr116		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine116

nextAddr116:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop116

callRoutine116:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit116

memoryRead116:
		mov	dh, [ebp + edx]	; Get our data

readExit116:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop117:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead117
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr117		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine117

nextAddr117:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop117

callRoutine117:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit117

memoryRead117:
		mov	al, [ebp + edx]	; Get our data

readExit117:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb4:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop118:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead118
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr118		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine118

nextAddr118:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop118

callRoutine118:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit118

memoryRead118:
		mov	cl, [ebp + edx]	; Get our data

readExit118:
		mov	dl, ah	; Save flags
		or	cl, cl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb5:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop119:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead119
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr119		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine119

nextAddr119:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop119

callRoutine119:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit119

memoryRead119:
		mov	al, [ebp + edx]	; Get our data

readExit119:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb6:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, cl	; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop120:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead120
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr120		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine120

nextAddr120:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop120

callRoutine120:
		call	ReadMemoryByte	; Standard read routine
		mov	cl, [_m6502y]	; Get Y back
		mov	bl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit120

memoryRead120:
		mov	bl, [ebp + edx]	; Get our data

readExit120:
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CLV
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb8:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		and	ah, 0efh	; Clear out overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstb9:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop121:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead121
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr121		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine121

nextAddr121:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop121

callRoutine121:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit121

memoryRead121:
		mov	al, [ebp + edx]	; Get our data

readExit121:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; TSX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstba:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	bl, [_m6502s]	; S -> X
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstbc:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop122:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead122
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr122		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine122

nextAddr122:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop122

callRoutine122:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit122

memoryRead122:
		mov	cl, [ebp + edx]	; Get our data

readExit122:
		mov	dl, ah	; Save flags
		or	cl, cl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDA
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstbd:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop123:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead123
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr123		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine123

nextAddr123:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop123

callRoutine123:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit123

memoryRead123:
		mov	al, [ebp + edx]	; Get our data

readExit123:
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; LDX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstbe:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop124:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead124
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr124		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine124

nextAddr124:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop124

callRoutine124:
		call	ReadMemoryByte	; Standard read routine
		mov	cl, [_m6502y]	; Get Y back
		mov	bl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit124

memoryRead124:
		mov	bl, [ebp + edx]	; Get our data

readExit124:
		mov	dl, ah	; Save flags
		or	bl, bl	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CPY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc0:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dh, ah	; Get our flags
		cmp	cl, dl	; Compare with Y!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc1:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop125:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead125
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr125		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine125

nextAddr125:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop125

callRoutine125:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit125

memoryRead125:
		mov	al, [ebp + edx]	; Get our data

readExit125:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop126:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead126
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr126		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine126

nextAddr126:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop126

callRoutine126:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit126

memoryRead126:
		mov	dh, [ebp + edx]	; Get our data

readExit126:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop127:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead127
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr127		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine127

nextAddr127:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop127

callRoutine127:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit127

memoryRead127:
		mov	dl, [ebp + edx]	; Get our data

readExit127:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CPY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc4:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop128:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead128
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr128		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine128

nextAddr128:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop128

callRoutine128:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit128

memoryRead128:
		mov	dl, [ebp + edx]	; Get our data

readExit128:
		mov	dh, ah	; Get our flags
		cmp	cl, dl	; Compare with Y!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc5:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop129:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead129
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr129		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine129

nextAddr129:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop129

callRoutine129:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit129

memoryRead129:
		mov	dl, [ebp + edx]	; Get our data

readExit129:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; DEC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc6:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop130:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead130
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr130		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine130

nextAddr130:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop130

callRoutine130:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit130

memoryRead130:
		mov	dl, [ebp + edx]	; Get our data

readExit130:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		dec	bh		; Decrement!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop131:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite131	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr131	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine131	; If not, go call it!

nextAddr131:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop131

callRoutine131:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit131

memoryWrite131:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit131:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; INY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc8:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, ah	; Save flags
		inc	cl	; Increment
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstc9:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; DEX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstca:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, ah	; Save flags
		dec	bl	; Decrement
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CPY
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstcc:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop132:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead132
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr132		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine132

nextAddr132:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop132

callRoutine132:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit132

memoryRead132:
		mov	dl, [ebp + edx]	; Get our data

readExit132:
		mov	dh, ah	; Get our flags
		cmp	cl, dl	; Compare with Y!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstcd:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop133:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead133
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr133		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine133

nextAddr133:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop133

callRoutine133:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit133

memoryRead133:
		mov	dl, [ebp + edx]	; Get our data

readExit133:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; DEC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstce:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop134:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead134
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr134		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine134

nextAddr134:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop134

callRoutine134:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit134

memoryRead134:
		mov	dl, [ebp + edx]	; Get our data

readExit134:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		dec	bh		; Decrement!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop135:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite135	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr135	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine135	; If not, go call it!

nextAddr135:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop135

callRoutine135:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit135

memoryWrite135:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit135:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstd0:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 40h	; Are we non-zero?
		jz	short takeJumpd0	; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJumpd0:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstd1:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop137:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead137
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr137		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine137

nextAddr137:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop137

callRoutine137:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit137

memoryRead137:
		mov	al, [ebp + edx]	; Get our data

readExit137:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop138:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead138
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr138		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine138

nextAddr138:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop138

callRoutine138:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit138

memoryRead138:
		mov	dh, [ebp + edx]	; Get our data

readExit138:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop139:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead139
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr139		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine139

nextAddr139:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop139

callRoutine139:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit139

memoryRead139:
		mov	dl, [ebp + edx]	; Get our data

readExit139:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstd5:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop140:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead140
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr140		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine140

nextAddr140:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop140

callRoutine140:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit140

memoryRead140:
		mov	dl, [ebp + edx]	; Get our data

readExit140:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; DEC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstd6:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop141:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead141
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr141		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine141

nextAddr141:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop141

callRoutine141:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit141

memoryRead141:
		mov	dl, [ebp + edx]	; Get our data

readExit141:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		dec	bh		; Decrement!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop142:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite142	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr142	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine142	; If not, go call it!

nextAddr142:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop142

callRoutine142:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit142

memoryWrite142:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit142:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CLD
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstd8:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		and	[_altFlags], byte 0f7h	; Binary mode
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstd9:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop143:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead143
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr143		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine143

nextAddr143:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop143

callRoutine143:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit143

memoryRead143:
		mov	dl, [ebp + edx]	; Get our data

readExit143:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CMP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstdd:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop144:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead144
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr144		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine144

nextAddr144:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop144

callRoutine144:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit144

memoryRead144:
		mov	dl, [ebp + edx]	; Get our data

readExit144:
		mov	dh, ah	; Get our flags
		cmp	al, dl	; Compare!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; DEC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstde:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		add	dword [dwElapsedTicks2], byte 7
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop145:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead145
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr145		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine145

nextAddr145:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop145

callRoutine145:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit145

memoryRead145:
		mov	dl, [ebp + edx]	; Get our data

readExit145:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		dec	bh		; Decrement!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop146:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite146	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr146	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine146	; If not, go call it!

nextAddr146:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop146

callRoutine146:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit146

memoryWrite146:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit146:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CPX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste0:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		mov	dh, ah	; Get our flags
		cmp	bl, dl	; Compare with X!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste1:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add in X
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop147:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead147
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr147		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine147

nextAddr147:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop147

callRoutine147:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit147

memoryRead147:
		mov	al, [ebp + edx]	; Get our data

readExit147:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop148:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead148
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr148		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine148

nextAddr148:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop148

callRoutine148:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit148

memoryRead148:
		mov	dh, [ebp + edx]	; Get our data

readExit148:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop149:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead149
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr149		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine149

nextAddr149:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop149

callRoutine149:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit149

memoryRead149:
		mov	dl, [ebp + edx]	; Get our data

readExit149:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode8	; It's binary mode
		jmp	decimalMode8		; Yup - go handle dec mode
binaryMode8:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode8:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow8 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow8:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow8
		sub	bl, 6		; BCD Fixup
noUnderFlow8:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg8
		dec	bh			; Borrow
noHighNeg8:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow8
		sub	bh, 6		; BCD Fixup
noUpperUnderflow8:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow8 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow8:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CPX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste4:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop150:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead150
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr150		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine150

nextAddr150:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop150

callRoutine150:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit150

memoryRead150:
		mov	dl, [ebp + edx]	; Get our data

readExit150:
		mov	dh, ah	; Get our flags
		cmp	bl, dl	; Compare with X!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste5:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop151:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead151
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr151		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine151

nextAddr151:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop151

callRoutine151:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit151

memoryRead151:
		mov	dl, [ebp + edx]	; Get our data

readExit151:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode9	; It's binary mode
		jmp	decimalMode9		; Yup - go handle dec mode
binaryMode9:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode9:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow9 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow9:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow9
		sub	bl, 6		; BCD Fixup
noUnderFlow9:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg9
		dec	bh			; Borrow
noHighNeg9:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow9
		sub	bh, 6		; BCD Fixup
noUpperUnderflow9:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow9 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow9:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; INC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste6:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop152:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead152
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr152		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine152

nextAddr152:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop152

callRoutine152:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit152

memoryRead152:
		mov	dl, [ebp + edx]	; Get our data

readExit152:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		inc	bh		; Increment!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop153:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite153	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr153	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine153	; If not, go call it!

nextAddr153:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop153

callRoutine153:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit153

memoryWrite153:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit153:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; INX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste8:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, ah	; Save flags
		inc	bl	; Increment
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInste9:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		mov	dl, [esi]	; Get our next byte
		inc	esi		; Increment to our next byte
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode10	; It's binary mode
		jmp	decimalMode10		; Yup - go handle dec mode
binaryMode10:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode10:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow10 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow10:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow10
		sub	bl, 6		; BCD Fixup
noUnderFlow10:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg10
		dec	bh			; Borrow
noHighNeg10:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow10
		sub	bh, 6		; BCD Fixup
noUpperUnderflow10:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow10 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow10:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; NOP
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstea:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; CPX
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstec:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop154:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead154
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr154		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine154

nextAddr154:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop154

callRoutine154:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit154

memoryRead154:
		mov	dl, [ebp + edx]	; Get our data

readExit154:
		mov	dh, ah	; Get our flags
		cmp	bl, dl	; Compare with X!
		cmc			; Compliment carry flag
		lahf
		and	ah, 0c1h	; Sign, zero, and carry
		and	dh, 03eh	; Everything but sign, zero and carry
		or		ah, dh	; OR In our new flags
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInsted:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop155:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead155
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr155		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine155

nextAddr155:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop155

callRoutine155:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit155

memoryRead155:
		mov	dl, [ebp + edx]	; Get our data

readExit155:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode11	; It's binary mode
		jmp	decimalMode11		; Yup - go handle dec mode
binaryMode11:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode11:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow11 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow11:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow11
		sub	bl, 6		; BCD Fixup
noUnderFlow11:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg11
		dec	bh			; Borrow
noHighNeg11:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow11
		sub	bh, 6		; BCD Fixup
noUpperUnderflow11:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow11 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow11:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; INC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstee:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop156:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead156
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr156		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine156

nextAddr156:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop156

callRoutine156:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit156

memoryRead156:
		mov	dl, [ebp + edx]	; Get our data

readExit156:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		inc	bh		; Increment!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop157:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite157	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr157	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine157	; If not, go call it!

nextAddr157:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop157

callRoutine157:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit157

memoryWrite157:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit157:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; Branch
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstf0:
		sub	dword [cyclesRemaining], byte 3
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 3
		add	dword [dwElapsedTicks2], byte 3
		test	ah, 40h	; Non-zero set?
		jnz	short takeJumpf0 ; Do it!
		inc	esi	; Skip past the offset
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

takeJumpf0:
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		movsx	dx, dl
		sub	esi, ebp
		add	si, dx
		add	esi, ebp
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstf1:
		sub	dword [cyclesRemaining], byte 5
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 5
		add	dword [dwElapsedTicks2], byte 5
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		push	edx	; Save address
		mov	[_m6502af], ax	; Store AF
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop159:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead159
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr159		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine159

nextAddr159:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop159

callRoutine159:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	ah, [_m6502af + 1] ; Get our flags back
		jmp	short readExit159

memoryRead159:
		mov	al, [ebp + edx]	; Get our data

readExit159:
		pop	edx	; Restore address
		inc	dx	; Next address
		push	eax	; Save it for later
		mov	ax, [_m6502af]	; Restore AF because it gets used later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop160:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead160
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr160		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine160

nextAddr160:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop160

callRoutine160:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dh, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit160

memoryRead160:
		mov	dh, [ebp + edx]	; Get our data

readExit160:
		pop	eax	; Restore it!
		mov	dl, al	; Restore our word into DX
		mov	ax, [_m6502af]	; Restore AF
		add	dx, cx	; Add in Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop161:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead161
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr161		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine161

nextAddr161:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop161

callRoutine161:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit161

memoryRead161:
		mov	dl, [ebp + edx]	; Get our data

readExit161:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode12	; It's binary mode
		jmp	decimalMode12		; Yup - go handle dec mode
binaryMode12:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode12:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow12 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow12:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow12
		sub	bl, 6		; BCD Fixup
noUnderFlow12:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg12
		dec	bh			; Borrow
noHighNeg12:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow12
		sub	bh, 6		; BCD Fixup
noUpperUnderflow12:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow12 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow12:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstf5:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop162:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead162
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr162		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine162

nextAddr162:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop162

callRoutine162:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit162

memoryRead162:
		mov	dl, [ebp + edx]	; Get our data

readExit162:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode13	; It's binary mode
		jmp	decimalMode13		; Yup - go handle dec mode
binaryMode13:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode13:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow13 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow13:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow13
		sub	bl, 6		; BCD Fixup
noUnderFlow13:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg13
		dec	bh			; Borrow
noHighNeg13:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow13
		sub	bh, 6		; BCD Fixup
noUpperUnderflow13:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow13 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow13:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; INC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstf6:
		sub	dword [cyclesRemaining], byte 6
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 6
		add	dword [dwElapsedTicks2], byte 6
		mov	dl, [esi]		; Get the next instruction
		inc	esi		; Advance PC!
		add	dl, bl	; Add X
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop163:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead163
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr163		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine163

nextAddr163:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop163

callRoutine163:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit163

memoryRead163:
		mov	dl, [ebp + edx]	; Get our data

readExit163:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		inc	bh		; Increment!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop164:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite164	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr164	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine164	; If not, go call it!

nextAddr164:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop164

callRoutine164:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit164

memoryWrite164:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit164:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SED
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstf8:
		sub	dword [cyclesRemaining], byte 2
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 2
		add	dword [dwElapsedTicks2], byte 2
		or		[_altFlags], byte 08h	; Decimal mode
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstf9:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		add	dword [dwElapsedTicks2], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, cx	 ; Add Y
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop165:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead165
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr165		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine165

nextAddr165:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop165

callRoutine165:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit165

memoryRead165:
		mov	dl, [ebp + edx]	; Get our data

readExit165:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode14	; It's binary mode
		jmp	decimalMode14		; Yup - go handle dec mode
binaryMode14:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode14:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow14 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow14:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow14
		sub	bl, 6		; BCD Fixup
noUnderFlow14:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg14
		dec	bh			; Borrow
noHighNeg14:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow14
		sub	bh, 6		; BCD Fixup
noUpperUnderflow14:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow14 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow14:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; SBC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstfd:
		sub	dword [cyclesRemaining], byte 4
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 4
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop166:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead166
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr166		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine166

nextAddr166:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop166

callRoutine166:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit166

memoryRead166:
		mov	dl, [ebp + edx]	; Get our data

readExit166:
		test	[_altFlags], byte 08h ; Are we in decimal mode?
		jz		binaryMode15	; It's binary mode
		jmp	decimalMode15		; Yup - go handle dec mode
binaryMode15:
		sahf		; Restore our flags for the adc
		cmc
		sbb	al, dl	; Subtract our value
		cmc
		o16 pushf	; Push our flags (and overflow)
		and	ah, 02eh	; No carry, overflow, zero or sign
		pop	dx	; Restore our flags into DX
		shl	dh, 1	; Shift overflow into position
		and	dh, 10h	; Only the overflow
		and	dl, 0c1h	; Only carry, sign, and zero
		or		ah, dl	; OR In our new flags
		or		ah, dh	; OR In overflow
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

decimalMode15:
		xor	di, di	; Zero our carry flag surrogate
		xor	ah, 01h	; Toggle carry - intentional
		sahf
		adc	di, 0	; Make 1 if carry is set
		xor	ah, ah	; Clear our flag register
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		xor	dh, dh		; Just in case we want to add with it later
		mov	cx, ax		; This is our sum
		sub	cx, dx		; Subtract our value
		sub	cx, di		; And our carry status
		mov	bl, al		; Get accumulator
		xor	bl, dl		; XOR With our value
		mov	bh, al		; Get accumulator
		xor	bh, cl		; XOR With our sum
		and	bl, bh		; See if we overflow
		and	bl, 80h		; Only the top bit
		jns	noOverflow15 ; No overflow
		or		ah, 40h		; Indicate an overflow
noOverflow15:
		mov	bl, al		; Get our low value
		mov	bh, dl		; Original value
		and	bx, 0f0fh	; Only the lower nibbles
		sub	bl, bh		; Subtract
		xor	bh, bh		; Zero this
		sub	bx, di		; Subtract our !Carry flag
		test	bl, 0f0h		; Did we underflow?
		jz		noUnderFlow15
		sub	bl, 6		; BCD Fixup
noUnderFlow15:
		shr	al, 4		; Upper BCD
		shr	dl, 4		; Upper BCD for the value
		mov	bh, al	; A
		sub	bh, dl	; Subtract our bigger BCD
		or		bl, bl	; Set flags
		jns	noHighNeg15
		dec	bh			; Borrow
noHighNeg15:
		test	bh, 0f0h	; Upper nibble? Underflow?
		jz		noUpperUnderflow15
		sub	bh, 6		; BCD Fixup
noUpperUnderflow15:
		or		ch, ch	; Did we underflow on our sum?
		jnz	noUnderflow15 ; Nope!
		or		ah, 01h	; Set carry
noUnderflow15:
		and	bl, 0fh	; Only the lower nibble
		mov	al, bl	; Low part
		shl	bh, 4		; Move to upper BCD
		or		al, bh	; OR It in!
		mov	dl, ah	; Save flags
		or	al, al	; OR Our new value
		lahf		; Restore flags
		and	dl, 03fh	; Original value
		and	ah, 0c0h	; Only zero and sign
		or	ah, dl		; New flags with the old!
		xor	bx, bx	; Zero
		xor	cx, cx
		mov	bl, [_m6502x]	; X!
		mov	cl, [_m6502y]	; Y!
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

;
; INC
;

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

RegInstfe:
		sub	dword [cyclesRemaining], byte 7
		jc	near noMoreExec	; Can't execute anymore!
		add	dword [dwElapsedTicks], byte 7
		mov	dx, [esi]	; Get our address
		add	esi, 2	; Increment past instruction
		add	dx, bx	 ; Add X
		push	edx	; Save this for later
		mov	edi, [_m6502MemRead]	; Point to the read array

checkLoop167:
		cmp	[edi], word 0ffffh ; End of the list?
		je		short memoryRead167
		cmp	dx, [edi]	; Are we smaller?
		jb		short nextAddr167		; Yes, go to the next address
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	short callRoutine167

nextAddr167:
		add	edi, 10h		; Next structure!
		jmp	short checkLoop167

callRoutine167:
		call	ReadMemoryByte	; Standard read routine
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		mov	dl, al	; Get our value
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short readExit167

memoryRead167:
		mov	dl, [ebp + edx]	; Get our data

readExit167:
		mov	bh, dl	; Save the data we just got
		mov	ch, ah	; Save flags
		and	ch, 03fh	; No sign or zero flags
		inc	bh		; Increment!
		lahf
		and	ah, 0c0h	; Only sign &  zero flags
		or	ah, ch	; Merge the two flags together
		pop	edx	; Restore our address
		mov	edi, [_m6502MemWrite]	; Point to the write array

checkLoop168:
		cmp	[edi], word 0ffffh ; End of our list?
		je	near memoryWrite168	; Yes - go write it!
		cmp	dx, [edi]	; Are we smaller?
		jb	nextAddr168	; Yes... go to the next addr
		cmp	dx, [edi+4]	; Are we bigger?
		jbe	callRoutine168	; If not, go call it!

nextAddr168:
		add	edi, 10h		; Next structure, please
		jmp	short checkLoop168

callRoutine168:
		mov	[_m6502x], bl	; Save X
		mov	[_m6502y], cl	; Save Y
		mov	[_m6502af], ax	; Save Accumulator & flags
		sub	esi, ebp	; Our program counter
		mov	[_m6502pc], si	; Save our program counter
		push	edi	; Pointer to MemoryWriteByte structure
		mov	bl, bh	; Put a copy here
		push	ebx	; The byte value
		and	edx, 0ffffh	; Only lower 16 bits
		push	edx	; The address
		call	dword [edi + 8] ; Go call our handler
		add	esp, 12	; Get rid of our stack
		xor	ebx, ebx	; Zero this
		xor	ecx, ecx	; This too!
		mov	bl, [_m6502x]	; Get X back
		mov	cl, [_m6502y]	; Get Y back
		xor	esi, esi	; Zero it!
		mov	si, [_m6502pc]	; Get our program counter back
		mov	ebp, [_m6502Base] ; Base pointer comes back
		add	esi, ebp	; Rebase it properly
		mov	ax, [_m6502af]	; Get our flags & stuff back
		jmp	short writeMacroExit168

memoryWrite168:
		mov	[ebp + edx], bh ; Store the byte

writeMacroExit168:
		xor	bh, bh	; Zero this so we don't totally screw things up
		xor	ch, ch	; Zero this as well
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

		global	_m6502GetContext
		global	m6502GetContext_
		global	m6502GetContext

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502GetContext_:
_m6502GetContext:
		mov	eax, [esp+4]	; Get our context address
		push	esi		; Save registers we use
		push	edi
		push	ecx
		mov     ecx, _m6502contextEnd - _m6502contextBegin
		mov	esi, _m6502contextBegin
		mov	edi, eax	; Source address in ESI
		rep	movsb		; Move it as fast as we can!
		pop	ecx
		pop	edi
		pop	esi
		ret			; No return code
		global	_m6502SetContext
		global	m6502SetContext_
		global	m6502SetContext

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502SetContext_:
_m6502SetContext:
		mov	eax, [esp+4]	; Get our context address
		push	esi		; Save registers we use
		push	edi
		push	ecx
		mov     ecx, _m6502contextEnd - _m6502contextBegin
		mov	edi, _m6502contextBegin
		mov	esi, eax	; Source address in ESI
		rep	movsb		; Move it as fast as we can!
		pop	ecx
		pop	edi
		pop	esi
		ret			; No return code
		global	_m6502GetContextSize
		global	m6502GetContextSize_
		global	m6502GetContextSize

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502GetContextSize_:
_m6502GetContextSize:
		mov     eax, _m6502contextEnd - _m6502contextBegin
		ret
		global	_m6502init
		global	m6502init_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502init_:
_m6502init:
		ret
		
		
		
		global	_m6502GetElapsedTicks
		global	m6502GetElapsedTicks_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502GetElapsedTicks_:
_m6502GetElapsedTicks:
		mov	eax, [esp+4]	; Get our context address
		or	eax, eax	; Should we clear it?
		jz	getTicks
		xor	eax, eax
		xchg	eax, [dwElapsedTicks]
		ret
getTicks:
		mov	eax, [dwElapsedTicks]
		ret
		
;----------------------------------------------------------------------------
global	_m6502GetElapsedTicks2
		global	m6502GetElapsedTicks2_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502GetElapsedTicks2_:
_m6502GetElapsedTicks2:
		mov	eax, [esp+4]	; Get our context address
		or	eax, eax	; Should we clear it?
		jz	getTicks
		xor	eax, eax
		xchg	eax, [dwElapsedTicks2]
		ret
getTicks2:
		mov	eax, [dwElapsedTicks2]
		ret
		

;----------------------------------------------------------------------------		
		
		global	_m6502ReleaseTimeslice
		global	m6502ReleaseTimeslice_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary


m6502ReleaseTimeslice_:
_m6502ReleaseTimeslice:
		mov	[cyclesRemaining], dword 1
		ret
		global	_m6502reset
		global	m6502reset_
		global	m6502reset

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502reset_:
_m6502reset:
		push	ebp	; Save our important register
		xor	eax, eax
		mov	ebp, [_m6502Base]
		mov	[_m6502x], al
		mov	[_m6502y], al
		mov	[_irqPending], al
		mov	[_m6502s], byte 0ffh
		mov	[_m6502af], word 2200h
		mov	ax, [ebp + 0fffch] ; Get reset address
		mov	[_m6502pc], ax
		pop	ebp
		ret

		global	_m6502int
		global	m6502int_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502int_:
_m6502int:
		test	[_m6502af + 1], byte 04h	; Are interrupts disabled?
		jnz	intNotTaken	; Nope! Make it pending!
		mov	[_irqPending], byte 00h	; No longer pending
		push    edi
		push    ebx
		push    ax
		mov     ebx, 0100h              ; Point to stack page
		mov	  edi, [_m6502Base]	; Get pointer to game image
		mov     bl, [_m6502s]           ; Get our S reg pointer
		mov     ax, [_m6502pc]          ; Get our PC
		mov     [edi + ebx], ah         ; Store it!
		dec     bl
		mov     [edi + ebx], al         ; Store it!
		dec     bl
		mov     al, byte [_m6502af+1] ; Get our flags
		mov     [edi + ebx], al         ; Store flags
		dec     bl
		mov     ax, [edi+0fffeh]        ; Get our start vector!
		mov     [_m6502s], bl             ; Store S reg pointer
		and	  [_m6502af + 1], byte 0efh		; Knock out source of interrupt bit
		or		  [_m6502af + 1], byte 24h		; Turn on something
		mov     [_m6502pc], ax            ; Store our new PC
		pop     ax                      ; Restore used registers
		pop     ebx
		pop     edi
		xor	eax, eax		; Indicate we've taken the interrupt
		mov	[_irqPending], al	; No more IRQ pending!
		ret
intNotTaken:
		mov	eax, 1		; Indicate we didn't take it
		mov	[_irqPending], al ; Indicate we have a pending IRQ
		ret
		global	_m6502nmi
		global	m6502nmi_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502nmi_:
_m6502nmi:
		push    edi
		push    ebx

		mov     ebx, 0100h              ; Point to stack page

		mov     edi, [_m6502Base]       ; Get pointer to game image
		mov     bl, [_m6502s]           ; Get our S reg pointer
		mov     ax, [_m6502pc]          ; Get our PC

		mov     [edi + ebx], ah         ; Store it!
		dec     bl
		mov     [edi + ebx], al         ; Store it!
		dec     bl

		mov     al, byte [_m6502af+1] ; Get our flags


		mov     [edi + ebx], al         ; Store flags
		dec     bl
		mov     ax, [edi+0fffah]        ; Get our start vector!

		mov     [_m6502s], bl             ; Store S reg pointer
		and     [_m6502af + 1], byte 0efh      ; Knock out source of interrupt bit
		or      [_m6502af + 1], byte 24h       ; Turn on something
		mov     [_m6502pc], ax            ; Store our new PC

		pop     ebx
		pop     edi

		xor	eax, eax		; Indicate that we took the NMI
		ret
		global	_m6502exec
		global	m6502exec_

times ($$-$) & 3 nop	; pad with NOPs to 4-byte boundary

m6502exec_:
_m6502exec:
		mov	eax, [esp+4]	; Get our execution cycle count
		push	ebx			; Save all registers we use
		push	ecx
		push	edx
		push	ebp
		push	esi
		push	edi

		mov	dword [cyclesRemaining], eax	; Store # of instructions to
		cld				; Go forward!

		xor	eax, eax		; Zero EAX 'cause we use it!
		xor	ebx, ebx		; Zero EBX, too
		xor	ecx, ecx		; Zero ECX
		xor	esi, esi		; Zero our source address

		mov	bl, [_m6502x]	; Get X
		mov	cl, [_m6502y]	; Get Y
		mov	ax, [_m6502af]	; Get our flags and accumulator
		mov	si, [_m6502pc]	; Get our program counter
		mov	ebp, [_m6502Base]	; Get our base address register
		add	esi, ebp		; Add in our base address
		xor	edx, edx		; And EDX
		xor	edi, edi		; Zero EDI as well
		xor	edx, edx
		mov	dl, ah
		mov	[_altFlags], dl
		and	[_altFlags], byte 3ch;
		mov	ah, [bit6502tox86+edx]
		xor	edx, edx
		mov	dl, [esi]
		inc	esi
		jmp	dword [m6502regular+edx*4]

invalidInsWord:
		dec	esi

; We get to invalidInsByte if it's a single byte invalid opcode

invalidInsByte:
		dec	esi			; Back up one instruction...
		mov	edx, esi		; Get our address in EAX
		sub	edx, ebp		; And subtract our base for
						; an invalid instruction
		jmp	short emulateEnd

noMoreExec:
		dec	esi
		mov	edx, 80000000h		; Indicate successful exec
emulateEnd:
		push	edx		; Save this for the return
		xor	edx, edx
		mov	dl, ah
		mov	ah, [bitx86to6502+edx]
		or		ah, [_altFlags]
		mov	[_m6502x], bl	; Store X
		mov	[_m6502y], cl	; Store Y
		mov	[_m6502af], ax	; Store A & flags
		sub	esi, ebp	; Get our PC back
		mov	[_m6502pc], si	; Store PC
		pop	edx		; Restore EDX for later

popReg:
		mov	eax, edx	; Get our result code
		pop	edi			; Restore registers
		pop	esi
		pop	ebp
		pop	edx
		pop	ecx
		pop	ebx

		ret

		end
