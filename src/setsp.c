/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/setsp.c,v 1.1 1991/10/18 18:42:53 deyke Exp $ */

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
	ADDIL   LR'newstackptr-$global$,%r27    ;offset 0x0
	LDW     RR'newstackptr-$global$(0,%r1),%r31     ;offset 0x4
	BV      %r0(%r2)        ;offset 0xc
	.EXIT
	COPY    %r31,%r30       ;patch to set SP
	.PROCEND ;

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

#ifdef ISC
	.file   "setsp.s"
	.version        "02.01"
	.data
	.text
	.align  4
	.def    setstack;       .val    setstack;       .scl    2;      .type   044;    .endef
	.globl  setstack
setstack:
	movl    %esp, %ebp
	movl    newstackptr, %esp
	jmp     *(%ebp)
	.def    setstack;       .val    .;      .scl    -1;     .endef
	.data
#endif
