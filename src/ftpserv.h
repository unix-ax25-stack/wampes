/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftpserv.h,v 1.3 1996/08/11 18:16:09 deyke Exp $ */

#ifndef _FTPSERV_H
#define _FTPSERV_H

#include <stdio.h>

#ifndef _FTP_H
#include "ftp.h"
#endif

/* FTP commands */
enum ftp_cmd {
	USER_CMD,
	ACCT_CMD,
	PASS_CMD,
	TYPE_CMD,
	LIST_CMD,
	CWD_CMD,
	DELE_CMD,
	NAME_CMD,
	QUIT_CMD,
	RETR_CMD,
	STOR_CMD,
	PORT_CMD,
	NLST_CMD,
	PWD_CMD,
	XPWD_CMD,
	MKD_CMD,
	XMKD_CMD,
	XRMD_CMD,
	RMD_CMD,
	STRU_CMD,
	MODE_CMD,
	SYST_CMD,
	XMD5_CMD,
	APPE_CMD,
	CDUP_CMD,
	HELP_CMD,
	MDTM_CMD,
	NOOP_CMD,
	REST_CMD,
	SIZE_CMD,
	XCUP_CMD,
	XCWD_CMD
};

#endif  /* _FTPSERV_H */
