#ifndef __lint
static const char rcsid[] = "@(#) $Id: qth.c,v 1.23 2006/02/12 17:49:57 dl9sau Exp $";
#endif

/* qth: qth, locator, distance, and course computations */

/* This is a work around for the -pedantic problem with math.h on RedHat Linux 6.0 */
#define __OPTIMIZE_SIZE__ 1

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined linux || defined __MACOSX__
#include <getopt.h>
#endif

#ifndef M_PI
#define M_PI            3.14159265358979323846
#endif

#define CONFFILE        "/usr/local/lib/qth.conf"
#define RADIUS          6370.0

static char **argv;
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
  printf("              <place> ::= <gauss-krueger-coordinate>\n");
  printf("                          mmmmmm[m]hhhhhh[h]\n");
  printf("                          7*m+7*h: normal; 6*m+6*h: O2 (ex viag)\n");
  printf("              [-nat] [-h value] [-d arg] <place> [arg2 [..]]\n");
  printf("               -n: nmea compatible output.\n");
  printf("                     [arg2 [arg3]] will be head/tail.\n");
  printf("                     if present, a GPS checkum is automaticaly added.\n");
  printf("               -a: nmea $GPGGA header. arg2 may be user specified suffix\n");
  printf("               -t: nmea timestamp for $GPGGA\n");
  printf("               -i: stamp $GPGGA information as invalid\n");
  printf("               -h: give height above sea [and metric], for e.g. 100M\n");
  printf("               -d: $GPGGA horizontal precision, for e.g. 1.5\n");
  printf("\n");
  printf("Examples: qth jn48kp\n");
  printf("          qth ei25e\n");
  printf("          qth 8 53 28 east 48 38 33 north\n");
  printf("          qth 8 53.47 east 48.642 north\n");
  printf("          qth jn48aa 9 east 48 30 north\n");
  printf("          qth 345827543286\n");
  printf("          qth 34582705432860\n");
  printf("          qth -n jo33sm\n");
  printf("          qth -nat 34582705432860\n");
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

  if (!sscanf((char *) s, "%d", &i)) usage();
  if (i < lower || i > upper) usage();
  return i;
}

/*---------------------------------------------------------------------------*/

static float get_float(const char *s)
{
  float f;

  if (!sscanf((char *) s, "%f", &f)) usage();
  return f;
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

void print_nmea(long longitude, long latitude, char *nmea_head, char *nmea_tail)
{

  char buf[256];
  unsigned char crc = 0x00;
  char c_nw = 'N';
  char c_we = 'W';

#define do_crc(s) { \
	{ char *p = s; \
	  while (*p) \
	    crc ^= *p++; \
	} \
}

  if (nmea_head) {
    int len;
    if (*nmea_head == '$')
      nmea_head++;
    do_crc(nmea_head);
    printf("$%s", nmea_head);
    len = strlen(nmea_head);
    if (len > 1 && nmea_head[len-1] != ',') {
      do_crc(",");
      putchar(',');
    }
  }

  if (latitude < 0) {
    latitude *= -1;
    c_nw = 'S';
  }
  if (longitude < 0) {
    longitude *= -1;
    c_we = 'E';
  }
  sprintf(buf, "%2.2d%2.2d.%3.3d,%c,%3.3d%2.2d.%3.3d,%c",
	(int) latitude / 3600,
	(int) latitude / 60 % 60,
	(int) latitude % 60 * 1000 / 60,
	c_nw,
	(int) longitude / 3600,
	(int) longitude / 60 % 60,
	(int) longitude % 60 * 1000 / 60,
	c_we);

  do_crc(buf);
  printf("%s", buf);

  if (nmea_tail && *nmea_tail) {
	char *p;
        if (*nmea_tail != ',') {
          do_crc(",");
          putchar(',');
	}
	if ((p = strchr(nmea_tail, '*')))
	  *p = 0;
	do_crc(nmea_tail);
	/* nmea_tail can be argv[0] - see do_nmea() - so it is data, not a
	 * format.  "qth -a '%n'" used to write through this. */
	printf("%s", nmea_tail);
  }
  if (nmea_head || nmea_tail) {
    // print crc
    printf("*%2.2X", crc);
  }

  putchar('\n');

}

/*---------------------------------------------------------------------------*/

