/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/files.h,v 1.1 1990/08/23 17:32:48 deyke Exp $ */

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

void initroot __ARGS((char *root));
