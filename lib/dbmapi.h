/* Which dbm interface this build uses - the header and the code path, decided
 * together and in one place.
 *
 * configure probes four possibilities and THEY ARE NOT EXCLUSIVE.  A Debian
 * with libgdbm-compat-dev and libgdbm-dev installed has <ndbm.h> and <gdbm.h>
 * both, so HAS_NDBM and HAS_GDBM come out 1 together.
 *
 * domain.c and mkhostdb.c each carried a copy of the same cascade for the
 * INCLUDE - first match wins, so <ndbm.h> - and then chose the code path with
 * a plain "#if HAS_GDBM", which is a different question.  With both set that
 * compiled the gdbm code against the ndbm header:
 *
 *      domain.c:78: unknown type name 'GDBM_FILE'
 *      domain.c:364: implicit declaration of 'gdbm_open'; did you mean
 *                    'dbm_open'?
 *
 * and the build stopped.  It never showed on macOS, where configure forces
 * HAS_NDBM alone for the bundled shim, so the two questions happened to have
 * the same answer.
 *
 * USE_GDBM says which API the code should call, and it is by construction the
 * one whose header was just included.
 */

#ifndef _DBMAPI_H
#define _DBMAPI_H

#ifndef _CONFIGURE_H
#include "configure.h"
#endif

#if HAS_NDBM
#include <ndbm.h>
#define USE_GDBM 0
#elif HAS_DB1_NDBM
#include <db1/ndbm.h>
#define USE_GDBM 0
#elif HAS_GDBM_NDBM
#include <gdbm-ndbm.h>
#define USE_GDBM 0
#elif HAS_GDBM
#include <gdbm.h>
#define USE_GDBM 1
#else
#error No dbm interface found - see the dbm probe in lib/configure
#endif

#endif  /* _DBMAPI_H */
