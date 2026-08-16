/* Route filters for NET/ROM and FlexNet - see routefilter.c. */

#ifndef _ROUTEFILTER_H
#define _ROUTEFILTER_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* What we take from a station into our own tables. */

enum rf_in {
	RF_IN_ALL,              /* everything he announces - as it always was */
	RF_IN_ONLY_HIM,         /* only him; nothing he announces about others */
	RF_IN_NONE              /* not even him */
};

/* The two protocols are configured apart, because a station may well be a
 * NET/ROM neighbour and a FlexNet user at the same time.
 */

enum rf_proto {
	RF_NETROM,
	RF_FLEXNET
};

const char *rf_in_name(enum rf_in in);
enum rf_in rf_in(enum rf_proto proto, const uint8 *call, const struct iface *ifp);
int rf_advert(enum rf_proto proto, const uint8 *call, const struct iface *ifp);
int rf_configured(enum rf_proto proto);
int rf_cmd(enum rf_proto proto, int argc, char *argv[], void *p);

#endif /* _ROUTEFILTER_H */
