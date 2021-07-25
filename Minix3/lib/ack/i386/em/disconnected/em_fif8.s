.sect .text; .sect .rom; .sect .data; .sect .bss
.define .fif8

	.sect .text
.fif8:
	mov	bx,sp
	fldd	8(bx)
	fmuld	16(bx)		! multiply
	fld	st		! and copy result
	ftst			! test sign; handle negative separately
	fstsw	ax
	wait
	sahf			! result of test in condition codes
	jb	1f
	frndint			! this one rounds (?)
	fcom	st(1)		! compare with original; if <=, then OK
	fstsw	ax
	wait
	sahf
	jbe	2f
	fisubs	(one)		! else subtract 1
	jmp	2f
1:				! here, negative case
	frndint			! this one rounds (?)
	fcom	st(1)		! compare with original; if >=, then OK
	fstsw	ax
	wait
	sahf
	jae	2f
	fiadds	(one)		! else add 1
2:
	fsub	st(1),st	! subtract integer part
	mov	bx,4(bx)
	fstpd	(bx)
	fstpd	8(bx)
	wait
	ret
