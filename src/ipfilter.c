/* @(#) $Id: ipfilter.c,v 1.10 1999/06/20 17:47:46 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "netuser.h"
#include "cmdparse.h"

#define USAGE "ipfilter allow|deny <addr>[/<bits>] [to <addr>[/<bits>]]"

struct ipfilter_t {
	unsigned long lo;
	unsigned long hi;
	int allow;
	struct ipfilter_t *next;
};

static struct ipfilter_t *Ipfilter;

/*---------------------------------------------------------------------------*/

int ipfilter(int32 addr)
{
	struct ipfilter_t *p;

	for (p = Ipfilter; p; p = p->next) {
		if (((unsigned long) addr) >= p->lo &&
		    ((unsigned long) addr) <= p->hi) {
			return p->allow;
		}
	}
	return 1;
}

/*---------------------------------------------------------------------------*/

static int isaddr(const char *s)
{
	int c;

	if (s) {
		while ((c = *s++)) {
			if ((c < '0' || c > '9') && c != '.') {
				return 0;
			}
		}
	}
	return 1;
}

/*---------------------------------------------------------------------------*/

static int parse(char *name, unsigned long *loptr, unsigned long *hiptr)
{

	char *bitp;
	int bits;
	int32 addr;
	int32 mask;

	if ((bitp = strchr(name, '/'))) {
		*bitp++ = 0;
		bits = atoi(bitp);
		if (bits > 32) {
			bits = 32;
		} else if (bits < 0) {
			bits = 0;
		}
	} else {
		bits = 32;
	}

	if (isaddr(name)) {
		addr = aton(name);
	} else if (!(addr = resolve(name))) {
		printf(Badhost, name);
		return 1;
	}

	if (bits) {
		mask = ~0L << (32 - bits);
	} else {
		mask = 0L;
	}

	*loptr = addr & mask;
	*hiptr = addr | ~mask;

	return 0;
}

/*---------------------------------------------------------------------------*/

static int doipfilteradd(int argc, char *argv[], void *parg)
{

	int allow;
	struct ipfilter_t **pp;
	struct ipfilter_t *p1;
	struct ipfilter_t *p;
	unsigned long hi1;
	unsigned long hi;
	unsigned long lo1;
	unsigned long lo;

	allow = (**argv == 'a');

	if (parse(argv[1], &lo, &hi)) {
		return 1;
	}

	if (argc > 2) {
		if ((argc != 4 || strcmp(argv[2], "to"))) {
			printf("Usage: %s\n", USAGE);
			return 1;
		}
		if (parse(argv[3], &lo1, &hi1)) {
			return 1;
		}
		if (lo > lo1) {
			lo = lo1;
		}
		if (hi < hi1) {
			hi = hi1;
		}
	}

	p = (struct ipfilter_t *) malloc(sizeof(struct ipfilter_t));
	if (!p) {
		printf(Nospace);
		return 1;
	}
	p->lo = lo;
	p->hi = hi;
	p->allow = allow;
	p->next = Ipfilter;
	Ipfilter = p;

      simplify:

	for (p1 = Ipfilter; p1; p1 = p1->next) {
		for (pp = &p1->next; (p = *pp); pp = &p->next) {
			if (p->lo >= p1->lo && p->hi <= p1->hi) {
				*pp = p->next;
				free(p);
				goto simplify;
			}
		}
	}

	for (pp = &Ipfilter; (p = *pp); pp = &p->next) {
		lo = p->lo;
		if (lo) {
			lo--;
		}
		hi = p->hi;
		if (hi != 0xffffffff) {
			hi++;
		}
		for (p1 = p->next; p1; p1 = p1->next) {
			if (p1->lo <= hi && p1->hi >= lo) {
				if (p1->allow != p->allow) {
					if (p1->lo <= p->hi && p1->hi >= p->lo) {
						break;
					} else {
						continue;
					}
				}
				if (p1->lo > p->lo) {
					p1->lo = p->lo;
				}
				if (p1->hi < p->hi) {
					p1->hi = p->hi;
				}
				*pp = p->next;
				free(p);
				goto simplify;
			}
		}
	}

	return 0;
}

/*---------------------------------------------------------------------------*/

static struct cmds Ipfiltercmds[] =
{
	{"allow", doipfilteradd, 0, 2, USAGE},
	{"deny",  doipfilteradd, 0, 2, USAGE},
	{0,       0,             0, 0, 0}
};

/*---------------------------------------------------------------------------*/

int doipfilter(int argc, char *argv[], void *parg)
{
	struct ipfilter_t *p;

	if (!Ipfilter) {
		Ipfilter = (struct ipfilter_t *) malloc(sizeof(struct ipfilter_t));
		if (!Ipfilter) {
			printf(Nospace);
			return 1;
		}
		Ipfilter->lo = 0;
		Ipfilter->hi = 0xffffffff;
		Ipfilter->allow = 1;
		Ipfilter->next = 0;
	}

	if (argc >= 2) {
		return subcmd(Ipfiltercmds, argc, argv, parg);
	}

	for (p = Ipfilter; p; p = p->next) {
		printf("%s ", p->allow ? "allow" : "deny ");
		if (p->lo == p->hi) {
			printf("%s\n", inet_ntoa((int32) p->lo));
		} else {
			printf("%lu.%lu.%lu.%lu to %lu.%lu.%lu.%lu\n",
			       (p->lo >> 24) & 0xff,
			       (p->lo >> 16) & 0xff,
			       (p->lo >>  8) & 0xff,
			       (p->lo      ) & 0xff,
			       (p->hi >> 24) & 0xff,
			       (p->hi >> 16) & 0xff,
			       (p->hi >>  8) & 0xff,
			       (p->hi      ) & 0xff);
		}
	}
	return 0;
}
