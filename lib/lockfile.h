/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/lockfile.h,v 1.3 1996/08/11 18:17:33 deyke Exp $ */

#ifndef _LOCKFILE_H
#define _LOCKFILE_H

/* In lockfile.c: */
int lock_fd(int fd, int exclusive, int dont_block);
int lock_file(const char *filename, int exclusive, int dont_block);

#endif  /* _LOCKFILE_H */
