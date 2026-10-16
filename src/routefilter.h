/* routefilter.h
 * Copyright (C) 2026 Thomas Osterried <dl9sau@darc.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

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

/* The help text for "<proto> filter", one per protocol because it spells the
 * command out.  It lives here so that the command tables in flexnet.c and
 * netrom.c can point at it - the table's usage field is what "filter ?"
 * prints, and the wording should exist once.
 */

extern char Rf_usage_flexnet[];
extern char Rf_usage_netrom[];

const char *rf_in_name(enum rf_in in);
enum rf_in rf_in(enum rf_proto proto, const uint8 *call, const struct iface *ifp);
int rf_advert(enum rf_proto proto, const uint8 *call, const struct iface *ifp);
int rf_feed(enum rf_proto proto, const uint8 *call, const struct iface *ifp);
int rf_configured(enum rf_proto proto);
int rf_cmd(enum rf_proto proto, int argc, char *argv[], void *p);

#endif /* _ROUTEFILTER_H */
