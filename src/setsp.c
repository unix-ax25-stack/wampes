/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/setsp.c,v 1.10 1995/03/13 13:32:18 deyke Exp $ */

#ifndef __lint

#ifdef __hp9000s300
	text
	global  _setstack
_setstack:
	mov.l   (%sp),%a0
	mov.l   _newstackptr,%sp
	jmp     (%a0)
#endif

#ifdef __hp9000s800
	.SPACE  $TEXT$,SORT=8
	.SUBSPA $CODE$,QUAD=0,ALIGN=4,ACCESS=44,CODE_ONLY,SORT=24
setstack
	.PROC
	.CALLINFO CALLER,FRAME=0
	.ENTRY
	ADDIL   LR'newstackptr-$global$,%r27            ;offset 0x0 '
	LDW     RR'newstackptr-$global$(0,%r1),%r31     ;offset 0x4 '
	BV      %r0(%r2)                                ;offset 0xc
	.EXIT
	COPY    %r31,%r30                               ;patch to set SP
	.PROCEND

	.SPACE  $TEXT$
	.SUBSPA $LIT$,QUAD=0,ALIGN=8,ACCESS=44,SORT=16
	.SUBSPA $CODE$
	.SPACE  $PRIVATE$,SORT=16
	.SUBSPA $DATA$,QUAD=1,ALIGN=8,ACCESS=31,SORT=16
$THIS_DATA$

	.SUBSPA $SHORTDATA$,QUAD=1,ALIGN=8,ACCESS=31,SORT=24
$THIS_SHORTDATA$

	.SUBSPA $BSS$,QUAD=1,ALIGN=8,ACCESS=31,ZERO,SORT=82
$THIS_BSS$

	.SUBSPA $SHORTBSS$,QUAD=1,ALIGN=8,ACCESS=31,ZERO,SORT=80
$THIS_SHORTBSS$

	.SUBSPA $STATICDATA$,QUAD=1,ALIGN=8,ACCESS=31,SORT=16
$STATIC_DATA$

	.SUBSPA $SHORTSTATICDATA$,QUAD=1,ALIGN=8,ACCESS=31,SORT=24
$SHORT_STATIC_DATA$

	.IMPORT $global$,DATA
	.IMPORT newstackptr,DATA
	.SPACE  $TEXT$
	.SUBSPA $CODE$
	.EXPORT setstack,ENTRY,PRIV_LEV=3
	.END
#endif

#ifdef linux
	.file   "setsp.s"
	.globl  _setstack
_setstack:
	movl    %esp, %ebp
	movl    _newstackptr, %esp
	jmp     *(%ebp)
	.align  4
#endif

#ifdef __386BSD__
	.file   "setsp.s"
	.globl  _setstack
_setstack:
	movl    %esp, %ebp
	movl    _newstackptr, %esp
	jmp     *(%ebp)
	.align  4
#endif

#ifdef __FreeBSD__
	.file   "setsp.s"
	.globl  _setstack
_setstack:
	movl    %esp, %ebp
	movl    _newstackptr, %esp
	jmp     *(%ebp)
	.align  4
#endif

#ifdef __bsdi__
	.file   "setsp.s"
	.globl  _setstack
_setstack:
	movl    %esp, %ebp
	movl    _newstackptr, %esp
	jmp     *(%ebp)
	.align  4
#endif

#ifdef RISCiX
	.text
	.global _setstack
_setstack:
	mov     r13,r0
	movs    r15,r14
#endif

#endif
