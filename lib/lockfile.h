/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/lockfile.h,v 1.1 1994/02/22 13:23:13 deyke Exp $ */

#ifndef _LOCKFILE_H
#define _LOCKFILE_H

/* In lockfile.c: */
int lock_fd(int fd, int dont_block);
int lock_file(const char *filename, int dont_block);

#endif  /* _LOCKFILE_H */
