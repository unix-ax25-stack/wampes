/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipfilter.c,v 1.4 1994/10/06 16:15:27 deyke Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "netuser.h"
#include "cmdparse.h"

struct ipfilter_t {
	int32 addr;
	int32 mask;
	int bits;
	struct ipfilter_t *next;
};

static struct ipfilter_t *Ipfilter;

int ipfilter(int32 addr)
{
	struct ipfilter_t *curr, *prev;

	if (!Ipfilter)
		return 1;
	for (prev = 0, curr = Ipfilter; curr; prev = curr, curr = curr->next)
		if ((addr & curr->mask) == curr->addr) {
			if (prev) {
				prev->next = curr->next;
				curr->next = Ipfilter;
				Ipfilter = curr;
			}
			return 1;
		}
	return 0;
}

static int
doipfilteradd(int argc, char *argv[], void *p)
{

	char *bitp;
	int bits;
	int32 addr;
	int32 mask;
	struct ipfilter_t *ptr;

	if ((bitp = strchr(argv[1], '/')) != NULLCHAR) {
		*bitp++ = 0;
		bits = atoi(bitp);
		if (bits > 32)
			bits = 32;
		else if (bits < 0)
			bits = 0;
	} else
		bits = 32;

	if ((addr = resolve(argv[1])) == 0) {
		printf(Badhost, argv[1]);
		return 1;
	}
	if (bits)
		mask = ~0L << (32 - bits);
	else
		mask = 0L;
	addr &= mask;

	for (ptr = Ipfilter; ptr; ptr = ptr->next)
		if (ptr->addr == addr && ptr->bits == bits)
			return 0;

	ptr = (struct ipfilter_t *) malloc(sizeof(struct ipfilter_t));
	ptr->addr = addr;
	ptr->mask = mask;
	ptr->bits = bits;
	ptr->next = Ipfilter;
	Ipfilter = ptr;
	return 0;
}

static int
doipfilterdrop(int argc, char *argv[], void *p)
{

	char *bitp;
	int bits;
	int32 addr;
	int32 mask;
	struct ipfilter_t *curr;
	struct ipfilter_t *prev;

	if ((bitp = strchr(argv[1], '/')) != NULLCHAR) {
		*bitp++ = 0;
		bits = atoi(bitp);
		if (bits > 32)
			bits = 32;
		else if (bits < 0)
			bits = 0;
	} else
		bits = 32;

	if ((addr = resolve(argv[1])) == 0) {
		printf(Badhost, argv[1]);
		return 1;
	}
	if (bits)
		mask = ~0L << (32 - bits);
	else
		mask = 0L;
	addr &= mask;

	for (prev = 0, curr = Ipfilter; curr; prev = curr, curr = curr->next)
		if (curr->addr == addr && curr->bits == bits) {
			if (prev)
				prev->next = curr->next;
			else
				Ipfilter = curr->next;
			free(curr);
			break;
		}
	return 0;
}

static struct cmds Ipfiltercmds[] = {
	"add",          doipfilteradd,  0,      2,
	"ipfilter add <addr>[/<bits>]",

	"drop",         doipfilterdrop, 0,      2,
	"ipfilter drop <addr>[/<bits>]",

	NULLCHAR,
};

int
doipfilter(int argc, char *argv[], void *p)
{

	struct ipfilter_t *ptr;

	if (argc >= 2)
		return subcmd(Ipfiltercmds, argc, argv, p);

	for (ptr = Ipfilter; ptr; ptr = ptr->next)
		printf("%s/%d\n", inet_ntoa(ptr->addr), ptr->bits);

	return 0;
}

