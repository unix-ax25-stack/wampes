#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/qth.c,v 1.11 1993/10/13 22:31:24 deyke Exp $";
#endif

/* qth: qth, locator, distance, and course computations */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI            3.14159265358979323846
#endif

#define CONFFILE        "/usr/local/lib/qth.conf"
#define RADIUS          6370.0

static char **argv;
static int argc;
static long mylatitude  =  (48L *3600L + 38L *60L + 33L);
static long mylongitude = -( 8L *3600L + 53L *60L + 28L);

#ifdef __hpux
#pragma OPTIMIZE OFF
#endif

/*---------------------------------------------------------------------------*/

static void usage(void)
{
  printf("Usage:    qth <place> [<place>]\n");
  printf("              <place> ::= <locator>\n");
  printf("              <place> ::= <grd> [<min> [<sec>]] east|west\n");
  printf("                          <grd> [<min> [<sec>]] north|south\n");
  printf("\n");
  printf("Examples: qth jn48kp\n");
  printf("          qth ei25e\n");
  printf("          qth 8 53 28 east 48 38 33 north\n");
  printf("          qth jn48aa 9 east 48 30 north\n");
  exit(1);
}

/*---------------------------------------------------------------------------*/

static double safe_acos(double a)
{
  if (a >=  1.0) return 0;
  if (a <= -1.0) return M_PI;
  return acos(a);
}

/*---------------------------------------------------------------------------*/

static double norm_course(double a)
{
  while (a <    0.0) a += 360.0;
  while (a >= 360.0) a -= 360.0;
  return a;
}

/*---------------------------------------------------------------------------*/

static long centervalue(long value, long center, long period)
{
  long range;

  range = period / 2;
  while (value > center + range) value -= period;
  while (value < center - range) value += period;
  return value;
}

/*---------------------------------------------------------------------------*/

static void sec_to_loc(long longitude, long latitude, char *loc)
{
  longitude = 180 * 3600L - longitude;
  latitude  =  90 * 3600L + latitude;
  *loc++ = (char) (longitude / 72000 + 'A'); longitude = longitude % 72000;
  *loc++ = (char) (latitude  / 36000 + 'A'); latitude  = latitude  % 36000;
  *loc++ = (char) (longitude /  7200 + '0'); longitude = longitude %  7200;
  *loc++ = (char) (latitude  /  3600 + '0'); latitude  = latitude  %  3600;
  *loc++ = (char) (longitude /   300 + 'A');
  *loc++ = (char) (latitude  /   150 + 'A');
  *loc   = 0;
}

/*---------------------------------------------------------------------------*/

static void sec_to_qra(long longitude, long latitude, char *qra)
{

  long z;
  static const char table[] = "fedgjchab";

  longitude = -longitude;
  while (longitude < 0) longitude += 26 * 7200L;
  latitude = latitude - 40 * 3600L;
  while (latitude < 0) latitude += 26 * 3600L;
  *qra++ = (char) ((longitude / 7200) % 26 + 'A'); longitude = longitude % 7200;
  *qra++ = (char) ((latitude  / 3600) % 26 + 'A'); latitude  = latitude  % 3600;
  z  = (longitude / 720) + 71; longitude = longitude % 720;
  z -= (latitude  / 450) * 10; latitude  = latitude  % 450;
  *qra++ = (char) (z / 10 + '0');
  *qra++ = (char) (z % 10 + '0');
  *qra++ = table[longitude / 240 + (latitude / 150) * 3];
  *qra   = 0;
}

/*---------------------------------------------------------------------------*/

static void loc_to_sec(char *loc, long *longitude, long *latitude)
{
  char *p;

  for (p = loc; *p; p++)
    if (*p >= 'a' && *p <= 'z') *p -= 32;

  if (loc[0] < 'A' || loc[0] > 'R' ||
      loc[1] < 'A' || loc[1] > 'R' ||
      loc[2] < '0' || loc[2] > '9' ||
      loc[3] < '0' || loc[3] > '9' ||
      loc[4] < 'A' || loc[4] > 'X' ||
      loc[5] < 'A' || loc[5] > 'X' ||
      loc[6]) usage();

  *longitude =  180 * 3600L
	       - 20 * 3600L * (loc[0] - 'A')
	       -  2 * 3600L * (loc[2] - '0')
	       -  5 *   60L * (loc[4] - 'A')
	       -       150L;

  *latitude  = - 90 * 3600L
	       + 10 * 3600L * (loc[1] - 'A')
	       +      3600L * (loc[3] - '0')
	       +       150L * (loc[5] - 'A')
	       +        75L;
}

