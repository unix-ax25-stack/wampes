/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/seek.h,v 1.2 1996/08/11 18:17:33 deyke Exp $ */

#ifndef _SEEK_H
#define _SEEK_H

#ifdef ibm032

#include <sys/fcntl.h>
#include <sys/file.h>

#define SEEK_SET        L_SET
#define SEEK_CUR        L_INCR
#define SEEK_END        L_XTND

#else

#include <stdio.h>

#ifndef SEEK_SET
#define SEEK_SET        0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR        1
#endif

#ifndef SEEK_END
#define SEEK_END        2
#endif

#endif

#endif  /* _SEEK_H */
