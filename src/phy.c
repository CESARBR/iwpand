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

#define PHY_INTERFACE		"net.connman.iwpand.Adapter"

struct phy {
	uint32_t id;
	char *name;
	bool powered;
	uint8_t page;
	uint8_t channel;
	uint16_t panid;
};

struct wpan {
	uint32_t id;
	uint16_t panid;
};

static struct l_queue *phy_list = NULL;
static struct l_queue *iface_list = NULL;
static struct l_genl_family *nl802154 = NULL;

static void phy_free(void *data)
{
	struct phy *phy = data;

	l_free(phy->name);
	l_free(phy);
}

static bool phy_property_get_powered(struct l_dbus *dbus,
				     struct l_dbus_message *msg,
				     struct l_dbus_message_builder *builder,
				     void *user_data)
{
	struct phy *phy = user_data;

	l_dbus_message_builder_append_basic(builder, 'b', &phy->powered);
	l_info("GetProperty(Powered = %d)", phy->powered);

	return true;
}

static struct l_dbus_message *phy_property_set_powered(struct l_dbus *dbus,
					struct l_dbus_message *message,
					struct l_dbus_message_iter *new_value,
					l_dbus_property_complete_cb_t complete,
					void *user_data)
{
	struct phy *phy = user_data;
	bool value;

	if (!l_dbus_message_iter_get_variant(new_value, "b", &value))
		return dbus_error_invalid_args(message);

	l_info("SetProperty(Powered = %d)", value);

	if (value == phy->powered)
		goto done;

	phy->powered = value;

	if (value == true)
		lowpan_init();
	else
		lowpan_exit();

done:
	complete(dbus, message, NULL);

	return NULL;
}

static bool phy_property_get_name(struct l_dbus *dbus,
				  struct l_dbus_message *msg,
				  struct l_dbus_message_builder *builder,
				  void *user_data)
{
	struct phy *phy = user_data;

	l_dbus_message_builder_append_basic(builder, 's', phy->name);
	l_info("GetProperty(Name = %s)", phy->name);

	return true;
}

static bool phy_property_get_channel(struct l_dbus *dbus,
				     struct l_dbus_message *msg,
				     struct l_dbus_message_builder *builder,
				     void *user_data)
{
	struct phy *phy = user_data;

	l_dbus_message_builder_append_basic(builder, 'y', &phy->channel);
	l_info("GetProperty(Channel = %d)", phy->channel);

	return true;
}

static struct l_dbus_message *phy_property_set_channel(struct l_dbus *dbus,
					struct l_dbus_message *message,
					struct l_dbus_message_iter *new_value,
					l_dbus_property_complete_cb_t complete,
					void *user_data)
{
	struct phy *phy = user_data;
	struct l_genl_msg *msg;
	uint8_t value;

	if (!l_dbus_message_iter_get_variant(new_value, "y", &value))
		return dbus_error_invalid_args(message);

	l_info("SetProperty(Channel = %d)", value);

	msg = l_genl_msg_new_sized(NL802154_CMD_SET_CHANNEL, 64);
	l_genl_msg_append_attr(msg, NL802154_ATTR_WPAN_PHY,
			       sizeof(phy->id), &phy->id);
	l_genl_msg_append_attr(msg, NL802154_ATTR_PAGE, 1, &phy->page);
	l_genl_msg_append_attr(msg, NL802154_ATTR_CHANNEL, 1, &value);

	if (!l_genl_family_send(nl802154, msg, NULL, NULL, NULL)) {
		l_error("NL802154_CMD_SET_CHANNEL failed");
		return dbus_error_invalid_args(message);
	}

	phy->channel = value;
	complete(dbus, message, NULL);

	return NULL;
}

static bool phy_property_get_panid(struct l_dbus *dbus,
                                        struct l_dbus_message *msg,
                                        struct l_dbus_message_builder *builder,
                                        void *user_data)
{
        struct phy *phy = user_data;

        l_dbus_message_builder_append_basic(builder, 'q', &phy->panid);
        l_info("GetProperty(PanId = %d)", phy->panid);

        return true;
}

