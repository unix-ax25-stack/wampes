/* ndbm.h
 * Copyright (C) 2026 Thomas Osterried <dl9sau@darc.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

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
