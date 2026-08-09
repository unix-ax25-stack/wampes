/* @(#) $Id: ndbm.h,v 1.1 2026/08/09 thomas Exp $ */

/* Minimal ndbm-compatible interface for systems without a system ndbm
 * (e.g. macOS). See ndbm.c for the implementation.
 */

#ifndef _NDBM_H
#define _NDBM_H

#include <sys/types.h>

typedef struct {
	char	*dptr;
	int	 dsize;
} datum;

typedef struct _ndbm DBM;

#define DBM_INSERT	0
#define DBM_REPLACE	1

extern DBM *dbm_open(const char *file, int flags, int mode);
extern void dbm_close(DBM *db);
extern int dbm_store(DBM *db, datum key, datum content, int flags);
extern datum dbm_fetch(DBM *db, datum key);
extern int dbm_delete(DBM *db, datum key);
extern datum dbm_firstkey(DBM *db);
extern datum dbm_nextkey(DBM *db);
extern int dbm_error(DBM *db);
extern int dbm_clearerr(DBM *db);

#endif
