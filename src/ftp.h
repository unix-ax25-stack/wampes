/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftp.h,v 1.8 1994/10/02 17:55:30 deyke Exp $ */

#ifndef _FTP_H
#define _FTP_H

#include <stdio.h>

/* Definitions common to both FTP servers and clients */
#define ASCII_TYPE      0
#define IMAGE_TYPE      1
#define LOGICAL_TYPE    2

struct ftp {
	struct ftp *prev;       /* Linked list pointers */
	struct ftp *next;

	struct tcb *control;    /* TCP control connection */
	struct tcb *data;       /* Data connection */
	char type;              /* Transfer type */
	int logbsize;           /* Logical byte size for logical type */

	FILE *fp;               /* File descriptor being transferred */
	struct socket port;     /* Remote port for data connection */
	char *username;         /* Arg to USER command */
	char *root;             /* Root directory name */
/*      char perms;             /* Permission flag bits */
				/* (See FILES.H for definitions) */
	char *cd;               /* Current directory name */

	char state;
#define COMMAND_STATE   0       /* Awaiting user command */
#define SENDING_STATE   1       /* Sending data to user */
#define RECEIVING_STATE 2       /* Storing data from user */
	char *buf;              /* Input command buffer */
	char cnt;               /* Length of input buffer */
	int rest;               /* Restart location */
	int uid;                /* User ID */
	int gid;                /* Group ID */
	struct session *session;
};

#define NULLFTP (struct ftp *)0

/* In ftp.c: */
void ftpdr(struct tcb *tcb, int cnt);
void ftpdt(struct tcb *tcb, int cnt);
struct ftp *ftp_create(unsigned bufsize);
void ftp_delete(struct ftp *ftp);

/* In ftpcli.c: */
void ftpccr(struct tcb *tcb, int cnt);

#endif  /* _FTP_H */
