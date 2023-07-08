/*
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include "bt_athome_logging.h"
#include "bt_athome_splitter.h"

void aahbt_logpacket(char chip, uint32_t type, const u8 *data, u32 sz,
			uint8_t owned_by)
{
	static const char *owner[] = {"BT", "BLE", "?"};
	static const char *type_name[] = {"Invalid", "HCI_CMD", "HCI_ACL",
					  "HCI_SCO", "HCI_EVENT"};
	char buf[sz * 3 + 1];

	if (LOG_ONLY_OURS && !owned_by)
		return;

	if (type == HCI_SCODATA_PKT)
		return;

	if (!LOG_BT_ACL && type == HCI_ACLDATA_PKT)
		return;

	if (!LOG_BT_EVTS && type == HCI_EVENT_PKT)
		return;

	if (!LOG_BT_CMDS && (type == HCI_COMMAND_PKT || type == PKT_MARVELL))
		return;

	aahlog_bytes(buf, sizeof(buf), data, sz);
	aahlog("%s %c: %02X (%s), sz=%d\n\t%s\n",
	       owner[owned_by], chip ? 'C' : 'H',
	       type,
	       type == HCI_VENDOR_PKT ? "HCI_VENDOR" :
	       type == PKT_MARVELL ? "HCI_MARVELL" :
	       type <= HCI_EVENT_PKT ? type_name[type]: "Unknown",
	       sz, buf);
}

void aahlog_bytes(char *buf, size_t buf_len, const char *bytes, size_t len)
{
	size_t i;
	char *ptr = buf;
	WARN(buf_len == 0, "buffer size 0 is probably not what was intended\n");
	if ((ssize_t)buf_len <= 0 || len == 0)
		return;
	/* we need exactly len * 3 bytes since we print two
	 * characters per byte (%02x) and add a space between
	 * each byte.  last byte has a NULL instead of a space.
	 */
	WARN(buf_len < len * 3, "buffer size too small\n");
	if (buf_len < (len * 3)) {
		buf[0] = 0;
		return;
	}
	/* all buffer len checks done already so we can use
	 * sprintf instead of snprintf
	 */
	for (i = 0; i < len - 1; i++) {
		sprintf(ptr, "%02X ", bytes[i]);
		ptr += 3;
	}
	/* last byte, no extra space, just NULL */
	sprintf(ptr, "%02X", bytes[i]);
}

void aahlog_uuid(char *buf, size_t buf_len, const char *bytes, size_t len)
{
	size_t i;
	char *ptr = buf;
	WARN(buf_len == 0, "buffer size 0 is probably not what was intended\n");
	if ((ssize_t)buf_len <= 0 || len == 0)
		return;
	/* we need exactly len * 2 + 1 bytes since we print two
	 * characters per byte (%02x) with no space in between, and
	 * we need a byte for the null terminator.
	 */
	WARN(buf_len < (len * 2 + 1), "buffer size too small\n");
	if (buf_len < (len * 2 + 1)) {
		buf[0] = 0;
		return;
	}
	/* all buffer len checks done already so we can use
	 * sprintf instead of snprintf
	 */
	for (i = 0; i < len; i++) {
		sprintf(ptr, "%02X", bytes[i]);
		ptr += 2;
	}
}
