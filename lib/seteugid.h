/* @(#) $Id: seteugid.h,v 1.3 1996/08/12 18:53:41 deyke Exp $ */

#ifndef _SETEUGID_H
#define _SETEUGID_H

/* In seteugid.c: */
void seteugid(int uid, int gid);

/* Give up privileges for good - real, effective and saved id, and root's
 * supplementary groups with them.  seteugid() only changes the effective id
 * and is meant to be undone; this cannot be undone, and that is the point:
 * a child process that finds real uid 0 can call setuid(0) whatever its
 * effective id is.  Call it in the child, between fork() and exec().
 *
 * Does nothing and succeeds if the process already runs as uid.  Returns 0
 * on success, -1 if any step failed - in which case the caller still holds
 * privileges and must not go on to exec anything.
 */
int dropprivileges(const char *name, int uid, int gid);

#endif  /* _SETEUGID_H */
