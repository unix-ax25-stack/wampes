#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char daynames[] = "SunMonTueWedThuFriSat";
static const char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static int stopped;

/*---------------------------------------------------------------------------*/

#define halt() { perror(""); exit(1); }

/*---------------------------------------------------------------------------*/

static long calculate_date(int yy, int mm, int dd,
			 int h, int m, int s,
			 int tzcorrection)
{

 static int mdays[] = {
 31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
 };

 int i;
 long jdate;

 if (yy <= 37)
 yy += 2000;
 if (yy <= 99)
 yy += 1900;
 mdays[1] = 28 + (yy % 4 == 0 && (yy % 100 || yy % 400 == 0));
 mm--;
 if (yy < 1970 || yy > 2037 ||
 mm < 0 || mm > 11 ||
 dd < 1 || dd > mdays[mm] ||
 h < 0 || h > 23 ||
 m < 0 || m > 59 ||
 s < 0 || s > 59)
 return -1;
 jdate = dd - 1;
 for (i = 0; i < mm; i++)
 jdate += mdays[i];
 for (i = 1970; i < yy; i++)
 jdate += 365 + (i % 4 == 0);
 jdate *= (24L * 60L * 60L);
 return jdate + 3600 * h + 60 * m + s - tzcorrection;
}

/*---------------------------------------------------------------------------*/

static long parse_rfc822_date(const char *str)
{

 char buf[1024];
 char monthstr[1024];
 char timestr[1024];
 char tzstr[1024];
 char *cp;
 char *p;
 int dd;
 int h;
 int m;
 int s;
 int tzcorrection = 0;
 int yy;

 strcpy(buf, str);

 /* Skip day "," */

 if ((cp = strchr(buf, ',')))
 cp++;
 else
 cp = buf;

 switch (sscanf(cp, "%d %s %d %s %s", &dd, monthstr, &yy, timestr, tzstr)) {
 case 4:
 break;
 case 5:
 if (!strcmp(tzstr, "EDT"))
 strcpy(tzstr, "-0400");
 else if (!strcmp(tzstr, "EST") || !strcmp(tzstr, "CDT"))
 strcpy(tzstr, "-0500");
 else if (!strcmp(tzstr, "CST") || !strcmp(tzstr, "MDT"))
 strcpy(tzstr, "-0600");
 else if (!strcmp(tzstr, "MST") || !strcmp(tzstr, "PDT"))
 strcpy(tzstr, "-0700");
 else if (!strcmp(tzstr, "PST"))
 strcpy(tzstr, "-0800");
 if ((tzstr[0] == '-' || tzstr[0] == '+') &&
	isdigit(tzstr[1] & 0xff) &&
	isdigit(tzstr[2] & 0xff) &&
	isdigit(tzstr[3] & 0xff) &&
	isdigit(tzstr[4] & 0xff) &&
	!tzstr[5]) {
 tzcorrection =
	 (tzstr[1] - '0') * 36000 +
	 (tzstr[2] - '0') * 3600 +
	 (tzstr[3] - '0') * 600 +
	 (tzstr[4] - '0') * 60;
 if (tzstr[0] == '-')
	tzcorrection = -tzcorrection;
 }
 break;
 default:
 return -1;
 }

 switch (sscanf(timestr, "%d:%d:%d", &h, &m, &s)) {
 case 2:
 s = 0;
 break;
 case 3:
 break;
 default:
 return -1;
 }

 if (strlen(monthstr) != 3)
 return -1;
 p = strstr(monthnames, monthstr);
 if (!p)
 return -1;
 return calculate_date(yy, (p - monthnames) / 3 + 1, dd,
			h, m, s, tzcorrection);
}

/*---------------------------------------------------------------------------*/

static char *rfc822_date_string(long gmt)
{

 static char buf[32];
 struct tm *tm;

 tm = gmtime(&gmt);
 sprintf(buf, "%.3s, %d %.3s %02d %02d:%02d:%02d GMT",
	 daynames + 3 * tm->tm_wday,
	 tm->tm_mday,
	 monthnames + 3 * tm->tm_mon,
	 tm->tm_year % 100,
	 tm->tm_hour,
	 tm->tm_min,
	 tm->tm_sec);
 return buf;
}

/*---------------------------------------------------------------------------*/

#define NEWSGROUPSPREFIX "ampr.bbs."
#define NEWSGROUPSPREFIXLENGTH 9        /* strlen(NEWSGROUPSPREFIX) */

#define NEWS_DIR "/usr/spool/news"

int main()
{

  char line[8 * 1024];
  char *cp;
  char *from;
  char *subject;
  char *token;
  FILE *fp;
  int i;
  long date;

  strcpy(line, NEWS_DIR "/over.view/");
  for (cp = line; *cp; cp++) ;
  strcat(line, NEWSGROUPSPREFIX);
  strcat(line, "all");          /* ############################ group name */
  strcat(line, "/overview");
  for (; *cp; cp++) {
    if (*cp == '.')
      *cp = '/';
  }
  fp = fopen(line, "r");
  if (!fp)
    halt();
  while (!stopped && fgets(line, sizeof(line), fp)) {
    if (!(token = strtok(line, "\t")))
      continue;
    i = atoi(token);
#if 0
    if (i < current_group->low || i > current_group->high)
      continue;
#endif
    if (!(subject = strtok(0, "\t")))
      continue;
    if (!(from = strtok(0, "\t")))
      continue;
    if (!(token = strtok(0, "\t")))
      continue;
    if ((date = parse_rfc822_date(token)) < 0)
      continue;
#if 1
    printf("Msg#: %d ", i);
    printf("Subject: %s ", subject);
    printf("From: %s ", from);
    printf("Date: %s\n", rfc822_date_string(date));
#endif
  }
  fclose(fp);
  return 0;
}
