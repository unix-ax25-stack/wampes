/* @(#) $Id: files.c,v 1.14 1996/08/12 18:51:17 deyke Exp $ */

/* System-dependent definitions of various files, spool directories, etc */
#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "netuser.h"
#include "files.h"

#ifdef  MSDOS
char System[] = "MSDOS";
char *Startup = "/autoexec.net";        /* Initialization file */
char *Userfile = "/ftpusers";   /* Authorized FTP users and passwords */
char *Maillog = "/spool/mail.log";      /* mail log */
char *Mailspool = "/spool/mail";        /* Incoming mail */
char *Mailqdir = "/spool/mqueue";               /* Outgoing mail spool */
char *Mailqueue = "/spool/mqueue/*.wrk";        /* Outgoing mail work files */
char *Routeqdir = "/spool/rqueue";              /* queue for router */
char *Alias = "/alias";         /* the alias file */
char *Dfile = "/domain.txt";    /* Domain cache */
char *Fdir = "/finger";         /* Finger info directory */
char *Arealist = "/spool/areas";/* List of message areas */
char *Helpdir = "/spool/help";  /* Mailbox help file directory */
char *Rewritefile = "/spool/rewrite"; /* Address rewrite file */
char *Newsdir = "/spool/news";          /* News messages and NNTP data */
char *Popusers = "/popusers";           /* POP user and passwd file */
char *Signature = "/spool/signatur"; /* Mail signature file directory */
char *Forwardfile = "/spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "/spool/history"; /* Message ID history file */
char *Tmpdir = "/tmp";
char Eol[] = "\r\n";
#define SEPARATOR       "/"
#endif

#ifdef  UNIX
char System[] = "UNIX";
char *Startup = "/tcp/net.rc";          /* Initialization file */
char *Config = "./config.net";  /* Device configuration list */
char *Userfile = "./ftpusers";
char *Mailspool = "./mail";
char *Maillog = "./mail.log";   /* mail log */
char *Mailqdir = "./mqueue";
char *Mailqueue = "./mqueue/*.wrk";
char *Routeqdir = "./rqueue";           /* queue for router */
char *Alias = "./alias";        /* the alias file */
char *Dfile = "./domain.txt";   /* Domain cache */
char *Fdir = "./finger";                /* Finger info directory */
char *Arealist = "./areas";             /* List of message areas */
char *Helpdir = "./help";       /* Mailbox help file directory */
char *Rewritefile = "./rewrite"; /* Address rewrite file */
char *Newsdir = "./news";               /* News messages and NNTP data */
char *Popusers = "./popusers";          /* POP user and passwd file */
char *Signature = "./signatur"; /* Mail signature file directory */
char *Forwardfile = "./forward.bbs"; /* Mail forwarding file */
char *Historyfile = "./history"; /* Message ID history file */
char *Tmpdir = "/tmp";
#define SEPARATOR       "/"
char Eol[] = "\n";
#endif

#ifdef  AMIGA
char System[] = "AMIGA";
char *Startup = "TCPIP:net-startup";
char *Config = "TCPIP:config.net";      /* Device configuration list */
char *Userfile = "TCPIP:ftpusers";
char *Mailspool = "TCPIP:spool/mail";
char *Maillog = "TCPIP:spool/mail.log";
char *Mailqdir = "TCPIP:spool/mqueue";
char *Mailqueue = "TCPIP:spool/mqueue/#?.wrk";
char *Routeqdir = "TCPIP:spool/rqueue";         /* queue for router */
char *Alias = "TCPIP:alias";    /* the alias file */
char *Dfile = "TCPIP:domain.txt";       /* Domain cache */
char *Fdir = "TCPIP:finger";            /* Finger info directory */
char *Arealist = "TCPIP:spool/areas";   /* List of message areas */
char *Helpdir = "TCPIP:spool/help";     /* Mailbox help file directory */
char *Rewritefile = "TCPIP:spool/rewrite"; /* Address rewrite file */
char *Newsdir = "TCPIP:spool/news";     /* News messages and NNTP data */
char *Popusers = "TCPIP:/popusers";     /* POP user and passwd file */
char *Signature = "TCPIP:spool/signatur"; /* Mail signature file directory */
char *Forwardfile = "TCPIP:spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "TCPIP:spool/history"; /* Message ID history file */
Char *Tmpdir = "TCPIP:tmp";
#define SEPARATOR       "/"
char Eol[] = "\r\n";
#endif

