/*
 *
 *  Wireless PAN (802.15.4) daemon for Linux
 *
 *  Copyright (C) 2017 CESAR. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>

#include <sys/socket.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>

#include <ell/ell.h>

#include "lowpan.h"

#define TYPE_6LOWPAN "lowpan"

static struct l_netlink *rtnl = NULL;

static size_t rta_add(void *rta_buf, unsigned short type, uint16_t len,
							const void *data)
{
	unsigned short rta_len = RTA_LENGTH(len);
	struct rtattr *rta = rta_buf;

	memset(RTA_DATA(rta), 0, RTA_SPACE(len));

	rta->rta_len = rta_len;
	rta->rta_type = type;
	memcpy(RTA_DATA(rta), data, len);

	return RTA_SPACE(len);
}

static void rtnl_link_notify(uint16_t type, const void *data, uint32_t len,
							void *user_data)
{
	/*
	 * ip link add link wpan0 name lowpan0 type lowpan
	 * Callback called when virtual link is created or deleted.
	 */
	const struct ifinfomsg *ifi = data;

	if (ifi->ifi_type != ARPHRD_6LOWPAN)
		return;

	switch (type) {
	case RTM_NEWLINK:
		l_info("RTNL_NEWLINK");
		break;
	case RTM_DELLINK:
		l_info("RTM_DELLINK");
		break;
	}
}


static void lowpan_enable_callback(int error, uint16_t type,
						const void *data, uint32_t len,
						void *user_data)
{
	l_info("lowpan enabled error: %d", error);
}

bool lowpan_init(uint32_t ifindex)
{
	struct ifinfomsg *rtmmsg;
	size_t nlmon_type_len = strlen(TYPE_6LOWPAN);
	const char *ifname = "lowpan0";
	unsigned short ifname_len = 0;
	unsigned short link_len = 0;
	void *rta_buf;
	size_t bufsize;
	struct rtattr *linkinfo_rta;
	uint32_t iflink = ifindex;

	l_info("6LoWPAN init");

	if (ifname) {
		ifname_len = strlen(ifname) + 1;
		if (ifname_len < 2 || ifname_len > IFNAMSIZ)
			return false;
	}

	link_len = sizeof(iflink);
	bufsize = NLMSG_LENGTH(sizeof(struct ifinfomsg)) +
		RTA_SPACE(link_len) +
		RTA_SPACE(ifname_len) + RTA_SPACE(0) +
		RTA_SPACE(nlmon_type_len);

	rtnl = l_netlink_new(NETLINK_ROUTE);
	if (!rtnl) {
		l_error("Failed to open netlink route socket");
		return false;
	}

	if (!l_netlink_register(rtnl, RTNLGRP_LINK,
				rtnl_link_notify, NULL, NULL)) {
		l_error("Failed to register RTNL link notifications");
		l_netlink_destroy(rtnl);
		return false;
	}

	/* Enable lowpan0 interface */
	rtmmsg = l_malloc(bufsize);
	memset(rtmmsg, 0, bufsize);

	rtmmsg->ifi_family = AF_UNSPEC;
	rtmmsg->ifi_change = 0;
	rtmmsg->ifi_index = ifindex;
	rtmmsg->ifi_type = ARPHRD_NETROM;
	rtmmsg->ifi_flags = 0;

	rta_buf = rtmmsg + 1;

	rta_buf += rta_add(rta_buf, IFLA_LINK, link_len, &iflink);
	if (ifname)
		rta_buf += rta_add(rta_buf, IFLA_IFNAME, ifname_len, ifname);

	linkinfo_rta = rta_buf;

	rta_buf += rta_add(rta_buf, IFLA_LINKINFO, 0, NULL);
	rta_buf += rta_add(rta_buf, IFLA_INFO_KIND,
			   nlmon_type_len, TYPE_6LOWPAN);

	linkinfo_rta->rta_len = rta_buf - (void *) linkinfo_rta;

	l_netlink_send(rtnl, RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL,
		       rtmmsg, rta_buf - (void *) rtmmsg,
		       lowpan_enable_callback, NULL, NULL);

	l_free(rtmmsg);

	return true;
}

void lowpan_exit(void)
{
	l_info("6LoWPAN exit");

	l_netlink_destroy(rtnl);
}
