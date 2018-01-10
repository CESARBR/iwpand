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

#include <ell/ell.h>
#include "nl802154.h"
#include "dbus.h"
#include "lowpan.h"
#include "phy.h"

#define ADAPTER_INTERFACE		"net.connman.iwpand.Adapter"

/* User defined settings */
struct channel {
	uint8_t page;
	uint8_t ch;
};

struct wpan {
	uint32_t ifindex;
	char *name;
	bool powered;
	uint16_t panid;
};

static struct l_queue *wpan_list = NULL;
static struct l_genl_family *nl802154 = NULL;

static void wpan_free(void *data)
{
	struct wpan *wpan = data;

	l_free(wpan->name);
	l_free(wpan);
}

static bool property_get_powered(struct l_dbus *dbus,
				     struct l_dbus_message *msg,
				     struct l_dbus_message_builder *builder,
				     void *user_data)
{
	struct wpan *wpan = user_data;

	l_dbus_message_builder_append_basic(builder, 'b', &wpan->powered);
	l_info("GetProperty(Powered = %d)", wpan->powered);

	return true;
}

static struct l_dbus_message *property_set_powered(struct l_dbus *dbus,
					struct l_dbus_message *message,
					struct l_dbus_message_iter *new_value,
					l_dbus_property_complete_cb_t complete,
					void *user_data)
{
	struct wpan *wpan = user_data;
	bool value;

	if (!l_dbus_message_iter_get_variant(new_value, "b", &value))
		return dbus_error_invalid_args(message);

	l_info("SetProperty(Powered = %d)", value);

	if (value == wpan->powered)
		goto done;

	wpan->powered = value;

	if (value == true)
		lowpan_init(wpan->ifindex);
	else
		lowpan_exit();

done:
	complete(dbus, message, NULL);

	return NULL;
}

static bool property_get_name(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct wpan *wpan = user_data;

	l_dbus_message_builder_append_basic(builder, 's', wpan->name);
	l_info("GetProperty(Name = %s)", wpan->name);

	return true;
}

static bool property_get_panid(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct wpan *wpan = user_data;

	l_dbus_message_builder_append_basic(builder, 'q', &wpan->panid);
	l_info("GetProperty(PanId = %d)", wpan->panid);

	return true;
}

static struct l_dbus_message *property_set_panid(struct l_dbus *dbus,
					struct l_dbus_message *message,
					struct l_dbus_message_iter *new_value,
					l_dbus_property_complete_cb_t complete,
					void *user_data)
{
	struct wpan *wpan = user_data;
	struct l_genl_msg *msg;
	uint16_t value;

	if (!l_dbus_message_iter_get_variant(new_value, "q", &value))
		return dbus_error_invalid_args(message);

	l_info("SetProperty(PanId = %d)", value);

	msg = l_genl_msg_new_sized(NL802154_CMD_SET_PAN_ID, 64);
	l_genl_msg_append_attr(msg, NL802154_ATTR_IFINDEX,
					sizeof(wpan->ifindex), &wpan->ifindex);
	l_genl_msg_append_attr(msg, NL802154_ATTR_PAN_ID,
						sizeof(value), &value);

	if (!l_genl_family_send(nl802154, msg, NULL, NULL, NULL)) {
		l_error("NL802154_CMD_SET_PAN_ID failed");
		return dbus_error_invalid_args(message);
	}

	wpan->panid =value;
	complete(dbus, message, NULL);

	return NULL;
}

static void register_property(struct l_dbus_interface *interface)
{
	if (!l_dbus_interface_property(interface, "Powered", 0, "b",
				       property_get_powered,
				       property_set_powered))
		l_error("Can't add 'Powered' property");

	if (!l_dbus_interface_property(interface, "Name", 0, "s",
				       property_get_name,
				       NULL))
		l_error("Can't add 'Name' property");

	if (!l_dbus_interface_property(interface, "PanId", 0, "q",
				       property_get_panid,
				       property_set_panid))
		l_error("Can't add 'PanId' property");
}

static void add_interface(struct wpan *wpan)
{
	char *path;

	path = l_strdup_printf("/%s", wpan->name);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 path,
					 ADAPTER_INTERFACE,
					 wpan))
		l_error("'%s': Unable to register %s interface",
							path,
							ADAPTER_INTERFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 path,
					 L_DBUS_INTERFACE_PROPERTIES,
					 wpan))
		l_error("'%s': Unable to register %s interface",
						path,
						L_DBUS_INTERFACE_PROPERTIES);

	l_free(path);
}

