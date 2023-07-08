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

#ifndef _BT_ATHOME_SPLITTER_H_
#define _BT_ATHOME_SPLITTER_H_


#include <linux/skbuff.h>
#include <linux/ioctl.h>

/* a type val BT will not use */
#define AAH_BT_PKT_TYPE_CMD			5

/* marvell uses this like a command */
#define PKT_MARVELL				0xFE


#define AAH_BT_PKT_PROCEED			0
#define AAH_BT_PKT_DROP				1


/*  ========== functions provided to external BT driver ========== */

/*
 *	Process an incoming data packet.
 *	returns AAH_BT_PKT_*
 */
int aahbt_remote_filter_rx_data(void *priv, u32 pkt_type,
						u8 *rx_buf, u32 *rx_len);


/*
 *	Called when driver about to send a packet to chip.
 *	returns AAH_BT_PKT_*
 */
int aahbt_pkt_send_req(void *priv, struct sk_buff *skb);

/*
 *	Check if the splitter thinks it is ok to send a packet
 */
int aahbt_ok_to_send(void);

void aahbt_send_to_chip(uint32_t pkt_type, const uint8_t *data, uint32_t len);

/*  ========== functions needed in external BT driver ========== */

/*
 *	Send packet to user, as if it came from the chip
 */
void aahbt_pkt_to_user(void *priv, uint32_t pkt_type,
			   const uint8_t *data, uint32_t len);

/*
 *	Send packet to chip, as if it came from the user
 */
void aahbt_pkt_to_chip(void *priv, uint32_t pkt_type,
			   const uint8_t *data, uint32_t len);

void aahbt_purge_acl_for_conn_id(void *priv, uint16_t conn_id);

void aahbt_splitter_init(void);
bool aahbt_splitter_may_tx_data(void);
void aahbt_splitter_send_data(uint16_t conn_id, struct sk_buff *skb);

#endif

