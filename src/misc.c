/* @(#) $Id: misc.c,v 1.25 1999/02/01 22:24:25 deyke Exp $ */

/* Miscellaneous machine independent utilities
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <ctype.h>
#include <stdio.h>
#include "global.h"
#include "socket.h"
#include "mbuf.h"

char Whitespace[] = " \t\r\n";

int Xtolower(
int c)
{
	return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

int Xtoupper(
int c)
{
	return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

/* Select from an array of strings, or return ascii number if out of range */
char *
smsg(char *msgs[],unsigned nmsgs,unsigned n)
{
	static char buf[16];

	if(n < nmsgs && msgs[n] != NULL)
		return msgs[n];
	sprintf(buf,"%u",n);
	return buf;
}

/* Convert hex-ascii to integer */
int
htoi(char *s)
{
	int i = 0;
	char c;

	while((c = *s++) != '\0'){
		if(c == 'x')
			continue;       /* allow 0x notation */
		if('0' <= c && c <= '9')
			i = (i * 16) + (c - '0');
		else if('a' <= c && c <= 'f')
			i = (i * 16) + (c - 'a' + 10);
		else if('A' <= c && c <= 'F')
			i = (i * 16) + (c - 'A' + 10);
		else
			break;
	}
	return i;
}
/* Convert single hex-ascii character to binary */
int
htob(char c)
{
	if('0' <= c && c <= '9')
		return c - '0';
	else if('a' <= c && c <= 'f')
		return c - 'a' + 10;
	else if('A' <= c && c <= 'F')
		return c - 'A' + 10;
	else
		return -1;
}
/* Read an ascii-encoded hex string, convert to binary and store in
 * output buffer. Return number of bytes converted
 */
int
readhex(uint8 *out,char *in,int size)
{
	int c,count;

	if(in == NULL)
		return 0;
	for(count=0;count < size;count++){
		while(*in == ' ' || *in == '\t')
			in++;   /* Skip white space */
		if((c = htob(*in++)) == -1)
			break;  /* Hit non-hex character */
		out[count] = c << 4;    /* First nybble */
		while(*in == ' ' || *in == '\t')
			in++;   /* Skip white space */
		if((c = htob(*in++)) == -1)
			break;  /* Hit non-hex character */
		out[count] |= c;        /* Second nybble */
	}
	return count;
}
/* replace terminating end of line marker(s) with null */
void
rip(char *s)
{
	char *cp = s;

	while (*cp)
		cp++;
	while (--cp >= s && (*cp == '\r' || *cp == '\n'))
		;
	cp[1] = '\0';
}
/* Count the occurrances of 'c' in a buffer */
int
memcnt(const uint8 *buf,uint8 c,int size)
{
	int cnt = 0;
	uint8 *icp;

	while(size != 0){
		int change;

		if((icp = (uint8 *) memchr(buf,c,size)) == NULL)
			break;  /* No more found */
		/* Advance the start of the next search to right after
		 * this character
		 */
		change = (int) (icp - buf + 1);
		buf += change;
		size -= change;
		cnt++;
	}
	return cnt;
}
/* XOR block 'b' into block 'a' */
void
memxor(uint8 *a,uint8 *b,unsigned int n)
{
	while(n-- != 0)
		*a++ ^= *b++;
}

/* Case-insensitive string comparison */

int stricmp(char *s1,char *s2)
{
	while(Xtolower(*s1) == Xtolower(*s2)){
		if(!*s1)
			return 0;
		s1++;
		s2++;
	}
	return Xtolower(*s1) - Xtolower(*s2);
}

int strnicmp(char *a,char *b,size_t n)
{
	char a1=0,b1=0;

	while(n-- != 0 && (a1 = *a++) != '\0' && (b1 = *b++) != '\0'){
		if(a1 == b1)
			continue;       /* No need to convert */
		a1 = Xtolower(a1);
		b1 = Xtolower(b1);
		if(a1 == b1)
			continue;       /* NOW they match! */
		if(a1 > b1)
			return 1;
		if(a1 < b1)
			return -1;
	}
	return 0;
}

/* Host-network conversion routines, replaced on the x86 with
 * assembler code in pcgen.asm
 */

/* Put a long in host order into a char array in network order */
uint8 *
put32(uint8 *cp,int32 x)
{
	*cp++ = (uint8) (x >> 24);
	*cp++ = (uint8) (x >> 16);
	*cp++ = (uint8) (x >>  8);
	*cp++ = (uint8) (x      );
	return cp;
}
/* Put a short in host order into a char array in network order */
uint8 *
put16(uint8 *cp,uint x)
{
	*cp++ = x >> 8;
	*cp++ = x;

	return cp;
}
uint
get16(uint8 *cp)
{
	uint x;

	x = *cp++;
	return (x << 8) | *cp;
}
/* Machine-independent, alignment insensitive network-to-host long conversion */
int32
get32(uint8 *cp)
{
	uint32 rval;

	rval = *cp++;
	rval <<= 8;
	rval |= *cp++;
	rval <<= 8;
	rval |= *cp++;
	rval <<= 8;
	rval |= *cp;

	return (int32) rval;
}
/* Compute int(log2(x)) */
int
ilog2(uint x)
{
	int n = 16;
	for(;n != 0;n--){
		if(x & 0x8000)
			break;
		x <<= 1;
	}
	n--;
	return n;
}