static void get_wpan_phy_callback(struct l_genl_msg *msg, void *user_data)
{
	struct channel *channel = user_data;
	struct l_genl_msg *setup;
	struct l_genl_attr attr;
	uint16_t type, len;
	const void *data;
	uint32_t id;
	uint8_t page = 0xff;
	uint8_t ch = 0xff;

	l_debug("");

	if (!l_genl_attr_init(&attr, msg))
		return;

	while (l_genl_attr_next(&attr, &type, &len, &data)) {
		l_debug("type: %u len:%u", type, len);
		switch (type) {
		case NL802154_ATTR_WPAN_PHY:
			id = *((uint32_t *) data);
			l_debug("  id: %d", id);
			break;
		case NL802154_ATTR_PAGE:
			page = *((uint8_t *) data);
			l_debug("  page: %d", page);
			break;
		case NL802154_ATTR_CHANNEL:
			ch = *((uint8_t *) data);
			l_debug("  channel: %d", ch);
			break;
		}
	}

	/* Malformed netlink message? */
	if (page == 0xff || ch == 0xff)
		return;

	/* Valid command line params? */
	if (channel->page == 0xff || channel->ch == 0xff)
		return;

	if (channel->page == page && channel->ch == ch)
		return;

	/* Change page and channel according to command line params */
	setup = l_genl_msg_new_sized(NL802154_CMD_SET_CHANNEL, 64);
	l_genl_msg_append_attr(setup, NL802154_ATTR_WPAN_PHY, sizeof(id), &id);
	l_genl_msg_append_attr(setup, NL802154_ATTR_PAGE,
			       sizeof(channel->page), &channel->page);
	l_genl_msg_append_attr(setup, NL802154_ATTR_CHANNEL,
			       sizeof(channel->ch), &channel->ch);

	if (!l_genl_family_send(nl802154, setup, NULL, NULL, NULL)) {
		l_error("NL802154_CMD_SET_CHANNEL failed");
		return;
	}
}

static void get_interface_callback(struct l_genl_msg *msg, void *user_data)
{
	struct wpan *wpan;
	struct l_genl_attr attr;
	uint16_t type, len;
	const void *data;

	l_debug("");

	if (!l_genl_attr_init(&attr, msg))
		return;

	wpan = l_new(struct wpan, 1);
	l_queue_push_head(wpan_list, wpan);

	while (l_genl_attr_next(&attr, &type, &len, &data)) {
		l_debug("type: %u len:%u", type, len);
		switch (type) {
		case NL802154_ATTR_IFINDEX:
			wpan->ifindex = *((uint32_t *) data);
			l_debug("  ifindex: %d",  wpan->ifindex);
			break;
		case NL802154_ATTR_IFNAME:
			wpan->name = l_strdup(data);
			l_debug("  name: %s", wpan->name);
			break;
		case NL802154_ATTR_PAN_ID:
			wpan->panid = *((uint16_t *) data);
			l_debug("  PAN ID: %d", wpan->panid);
		}
	}

	add_interface(wpan);
}

bool phy_init(struct l_genl_family *genl, uint8_t page, uint8_t ch)
{
	struct l_genl_msg *msg;
	struct channel *channel;

	channel = l_new(struct channel, 1);
	channel->page = page;
	channel->ch = ch;

	msg = l_genl_msg_new(NL802154_CMD_GET_WPAN_PHY);
	if (!l_genl_family_dump(genl, msg, get_wpan_phy_callback,
						channel, l_free)) {
		l_error("Getting all PHY devices failed");
		return false;
	}

	msg = l_genl_msg_new(NL802154_CMD_GET_INTERFACE);
	if (!l_genl_family_dump(genl, msg, get_interface_callback,
						NULL, NULL)) {
		l_error("Getting all interfaces failed");
		return false;
	}


	if (!l_dbus_register_interface(dbus_get_bus(),
				       ADAPTER_INTERFACE,
				       register_property,
				       NULL, false)) {
		l_error("Unable to register %s interface", ADAPTER_INTERFACE);
		return false;
	}

	wpan_list = l_queue_new();
	nl802154 = genl;

	return true;
}

void phy_exit(struct l_genl_family *genl)
{
	l_queue_destroy(wpan_list, wpan_free);
}