#ifdef  MAC
char System[] = "MACOS";
char *Startup ="Mikes Hard Disk:net.start";
char *Config = "Mikes Hard Disk:config.net";    /* Device configuration list */
char *Userfile = "Mikes Hard Disk:ftpusers";
char *Mailspool = "Mikes Hard Disk:spool:mail:";
char *Maillog = "Mikes Hard Disk:spool:mail.log:";
char *Mailqdir = "Mikes Hard Disk:spool:mqueue:";
char *Mailqueue = "Mikes Hard Disk:spool:mqueue:*.wrk";
char *Routeqdir = "Mikes Hard Disk:spool/rqueue:";      /* queue for router */
char *Alias = "Mikes Hard Disk:alias";  /* the alias file */
char *Dfile = "Mikes Hard Disk:domain:txt";     /* Domain cache */
char *Fdir = "Mikes Hard Disk:finger";          /* Finger info directory */
char *Arealist = "Mikes Hard Disk:spool/areas"; /* List of message areas */
char *Helpdir = "Mikes Hard Disk:spool/help"; /* Mailbox help file directory */
char *Rewritefile = "Mikes Hard Disk:spool/rewrite"; /* Address rewrite file */
char *Newsdir = "Mikes Hard Disk:spool/news"; /* News messages and NNTP data */
char *Popusers = "Mikes Hard Disk:/popusers";   /* POP user and passwd file */
char *Signature = "Mikes Hard Disk:spool/signatur"; /* Mail signature file directory */
char *Forwardfile = "Mikes Hard Disk:spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "Mikes Hard Disk:spool/history"; /* Message ID history file */
Char *Tmpdir = "Mikes Hard Disk:tmp";
#define SEPARATOR       "/"
char Eol[] = "\r";
#endif

static char *rootdir = "";

/* Establish a root directory other than the default. Can only be called
 * once, at startup time
 */
void
initroot(
char *root)
{
	rootdir = strdup( root );

	Startup = rootdircat(Startup);
	Userfile = rootdircat(Userfile);
	Maillog = rootdircat(Maillog);
	Mailspool = rootdircat(Mailspool);
	Mailqdir = rootdircat(Mailqdir);
	Mailqueue = rootdircat(Mailqueue);
	Routeqdir = rootdircat(Routeqdir);
	Alias = rootdircat(Alias);
	Dfile = rootdircat(Dfile);
	Fdir = rootdircat(Fdir);
	Arealist = rootdircat(Arealist);
	Helpdir = rootdircat(Helpdir);
	Rewritefile = rootdircat(Rewritefile);
	Newsdir = rootdircat(Newsdir);
	Signature = rootdircat(Signature);
	Forwardfile = rootdircat(Forwardfile);
	Historyfile = rootdircat(Historyfile);
}

/* Concatenate root, separator and arg strings into a malloc'ed output
 * buffer, then remove repeated occurrences of the separator char
 */
char *
rootdircat(
char *filename)
{
	char *out = filename;

	if ( strlen(rootdir) > 0 ) {
		char *separator = SEPARATOR;

		out = (char *) mallocw( strlen(rootdir)
				+ strlen(separator)
				+ strlen(filename) + 1);

		strcpy(out,rootdir);
		strcat(out,separator);
		strcat(out,filename);
		if(*separator != '\0'){
			char *p1, *p2;

			/* Remove any repeated occurrences */
			p1 = p2 = out;
			while(*p2 != '\0'){
				*p1++ = *p2++;
				while(p2[0] == p2[-1] && p2[0] == *separator)
					p2++;
			}
			*p1 = '\0';
		}
	}
	return out;
}

