/* qth: qth, locator, distance, course computations */

static char sccsid[] = "@(#) qth.c   1.7   87/09/13 19:54:50";

#include <ctype.h>
#include <math.h>
#include <string.h>

#define MYBREITE  (48l * 3600l + 38l * 60l + 33l)
#define MYLAENGE -( 8l * 3600l + 53l * 60l + 28l)
#define RADIUS   6370.0

static char **argv;
static int argc;

/*---------------------------------------------------------------------------*/

static void usage()
{
  extern void exit();

  printf("usage:    qth <place> [<place>]\n");
  printf("              <place> ::= <locator>\n");
  printf("              <place> ::= <grd> [<min> [<sec>]] east|west\n");
  printf("                          <grd> [<min> [<sec>]] north|south\n");
  printf("\n");
  printf("examples: qth jn48kp\n");
  printf("          qth ei25e\n");
  printf("          qth 8 53 28 east 48 38 33 north\n");
  printf("          qth jn48aa 9 east 48 30 north\n");
  exit(1);
}

/*---------------------------------------------------------------------------*/

static double safe_acos(a)
double a;
{
  if (a >=  1.0) return 0;
  if (a <= -1.0) return M_PI;
  return acos(a);
}

/*---------------------------------------------------------------------------*/

static double norm_course(a)
double a;
{
  while (a <    0.0) a += 360.0;
  while (a >= 360.0) a -= 360.0;
  return a;
}

/*---------------------------------------------------------------------------*/

static long centervalue(value, center, period)
long value, center, period;
{
  long range;

  range = period / 2;
  while (value > center + range) value -= period;
  while (value < center - range) value += period;
  return value;
}

/*---------------------------------------------------------------------------*/

static void sec_to_loc(laenge, breite, loc)
long laenge, breite;
char *loc;
{
  laenge = 180 * 3600l - laenge;
  breite =  90 * 3600l + breite;
  *loc++ = laenge / 72000 + 'A';   laenge = laenge % 72000;
  *loc++ = breite / 36000 + 'A';   breite = breite % 36000;
  *loc++ = laenge /  7200 + '0';   laenge = laenge %  7200;
  *loc++ = breite /  3600 + '0';   breite = breite %  3600;
  *loc++ = laenge /   300 + 'A';
  *loc++ = breite /   150 + 'A';
  *loc   =                  '\0';
}

/*---------------------------------------------------------------------------*/

static void sec_to_qra(laenge, breite, qra)
long laenge, breite;
char *qra;
{
  long z;
  static char table[] = "fedgjchab";

  laenge = -laenge;
  while (laenge < 0) laenge += 26 * 7200l;
  breite = breite - 40 * 3600l;
  while (breite < 0) breite += 26 * 3600l;
  *qra++ = (laenge / 7200) % 26 + 'A';   laenge = laenge % 7200;
  *qra++ = (breite / 3600) % 26 + 'A';   breite = breite % 3600;
  z  = (laenge / 720) + 71;   laenge = laenge % 720;
  z -= (breite / 450) * 10;   breite = breite % 450;
  *qra++ = z / 10 + '0';
  *qra++ = z % 10 + '0';
  *qra++ = table[laenge / 240 + (breite / 150) * 3];
  *qra   = '\0';
}

/*---------------------------------------------------------------------------*/

static void loc_to_sec(loc, laenge, breite)
char *loc;
long *laenge, *breite;
{

  char *p;

  for (p=loc; *p; p++)
    if (*p >= 'a' && *p <= 'z') *p -= 32;

  if (loc[0] < 'A' || loc[0] > 'R' ||
      loc[1] < 'A' || loc[1] > 'R' ||
      loc[2] < '0' || loc[2] > '9' ||
      loc[3] < '0' || loc[3] > '9' ||
      loc[4] < 'A' || loc[4] > 'X' ||
      loc[5] < 'A' || loc[5] > 'X' ||
      loc[6]) usage();

  *laenge =  180 * 3600l
	    - 20 * 3600l * (loc[0] - 'A')
	    -  2 * 3600l * (loc[2] - '0')
	    -  5 *   60l * (loc[4] - 'A')
	    -       150l;

  *breite = - 90 * 3600l
	    + 10 * 3600l * (loc[1] - 'A')
	    +      3600l * (loc[3] - '0')
	    +       150l * (loc[5] - 'A')
	    +        75l;
}

/*---------------------------------------------------------------------------*/

static void qra_to_sec(qra, laenge, breite)
char *qra;
long *laenge, *breite;
{
  char *p;
  long z;
  static ltab[] = {240l, 480l, 480l, 480l, 240l, 0l,   0l,   0l, 0l, 240l};
  static btab[] = {300l, 300l, 150l,   0l,   0l, 0l, 150l, 300l, 0l, 150l};

  for (p=qra; *p; p++)
    if (*p >= 'a' && *p <= 'z') *p -= 32;

  if (qra[0] < 'A' || qra[0] > 'Z' ||
      qra[1] < 'A' || qra[1] > 'Z' ||
      qra[2] < '0' || qra[2] > '8' ||
      qra[3] < '0' || qra[3] > '9' ||
      qra[4] < 'A' || qra[4] > 'J' || qra[4] == 'I' ||
      qra[5]) usage();

  z = 10 * (qra[2] - '0') + qra[3] - '0';
  if (z < 1 || z > 80) usage();

  *laenge = - (qra[0] - 'A') * 7200l
	    - (z - 1) % 10 * 720l
	    - ltab[qra[4] - 'A']
	    - 120l;

  *breite =   40 * 3600l
	    + (qra[1] - 'A') * 3600l
	    + (7 - (z - 1) / 10) * 450l
	    + btab[qra[4] - 'A']
	    + 75l;
  *laenge = centervalue(*laenge, MYLAENGE, 26 * 7200l);
  *breite = centervalue(*breite, MYBREITE, 26 * 3600l);
}

