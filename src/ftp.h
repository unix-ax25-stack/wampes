#define CTLZ    26              /* EOF for CP/M systems */

extern char *userfile;  /* List of user names and permissions */

/* Per-session control block */
struct ftp {
	struct ftp *prev;       /* Linked list pointers */
	struct ftp *next;
	struct tcb *control;    /* TCP control connection */
	char state;
#define COMMAND_STATE   0       /* Awaiting user command */
#define SENDING_STATE   1       /* Sending data to user */
#define RECEIVING_STATE 2       /* Storing data from user */

	char type;              /* Transfer type */
#define IMAGE_TYPE      0
#define ASCII_TYPE      1

	FILE *fp;               /* File descriptor being transferred */
	struct socket port;     /* Remote port for data connection */
	struct tcb *data;       /* Data connection */

	/* The following are used only by the server */
	char *username;         /* Arg to USER command */
	char *path;             /* Allowable path prefix */
	char perms;             /* Permission flag bits */
#define FTP_READ        1       /* Read files */
#define FTP_CREATE      2       /* Create new files */
#define FTP_WRITE       4       /* Overwrite or delete existing files */

	char *buf;              /* Input command buffer */
	char cnt;               /* Length of input buffer */
	char *cd;               /* Current directory name */

	int uid;                /* User ID */
	int gid;                /* Group ID */

	/* And this is used only by the client */
	struct session *session;
};

#define NULLFTP (struct ftp *)0