/*---------------------------------------------------------------------------*/

static void qra_to_sec(char *qra, long *longitude, long *latitude)
{

  static const long ltab[] = {
    240L, 480L, 480L, 480L, 240L, 0L,   0L,   0L, 0L, 240L
  };
  static const long btab[] = {
    300L, 300L, 150L,   0L,   0L, 0L, 150L, 300L, 0L, 150L
  };

  char *p;
  long z;

  for (p = qra; *p; p++)
    if (*p >= 'a' && *p <= 'z') *p -= 32;

  if (qra[0] < 'A' || qra[0] > 'Z' ||
      qra[1] < 'A' || qra[1] > 'Z' ||
      qra[2] < '0' || qra[2] > '8' ||
      qra[3] < '0' || qra[3] > '9' ||
      qra[4] < 'A' || qra[4] > 'J' || qra[4] == 'I' ||
      qra[5]) usage();

  z = 10 * (qra[2] - '0') + qra[3] - '0';
  if (z < 1 || z > 80) usage();

  *longitude = - (qra[0] - 'A') * 7200L
	       - (z - 1) % 10 * 720L
	       - ltab[qra[4] - 'A']
	       - 120L;

  *latitude  =   40 * 3600L
	       + (qra[1] - 'A') * 3600L
	       + (7 - (z - 1) / 10) * 450L
	       + btab[qra[4] - 'A']
	       + 75L;
  *longitude = centervalue(*longitude, mylongitude, 26 * 7200L);
  *latitude  = centervalue(*latitude,  mylatitude,  26 * 3600L);
}

/*---------------------------------------------------------------------------*/

static const char *course_name(double a)
{
  if (a <=  11.25) return "North";
  if (a <=  33.75) return "North-North-East";
  if (a <=  56.25) return "North-East";
  if (a <=  78.75) return "East-North-East";
  if (a <= 101.25) return "East";
  if (a <= 123.75) return "East-South-East";
  if (a <= 146.25) return "South-East";
  if (a <= 168.75) return "South-South-East";
  if (a <= 191.25) return "South";
  if (a <= 213.75) return "South-South-West";
  if (a <= 236.25) return "South-West";
  if (a <= 258.75) return "West-South-West";
  if (a <= 281.25) return "West";
  if (a <= 303.75) return "West-North-West";
  if (a <= 326.25) return "North-West";
  if (a <= 348.75) return "North-North-West";
  if (a <= 371.25) return "North";
  return "???";
}

/*---------------------------------------------------------------------------*/

static int get_int(const char *s, int lower, int upper)
{
  int i;

  if (!sscanf(s, "%d", &i)) usage();
  if (i < lower || i > upper) usage();
  return i;
}

/*---------------------------------------------------------------------------*/

static void print_qth(const char *prompt, long longitude, long latitude, const char *loc, const char *qra)
{
  char *pl, *pb;

  if (longitude < 0) { pl = "East"; longitude = -longitude; }
  else                 pl = "West";
  if (latitude  < 0) { pb = "South"; latitude = -latitude; }
  else                 pb = "North";
  printf("%s%3ld %2ld' %2ld\" %s  %3ld %2ld' %2ld\" %s  -->  %s = %s\n",
	 prompt,
	 longitude / 3600,
	 longitude / 60 % 60,
	 longitude % 60,
	 pl,
	 latitude / 3600,
	 latitude / 60 % 60,
	 latitude % 60,
	 pb,
	 loc,
	 qra);
}

/*---------------------------------------------------------------------------*/