/*---------------------------------------------------------------------------*/

static char *course_name(a)
double a;
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

static int get_int(s, lower, upper)
char *s;
int lower, upper;
{
  int i;

  if (!sscanf(s, "%d", &i)) usage();
  if (i < lower || i > upper) usage();
  return i;
}

/*---------------------------------------------------------------------------*/

static void print_qth(prompt, laenge, breite, loc, qra)
char *prompt;
long laenge, breite;
char *loc, *qra;
{
  char *pl, *pb;

  if (laenge < 0) { pl = "East"; laenge = -laenge; }
  else              pl = "West";
  if (breite < 0) { pb = "South"; breite = -breite; }
  else              pb = "North";
  printf("%s%3ld %2ld' %2ld\" %s  %3ld %2ld' %2ld\" %s  -->  %s = %s\n",
	 prompt,
	 laenge / 3600,
	 laenge / 60 % 60,
	 laenge % 60,
	 pl,
	 breite / 3600,
	 breite / 60 % 60,
	 breite % 60,
	 pb,
	 loc,
	 qra);
}

/*---------------------------------------------------------------------------*/

static int parse_arg(laenge, breite)
long *laenge, *breite;
{
  int c;

  if (! *argv) return -1;

  if (isalpha(*argv[0])) {
    switch (strlen(*argv)) {
    case 5:
      qra_to_sec(*argv, laenge, breite);
      break;
    case 6:
      loc_to_sec(*argv, laenge, breite);
      break;
    default:
      usage();
      break;
    }
    argv++;
    return 0;
  }

  *laenge = 3600l * get_int(*argv, 0, 179);
  argv++;
  if (*argv && isdigit(*argv[0])) {
    *laenge += 60l * get_int(*argv, 0, 59);
    argv++;
    if (*argv && isdigit(*argv[0])) {
      *laenge += get_int(*argv, 0, 59);
      argv++;
    }
  }
  if (! *argv) usage();
  c = *argv[0];
  if (c == 'E' || c == 'e') *laenge = - *laenge;
  else if (c != 'W' && c != 'w') usage();
  argv++;

  if (! *argv) usage();
  *breite = 3600l * get_int(*argv, 0, 89);
  argv++;
  if (*argv && isdigit(*argv[0])) {
    *breite += 60l * get_int(*argv, 0, 59);
    argv++;
    if (*argv && isdigit(*argv[0])) {
      *breite += get_int(*argv, 0, 59);
      argv++;
    }
  }
  if (! *argv) usage();
  c = *argv[0];
  if (c == 'S' || c == 's') *breite = - *breite;
  else if (c != 'N' && c != 'n') usage();
  argv++;

  return 0;
}

/*---------------------------------------------------------------------------*/

main(pargc, pargv)
int pargc;
char **pargv;
{

  char loc1[7], loc2[7];
  char qra1[6], qra2[6];
  double a1, a2;
  double b1, b2;
  double e;
  double l1, l2;
  int two_is_me;
  long breite1, breite2;
  long laenge1, laenge2;

  argc = --pargc;
  argv = ++pargv;

  if (parse_arg(&laenge1, &breite1)) usage();
  sec_to_loc(laenge1, breite1, loc1);
  sec_to_qra(laenge1, breite1, qra1);

  two_is_me = 0;
  if (parse_arg(&laenge2, &breite2)) {
    laenge2 = MYLAENGE;
    breite2 = MYBREITE;
    two_is_me = 1;
  }
  sec_to_loc(laenge2, breite2, loc2);
  sec_to_qra(laenge2, breite2, qra2);

  if (*argv) usage();

  l1 = laenge1 / 648000.0 * M_PI;
  l2 = laenge2 / 648000.0 * M_PI;
  b1 = breite1 / 648000.0 * M_PI;
  b2 = breite2 / 648000.0 * M_PI;

  e = safe_acos(sin(b1) * sin(b2) + cos(b1) * cos(b2) * cos(l2-l1));

  if (!e)
    print_qth("qth:  ", laenge1, breite1, loc1, qra1);
  else {
    if (two_is_me) {
      print_qth("your qth:       ", laenge1, breite1, loc1, qra1);
      print_qth(" my  qth:       ", laenge2, breite2, loc2, qra2);
    }
    else {
      print_qth("1st  qth:       ", laenge1, breite1, loc1, qra1);
      print_qth("2nd  qth:       ", laenge2, breite2, loc2, qra2);
    }

    printf("distance:       %.1f km\n", e * RADIUS);

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
