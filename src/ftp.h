/* @(#) $Id: ftp.h,v 1.15 1998/03/09 17:42:56 deyke Exp $ */

#ifndef _FTP_H
#define _FTP_H

#include <stdio.h>

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _TCP_H
#include "tcp.h"
#endif

#ifndef _SESSION_H
#include "session.h"
#endif

/* Definitions common to both FTP servers and clients */
enum ftp_type {
	ASCII_TYPE,
	IMAGE_TYPE,
	LOGICAL_TYPE
};

enum ftp_state {
	COMMAND_STATE,          /* Awaiting user command */
	SENDING_STATE,          /* Sending data to user */
	RECEIVING_STATE         /* Storing data from user */
};

struct ftp {
	struct ftp *prev;       /* Linked list pointers */
	struct ftp *next;

	struct tcb *control;    /* TCP control connection */
	struct tcb *data;       /* Data connection */
	enum ftp_type type;     /* Transfer type */
	int logbsize;           /* Logical byte size for logical type */

	FILE *fp;               /* File descriptor being transferred */
	struct socket port;     /* Remote port for data connection */
	char *username;         /* Arg to USER command */
	char *root;             /* Root directory name */
/*      char perms;                Permission flag bits */
				/* (See FILES.H for definitions) */
	char *cd;               /* Current directory name */

	enum ftp_state state;
	char *buf;              /* Input command buffer */
	int cnt;                /* Length of input buffer */
	int rest;               /* Restart location */
	int uid;                /* User ID */
	int gid;                /* Group ID */
	struct session *session;
};

/* In ftp.c: */
void ftpdr(struct tcb *tcb, int32 cnt);
void ftpdt(struct tcb *tcb, int32 cnt);
struct ftp *ftp_create(unsigned bufsize);
void ftp_delete(struct ftp *ftp);

/* In ftpcli.c: */
void ftpccr(struct tcb *tcb, int32 cnt);

#endif  /* _FTP_H */