void gauss_krueger_to_potsdam(char *s_long, char *s_lat, double *d_long, double *d_lat)
{
  long l_long = atol(s_long);
  long l_lat = atol(s_lat);

  int bm    = l_lat/1000000;
  long y    = l_lat-(bm*1000000+500000);
  double si = l_long/111120.6196;
  double px = si+0.143885358*sin(2*si*0.017453292)+0.00021079*sin(4*si*0.017453292)+0.000000423*sin(6*si*0.017453292);
  double t  = (sin(px*0.017453292))/(cos(px*0.017453292));
  double v  = sqrt(1+0.006719219*cos(px*0.017453292)*cos(px*0.017453292));
  double ys = (y*v)/6398786.85;
  double dl  = ys*57.29577/cos(px*0.017453292) * (1-ys*ys/6*(v*v+2*t*t-ys*ys*(0.6+1.1*t*t)*(0.6+1.1*t*t)));

  *d_lat = px-ys*ys*57.29577*t*v*v*(0.5-ys*ys*(4.97-3*t*t)/24);
  *d_long = bm*3+dl;
}

/*---------------------------------------------------------------------------*/

void potsdam_to_wgs84(double *d_long, double *d_lat)
{
  double potsd_a  = 6377397.155;
  double wgs84_a  = 6378137.0;
  double potsd_f  = 1/299.152812838;
  double wgs84_f  = 1/298.257223563;

  double potsd_es = 2*potsd_f - potsd_f*potsd_f;

  double potsd_dx = 606.0;
  double potsd_dy = 23.0;
  double potsd_dz = 413.0;
  double latr = *d_lat/180*M_PI;
  double lonr = *d_long/180*M_PI;

  double sa = sin(latr);
  double ca = cos(latr);
  double so = sin(lonr);
  double co = cos(lonr);

  double bda  = 1-potsd_f;

  double delta_a = wgs84_a - potsd_a;
  double delta_f = wgs84_f - potsd_f;

  double rn = potsd_a / sqrt(1-potsd_es*sin(latr)*sin(latr));
  double rm = potsd_a * ((1-potsd_es)/sqrt(1-potsd_es*sin(latr)*sin(latr)*1-potsd_es*sin(latr)*sin(latr)*1-potsd_es*sin(latr)*sin(latr)));

  double ta = (-potsd_dx*sa*co - potsd_dy*sa*so)+potsd_dz*ca;
  double tb = delta_a*((rn*potsd_es*sa*ca)/potsd_a);
  double tc = delta_f*(rm/bda+rn*bda)*sa*ca;
  double dlat = (ta+tb+tc)/rm;

  double dlon = (-potsd_dx*so + potsd_dy*co)/(rn*ca);

  *d_lat  = (latr + dlat)*180/M_PI;
  *d_long = (lonr + dlon)*180/M_PI;

}

/*---------------------------------------------------------------------------*/

