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

#ifndef _BT_ATHOME_LOGGING_H_
#define _BT_ATHOME_LOGGING_H_

/*
 *	Logging settings - keep in mind that enabling these may slow the stack
 *	down enough to cause timeouts in things like re-encryption.
 */
#define LOG_ONLY_OURS			1
#define LOG_BT_CMDS			0
#define LOG_BT_EVTS			0
#define LOG_BT_ACL			0
#define LOG_INPUT_EVENTS		0
#define LOG_INPUT_SPEW  		0
#define LOG_BT_CREDIT			0
#define LOG_DISCOVERY			0
#define LOG_DISCOVERY_KNOWN		1
#define LOG_MODESWITCH			1

#define aahlog(...)			printk("AahBtRemote: "  __VA_ARGS__)

void aahlog_bytes(char *buf, size_t buf_len, const char *bytes, size_t len);
void aahlog_uuid(char *buf, size_t buf_len, const char *bytes, size_t len);

void aahbt_logpacket(char chip, uint32_t type,
				const u8 *data, u32 sz, uint8_t owned_by);

void aahbt_set_pkt_log_enabled(int enabled);
void aahbt_dump_pkt_log(int post_dump_delay);
#endif

