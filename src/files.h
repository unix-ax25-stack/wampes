/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/files.h,v 1.2 1991/02/24 20:16:44 deyke Exp $ */

#ifndef _FILES_H
#define _FILES_H

/* External definitions for configuration-dependent file names set in
 * files.c
 */
extern char *Startup;   /* Initialization file */
extern char *Userfile;  /* Authorized FTP users and passwords */
extern char *Maillog;   /* mail log */
extern char *Mailspool; /* Incoming mail */
extern char *Mailqdir;  /* Outgoing mail spool */
extern char *Mailqueue; /* Outgoing mail work files */
extern char *Routeqdir; /* queue for router */
extern char *Alias;     /* the alias file */
extern char *Dfile;     /* Domain cache */
extern char *Fdir;      /* Finger info directory */
extern char *Arealist;          /* List of message areas */
extern char *Helpdir;           /* Mailbox help file directory */
extern char *Rewritefile;       /* Address rewrite file */
extern char *Newsdir;           /* News messages and NNTP data */
extern char *Signature;         /* Mail signature file directory */
extern char *Forwardfile;       /* Mail forwarding file */
extern char *Historyfile;       /* Message ID history file */
extern char *PPPhosts;          /* peer ID to IP address lookup table */
void initroot __ARGS((char *root));

#endif  /* _FILES_H */