static int parse_arg(long *longitude, long *latitude)
{
  int c;
  int len;
  char *q;

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

  // gauss-krueger (len 14: normal, len 12: O2)
  if ((len = strlen(argv[0])) == 12 || len == 14) {
    char s_lat[8], s_long[8];
    double d_long, d_lat;
    char *p;
    // only numeric args
    for (p = argv[0]; *p; p++) {
      if (*p < '0' || *p > '9') {
	return -1;
      }
    }
    strncpy(s_lat, argv[0], (len == 12 ? 6 : 7));
    if (len == 12)
      s_lat[6] = '0';
    s_lat[7] = 0;
    strncpy(s_long, argv[0]+(len == 12 ? 6 : 7), (len == 12 ? 6 : 7));
    if (len == 12)
      s_long[6] = '0';
    s_long[7] = 0;
    gauss_krueger_to_potsdam(s_long, s_lat, &d_long, &d_lat);
    //potsdam_to_wgs84(&d_long, &d_lat);
    *latitude = d_lat * 3600.0;
    *longitude = -d_long * 3600.0;
    argv++;
    return 0;
  }

  *longitude = 3600L * get_int(*argv, 0, 179);
  if ((q = strchr(*argv, '.'))) {
    *longitude += 3600.0 * get_float(q);
    argv++;
  } else {
    argv++;
    if (*argv && isdigit(*argv[0])) {
      *longitude += 60L * get_int(*argv, 0, 59);
      if ((q = strchr(*argv, '.'))) {
        *longitude += 60.0 * get_float(q);
        argv++;
      } else {
        argv++;
        if (*argv && isdigit(*argv[0])) {
          *longitude += get_int(*argv, 0, 59);
          argv++;
        }
      }
    }
  }
  if (! *argv) usage();
  c = *argv[0];
  if (c == 'E' || c == 'e') *longitude = - *longitude;
  else if (c != 'W' && c != 'w') usage();
  argv++;

  if (! *argv) usage();
  *latitude = 3600L * get_int(*argv, 0, 89);
  if ((q = strchr(*argv, '.'))) {
    *latitude += 3600.0 * get_float(q);
    argv++;
  } else {
    argv++;
    if (*argv && isdigit(*argv[0])) {
      *latitude += 60L * get_int(*argv, 0, 59);
      if ((q = strchr(*argv, '.'))) {
        *latitude += 60.0 * get_float(q);
        argv++;
      } else {
        argv++;
        if (*argv && isdigit(*argv[0])) {
          *latitude += get_int(*argv, 0, 59);
          argv++;
        }
      }
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
/* see GPL gauss converter gauss.pl from Nobert Hüttisch (nobbi@nobbi.com)   */

int do_nmea(int pargc, int do_gpgga, int do_time, int is_invalid, long longitude, long latitude, char *s_height, char *s_hdop)
{
#define	nmea_GPGGA_head "$GPGGA"
#define	nmea_GPGGA_tail ",%d,%2.2d,%s,%5.5d,%s,,,,"
  char s_nmea_head[256];
  char s_nmea_tail[256];
  char *nmea_head = 0;
  char *nmea_tail = 0;

  if (do_gpgga) {
    nmea_head = s_nmea_head;
    if (do_time) {
      time_t t = time(0);
      struct tm *tm = gmtime(&t);
      sprintf(s_nmea_head, "%s,%2.2d%2.2d%2.2d.0,", nmea_GPGGA_head, tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
       sprintf(s_nmea_head, "%s,0,", nmea_GPGGA_head);
    }
    if (pargc > 1)
      nmea_tail = argv[0];
    else {
      char *h_metric = "M";
      int h_value = 0;

      if (s_height) {
	h_value = atoi(s_height);
	while (*s_height && (*s_height < 'A' || *s_height > 'Z'))
	  s_height++;
	h_metric = s_height;
      }
      if (!s_hdop || strpbrk(s_hdop, ", \t\r\n"))
        s_hdop = "1.0";
      sprintf(s_nmea_tail, nmea_GPGGA_tail, is_invalid ? 0 : 1, is_invalid ? 0 : 1, s_hdop, h_value, h_metric);
      nmea_tail = s_nmea_tail;
    }
  } else {
    if (pargc > 1) {
      nmea_head = argv[0];
    if (pargc > 2)
      nmea_tail = argv[1];
    }
  }
  print_nmea(longitude, latitude, nmea_head, nmea_tail);

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
  FILE *fp;
  int two_is_me;
  int c;
  long latitude1;
  long latitude2;
  long longitude1;
  long longitude2;
  int nmea = 0;
  int a_set = 0;
  int t_set = 0;
  int i_set = 0;
  int h_set = 0;
  int d_set = 1;
  char *h_arg = 0;
  char *d_arg = 0;

  if ((fp = fopen(CONFFILE, "r"))) {
    if (fscanf(fp, "%ld %ld", &longitude1, &latitude1) == 2) {
      mylongitude = longitude1;
      mylatitude = latitude1;
    }
    fclose(fp);
  }

  while((c = getopt(pargc,pargv,"d:h:nati")) != EOF) {
    switch (c) {
    case 'n':
      nmea = 1;
      break;
    case 'a':
      a_set = 1;
      break;
    case 't':
      t_set = 1;
      break;
    case 'i':
      i_set = 1;
      break;
    case 'h':
      h_set = 1;
      h_arg = optarg;
      break;
    case 'd':
      d_set = 1;
      d_arg = optarg;
      break;
    default:
      usage();
    }
  }
  pargc -= optind;
  if (nmea) {
    if (pargc < 1 || pargc > 3)
      usage();
  }

  argv = pargv + optind;

  if (parse_arg(&longitude1, &latitude1)) usage();
  if (nmea) {
    int ret = do_nmea(pargc, a_set, t_set, i_set, longitude1, latitude1, h_arg, d_arg);
    exit(ret);
  }

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
