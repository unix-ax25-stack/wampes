/* @(#) $Id: lockfile.h,v 1.4 1996/08/12 18:53:41 deyke Exp $ */

#ifndef _LOCKFILE_H
#define _LOCKFILE_H

/* In lockfile.c: */
int lock_fd(int fd, int exclusive, int dont_block);
int lock_file(const char *filename, int exclusive, int dont_block);

#endif  /* _LOCKFILE_H */