static struct l_dbus_message *phy_property_set_panid(struct l_dbus *dbus,
                                        struct l_dbus_message *message,
                                        struct l_dbus_message_iter *new_value,
                                        l_dbus_property_complete_cb_t complete,
                                        void *user_data)
{
        struct phy *phy = user_data;
        struct l_genl_msg *msg;
        uint16_t value;

        if(!l_dbus_message_iter_get_variant(new_value, "q", &value))
                return dbus_error_invalid_args(message);

        l_info("SetProperty(PANID = %d)", value);

        msg = l_genl_msg_new_sized(NL802154_CMD_SET_PAN_ID, 64);
        l_genl_msg_append_attr(msg, NL802154_ATTR_WPAN_PHY,
                               sizeof(phy->id), &phy->id);
        l_genl_msg_append_attr(msg, NL802154_ATTR_PAGE, 1, &phy->page);
        l_genl_msg_append_attr(msg, NL802154_ATTR_CHANNEL, 1, &phy->channel);
        l_genl_msg_append_attr(msg, NL802154_ATTR_PAN_ID, 2, &value);

        if (!l_genl_family_send(nl802154, msg, NULL, NULL, NULL)) {
                l_error("NL802154_CMD_SET_PAN_ID failed");
                return dbus_error_invalid_args(message);
        }

        phy->panid =value;
        complete(dbus, message, NULL);

        return NULL;
}

static void setup_phy_interface(struct l_dbus_interface *interface)
{
	if (!l_dbus_interface_property(interface, "Powered", 0, "b",
				       phy_property_get_powered,
				       phy_property_set_powered))
		l_error("Can't add 'Powered' property");

	if (!l_dbus_interface_property(interface, "Name", 0, "s",
				       phy_property_get_name,
				       NULL))
		l_error("Can't add 'Name' property");

	if (!l_dbus_interface_property(interface, "Channel", 0, "y",
				       phy_property_get_channel,
				       phy_property_set_channel))
		l_error("Can't add 'Channel' property");

	if (!l_dbus_interface_property(interface, "PanId", 0, "q",
                                       phy_property_get_panid,
                                       phy_property_set_panid))
                l_error("Can't add 'PanId' property");
}

static void add_interface(struct phy *phy)
{
	char *path = l_strdup_printf("/%s", phy->name);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 path,
					 PHY_INTERFACE,
					 phy))
		l_error("'%s': Unable to register %s interface",
							path,
							PHY_INTERFACE);

	if (!l_dbus_object_add_interface(dbus_get_bus(),
					 path,
					 L_DBUS_INTERFACE_PROPERTIES,
					 phy))
		l_error("'%s': Unable to register %s interface",
						path,
						L_DBUS_INTERFACE_PROPERTIES);

	l_free(path);
}

static void get_wpan_phy_callback(struct l_genl_msg *msg, void *user_data)
{
	struct phy *phy;
	struct l_genl_attr attr;
	uint16_t type, len;
	const void *data;

	l_debug("");

	if (!l_genl_attr_init(&attr, msg))
		return;

	phy = l_new(struct phy, 1);
	l_queue_push_head(phy_list, phy);

	phy->powered = false;

	while (l_genl_attr_next(&attr, &type, &len, &data)) {
		l_debug("type: %u len:%u", type, len);
		switch (type) {
		case NL802154_ATTR_WPAN_PHY:
			phy->id = *((uint32_t *) data);
			l_debug("  id: %d", phy->id);
			break;
		case NL802154_ATTR_WPAN_PHY_NAME:
			phy->name = l_strdup(data);
			add_interface(phy);
			l_debug("  name: %s", phy->name);
			break;
		case NL802154_ATTR_PAGE:
			phy->page = *((uint8_t *) data);
			l_debug("  page: %d", phy->page);
			break;
		case NL802154_ATTR_CHANNEL:
			phy->channel = *((uint8_t *) data);
			l_debug("  channel: %d", phy->channel);
			break;
		case NL802154_ATTR_PAN_ID:
                        phy->panid = *((uint16_t *) data);
                        l_debug("  PanId: %d", phy->panid);
                        break;
		}
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
	l_queue_push_head(iface_list, wpan);

	while (l_genl_attr_next(&attr, &type, &len, &data)) {
		l_debug("type: %u len:%u", type, len);
		switch (type) {
		case NL802154_ATTR_WPAN_PHY:
			wpan->id = *((uint32_t *) data);
			l_debug("  id: %d", wpan->id);
			break;
		case NL802154_ATTR_PAN_ID:
                        wpan->panid = *((uint16_t *) data);
                        l_debug("  PanId: %d", wpan->panid);
			break;
		}
	}
}

bool phy_init(struct l_genl_family *genl)
{
	struct l_genl_msg *msg;

	msg = l_genl_msg_new(NL802154_CMD_GET_WPAN_PHY);
	if (!l_genl_family_dump(genl, msg, get_wpan_phy_callback,
						NULL, NULL)) {
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
				       PHY_INTERFACE,
				       setup_phy_interface,
				       NULL, false)) {
		l_error("Unable to register %s interface", PHY_INTERFACE);
		return false;
	}

	phy_list = l_queue_new();
	iface_list = l_queue_new();
	nl802154 = genl;

	return true;
}

void phy_exit(struct l_genl_family *genl)
{
	l_queue_destroy(phy_list, phy_free);
	l_queue_destroy(iface_list, l_free);
}
