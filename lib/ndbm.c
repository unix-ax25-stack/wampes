/* @(#) $Id: ndbm.c,v 1.1 2026/08/09 thomas Exp $ */

/* Minimal ndbm-compatible database for systems without a system ndbm
 * (e.g. macOS).
 *
 * Single-file format:
 *   magic "WNDBM1\n" followed by records
 *     [uint32_be keylen][key][uint32_be vallen][value]
 *
 * The whole database is cached in memory on open and rewritten on close
 * if modified. This is sufficient for the small hostaddr/hostname
 * databases used by wampes.
 *
 * Semantics follow classic ndbm:
 *   - dbm_fetch/dbm_firstkey/dbm_nextkey return pointers that stay valid
 *     until the next call on the same DBM.
 *   - dbm_store returns 0 on success, 1 if DBM_INSERT found the key,
 *     -1 on error.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ndbm.h"

#define NDBM_MAGIC	"WNDBM1\n"
#define NDBM_MAGIC_LEN	7

struct record {
	char	*key;
	int	 klen;
	char	*val;
	int	 vlen;
};

struct _ndbm {
	int	 fd;
	int	 error;
	int	 dirty;
	struct record *recs;
	int	 nrecs;
	int	 cap;
	int	 iter;
	datum	 lastkey;
	datum	 lastval;
};

static void
put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static unsigned long
get32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
	    ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static struct record *
findrec(DBM *db, datum key)
{
	int i;

	for (i = 0; i < db->nrecs; i++) {
		if (db->recs[i].klen == key.dsize &&
		    memcmp(db->recs[i].key, key.dptr, key.dsize) == 0)
			return &db->recs[i];
	}
	return NULL;
}

static int
addrec(DBM *db, const char *key, int klen, const char *val, int vlen)
{
	struct record *r;
	char *ckey, *cval;

	if (db->nrecs == db->cap) {
		int newcap = db->cap ? db->cap * 2 : 16;
		struct record *nrecs = realloc(db->recs,
		    newcap * sizeof(struct record));
		if (!nrecs)
			return -1;
		db->recs = nrecs;
		db->cap = newcap;
	}
	r = &db->recs[db->nrecs];
	ckey = malloc(klen);
	cval = malloc(vlen);
	if (!ckey || !cval) {
		free(ckey);
		free(cval);
		return -1;
	}
	memcpy(ckey, key, klen);
	memcpy(cval, val, vlen);
	r->key = ckey;
	r->klen = klen;
	r->val = cval;
	r->vlen = vlen;
	db->nrecs++;
	return 0;
}

DBM *
dbm_open(const char *file, int flags, int mode)
{
	DBM *db;
	unsigned char hdr[NDBM_MAGIC_LEN];
	unsigned char lenb[4];
	ssize_t n;
	int fd;

	if ((flags & O_ACCMODE) == O_WRONLY)
		flags = (flags & ~O_ACCMODE) | O_RDWR;

	db = calloc(1, sizeof(DBM));
	if (!db)
		return NULL;

	fd = open(file, flags, mode);
	if (fd < 0) {
		free(db);
		return NULL;
	}
	db->fd = fd;
	db->error = 0;

	if (read(fd, hdr, NDBM_MAGIC_LEN) == NDBM_MAGIC_LEN &&
	    memcmp(hdr, NDBM_MAGIC, NDBM_MAGIC_LEN) == 0) {
		for (;;) {
			unsigned long klen, vlen;
			char *key, *val;

			n = read(fd, lenb, 4);
			if (n == 0)
				break;
			if (n != 4)
				break;
			klen = get32(lenb);
			key = malloc(klen);
			if (!key)
				break;
			if (read(fd, key, klen) != (ssize_t)klen) {
				free(key);
				break;
			}
			if (read(fd, lenb, 4) != 4) {
				free(key);
				break;
			}
			vlen = get32(lenb);
			val = malloc(vlen);
			if (!val) {
				free(key);
				break;
			}
			if (read(fd, val, vlen) != (ssize_t)vlen) {
				free(key);
				free(val);
				break;
			}
			addrec(db, key, klen, val, vlen);
			free(key);
			free(val);
		}
	}
	return db;
}

void
dbm_close(DBM *db)
{
	int i;

	if (!db)
		return;
	if (db->dirty && db->fd >= 0) {
		unsigned char lenb[4];

		lseek(db->fd, 0, SEEK_SET);
		if (write(db->fd, NDBM_MAGIC, NDBM_MAGIC_LEN) != NDBM_MAGIC_LEN) {
			db->error = 1;
		} else {
			for (i = 0; i < db->nrecs && !db->error; i++) {
				struct record *r = &db->recs[i];

				put32(lenb, (unsigned long)r->klen);
				if (write(db->fd, lenb, 4) != 4 ||
				    write(db->fd, r->key, r->klen) != r->klen) {
					db->error = 1;
					break;
				}
				put32(lenb, (unsigned long)r->vlen);
				if (write(db->fd, lenb, 4) != 4 ||
				    write(db->fd, r->val, r->vlen) != r->vlen) {
					db->error = 1;
					break;
				}
			}
		}
		ftruncate(db->fd, lseek(db->fd, 0, SEEK_CUR));
	}
	if (db->fd >= 0)
		close(db->fd);
	for (i = 0; i < db->nrecs; i++) {
		free(db->recs[i].key);
		free(db->recs[i].val);
	}
	free(db->recs);
	free(db->lastkey.dptr);
	free(db->lastval.dptr);
	free(db);
}

int
dbm_store(DBM *db, datum key, datum content, int flags)
{
	struct record *r;

	if (!db)
		return -1;
	r = findrec(db, key);
	if (r) {
		char *cval;

		if (flags == DBM_INSERT) {
			return 1;
		}
		cval = malloc(content.dsize);
		if (!cval)
			return -1;
		memcpy(cval, content.dptr, content.dsize);
		free(r->val);
		r->val = cval;
		r->vlen = content.dsize;
		db->dirty = 1;
		return 0;
	}
	if (addrec(db, key.dptr, key.dsize, content.dptr, content.dsize) < 0)
		return -1;
	db->dirty = 1;
	return 0;
}

datum
dbm_fetch(DBM *db, datum key)
{
	datum res;
	struct record *r;
	char *cval;

	res.dptr = NULL;
	res.dsize = 0;
	if (!db)
		return res;
	r = findrec(db, key);
	if (!r)
		return res;
	cval = malloc(r->vlen);
	if (!cval)
		return res;
	memcpy(cval, r->val, r->vlen);
	free(db->lastval.dptr);
	db->lastval.dptr = cval;
	db->lastval.dsize = r->vlen;
	return db->lastval;
}

int
dbm_delete(DBM *db, datum key)
{
	struct record *r;
	int i;

	if (!db)
		return -1;
	r = findrec(db, key);
	if (!r) {
		db->error = 1;
		return -1;
	}
	i = r - db->recs;
	free(r->key);
	free(r->val);
	memmove(&db->recs[i], &db->recs[i + 1],
	    (db->nrecs - i - 1) * sizeof(struct record));
	db->nrecs--;
	db->dirty = 1;
	return 0;
}

datum
dbm_firstkey(DBM *db)
{
	if (db)
		db->iter = 0;
	return dbm_nextkey(db);
}

datum
dbm_nextkey(DBM *db)
{
	datum res;
	struct record *r;
	char *ckey;

	res.dptr = NULL;
	res.dsize = 0;
	if (!db || db->iter >= db->nrecs)
		return res;
	r = &db->recs[db->iter++];
	ckey = malloc(r->klen);
	if (!ckey)
		return res;
	memcpy(ckey, r->key, r->klen);
	free(db->lastkey.dptr);
	db->lastkey.dptr = ckey;
	db->lastkey.dsize = r->klen;
	return db->lastkey;
}

int
dbm_error(DBM *db)
{
	return db ? db->error : 0;
}

int
dbm_clearerr(DBM *db)
{
	if (db)
		db->error = 0;
	return 0;
}