static int parse_arg(long *longitude, long *latitude)
{
  int c;

  if (! *argv) return -1;

  if (isalpha(*argv[0])) {
    switch (strlen(*argv)) {
    case 5:
      qra_to_sec(*argv, longitude, latitude);
      break;
    case 6:
      loc_to_sec(*argv, longitude, latitude);
      break;
    default:
      usage();
      break;
    }
    argv++;
    return 0;
  }

  *longitude = 3600L * get_int(*argv, 0, 179);
  argv++;
  if (*argv && isdigit(*argv[0])) {
    *longitude += 60L * get_int(*argv, 0, 59);
    argv++;
    if (*argv && isdigit(*argv[0])) {
      *longitude += get_int(*argv, 0, 59);
      argv++;
    }
  }
  if (! *argv) usage();
  c = *argv[0];
  if (c == 'E' || c == 'e') *longitude = - *longitude;
  else if (c != 'W' && c != 'w') usage();
  argv++;

  if (! *argv) usage();
  *latitude = 3600L * get_int(*argv, 0, 89);
  argv++;
  if (*argv && isdigit(*argv[0])) {
    *latitude += 60L * get_int(*argv, 0, 59);
    argv++;
    if (*argv && isdigit(*argv[0])) {
      *latitude += get_int(*argv, 0, 59);
      argv++;
    }
  }
  if (! *argv) usage();
  c = *argv[0];
  if (c == 'S' || c == 's') *latitude = - *latitude;
  else if (c != 'N' && c != 'n') usage();
  argv++;

  return 0;
}

/*---------------------------------------------------------------------------*/

int main(int pargc, char **pargv)
{

  char loc1[7];
  char loc2[7];
  char qra1[6];
  char qra2[6];
  double a1;
  double a2;
  double b1;
  double b2;
  double e;
  double l1;
  double l2;
  int two_is_me;
  long latitude1;
  long latitude2;
  long longitude1;
  FILE * fp;
  long longitude2;

  if (fp = fopen(CONFFILE, "r")) {
    if (fscanf(fp, "%ld %ld", &longitude1, &latitude1) == 2) {
      mylongitude = longitude1;
      mylatitude = latitude1;
    }
    fclose(fp);
  }

  argc = --pargc;
  argv = ++pargv;

  if (parse_arg(&longitude1, &latitude1)) usage();
  sec_to_loc(longitude1, latitude1, loc1);
  sec_to_qra(longitude1, latitude1, qra1);

  two_is_me = 0;
  if (parse_arg(&longitude2, &latitude2)) {
    longitude2 = mylongitude;
    latitude2  = mylatitude;
    two_is_me = 1;
  }
  sec_to_loc(longitude2, latitude2, loc2);
  sec_to_qra(longitude2, latitude2, qra2);

  if (*argv) usage();

  l1 = longitude1 / 648000.0 * M_PI;
  l2 = longitude2 / 648000.0 * M_PI;
  b1 = latitude1  / 648000.0 * M_PI;
  b2 = latitude2  / 648000.0 * M_PI;

  e = safe_acos(sin(b1) * sin(b2) + cos(b1) * cos(b2) * cos(l2-l1));

  if (!e)
    print_qth("qth:  ", longitude1, latitude1, loc1, qra1);
  else {
    if (two_is_me) {
      print_qth("your qth:       ", longitude1, latitude1, loc1, qra1);
      print_qth(" my  qth:       ", longitude2, latitude2, loc2, qra2);
    }
    else {
      print_qth("1st  qth:       ", longitude1, latitude1, loc1, qra1);
      print_qth("2nd  qth:       ", longitude2, latitude2, loc2, qra2);
    }

    printf("distance:       %.1f km = %.1f miles\n", e * RADIUS, e * RADIUS / 1.609344);

    a1 = safe_acos((sin(b2) - sin(b1) * cos(e)) / sin(e) / cos(b1)) / M_PI * 180.0;
    a2 = safe_acos((sin(b1) - sin(b2) * cos(e)) / sin(e) / cos(b2)) / M_PI * 180.0;

    if (l2 > l1) a1 = 360.0 - a1;
    if (l1 > l2) a2 = 360.0 - a2;

    a1 = norm_course(a1);
    a2 = norm_course(a2);

    if (two_is_me) {
      printf("course you->me: %3.0f (%s)\n", a1, course_name(a1));
      printf("course me->you: %3.0f (%s)\n", a2, course_name(a2));
    } else {
      printf("course 1 --> 2: %3.0f (%s)\n", a1, course_name(a1));
      printf("course 2 --> 1: %3.0f (%s)\n", a2, course_name(a2));
    }
  }
  return 0;
}

