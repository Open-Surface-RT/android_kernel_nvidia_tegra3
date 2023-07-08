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

#ifndef _BT_ATHOME_PROTO_H_
#define _BT_ATHOME_PROTO_H_


#include <linux/skbuff.h>
#include <linux/ioctl.h>
#include "bt_athome_hci_extra.h"
#include "bt_athome_util.h"

#define ENCR_RND_NUM	{ 0x44, 0x47, 0x20, 0x43, 0x41, 0x20, 0x4A, 0x47 }
#define ENCR_DIV	0x6F67

/*
 * Clock accuracy of the chip in this device. Setting this higher is always
 * safer, but wastes battery on the remotes. Setting this lower than actual
 * accuracy will cause connections to randomly fail with long intervals.
 */
#define AAH_BT_MARVELL_MCA		HCI_Marvell_MCA_30_ppm

#define AAH_BT_LTK_SZ			16

#define L2CAP_CHAN_ATHOME_BIT		0x80
#define ATHOME_PKT_RX_ACK		(L2CAP_CHAN_ATHOME_BIT | 0)
#define ATHOME_PKT_RX_INPUT		(L2CAP_CHAN_ATHOME_BIT | 1)
#define ATHOME_PKT_RX_AUDIO_0		(L2CAP_CHAN_ATHOME_BIT | 2)
#define ATHOME_PKT_RX_AUDIO_1		(L2CAP_CHAN_ATHOME_BIT | 3)
#define ATHOME_PKT_RX_AUDIO_2		(L2CAP_CHAN_ATHOME_BIT | 4)
#define ATHOME_PKT_RX_AUDIO_3		(L2CAP_CHAN_ATHOME_BIT | 5)
#define ATHOME_PKT_RX_ACCEL		(L2CAP_CHAN_ATHOME_BIT | 6)
#define ATHOME_PKT_RX_MODESWITCH	(L2CAP_CHAN_ATHOME_BIT | 7)
#define ATHOME_PKT_RX_NFC		(L2CAP_CHAN_ATHOME_BIT | 8)
#define ATHOME_PKT_RX_NFC_RF_DETECT	(L2CAP_CHAN_ATHOME_BIT | 9)
#define ATHOME_PKT_RX_KEY_CHORD    	(L2CAP_CHAN_ATHOME_BIT | 10)
#define ATHOME_PKT_RX_TOUCH_V2		(L2CAP_CHAN_ATHOME_BIT | 11)
#define ATHOME_PKT_RX_BTN_V2   		(L2CAP_CHAN_ATHOME_BIT | 12)
#define ATHOME_PKT_RX_ERROR    		(L2CAP_CHAN_ATHOME_BIT | 13)
#define ATHOME_PKT_RX_PAIRING		(L2CAP_CHAN_ATHOME_BIT | 119)

#define ATHOME_INPUT_V1_HAS_TOUCH_FLAG	(1 << 7)
#define ATHOME_INPUT_V1_HAS_BTN_FLAG	(1 << 6)
#define ATHOME_INPUT_V1_GET_TS(x)	(((x & 0x3f) == 0x3f) \
					? AAH_BT_UNKNOWN_TS_DELTA \
					: ((long)(x & 0x3f) * 1000))

struct bm_input_packet_v2_touch_t {
	/* The time in 10uSec units since the last v2_touch event, or 0xFFFF if the
	 * delta is unknown, or too large to represent in 16 bits */
	uint16_t ts_delta;

	/* x[15]    : finger is up/down
	 * x[0..14] : x position
	 * y[15]    : unused, must be 0
	 * y[0..14] : y position
	 */
	uint16_t x;
	uint16_t y;
} __packed;

struct bm_input_packet_v2_btn_t {
	/* The time in 10uSec units since the last v2_touch event, or 0xFFFF if the
	 * delta is unknown, or too large to represent in 16 bits */
	uint16_t ts_delta;

	/* data[7]    : up/down status (1 => down)
	 * data[0..6] : button id
	 */
	uint8_t data;
} __packed;

#define ATHOME_PKT_PMODE_DEEP_SLEEP	0
#define ATHOME_PKT_PMODE_LIGHT_SLEEP	1
#define ATHOME_PKT_PMODE_ACTIVE		2
struct bm_pkt_modeswitch_t {
	uint16_t voltage;
	uint8_t  mode;
} __packed;

/* Event filter flags for remote connection */
#define AAHBT_EFF_SEND_INPUT_TO_SYSTEM	0x00000001
#define AAHBT_EFF_SEND_AUDIO_TO_SYSTEM	0x00000002
#define AAHBT_EFF_SEND_INPUT_TO_USER  	0x00010000
#define AAHBT_EFF_SEND_AUDIO_TO_USER  	0x00020000

struct aahbt_conn_req_t {
	bdaddr_t MAC;
	uint32_t timeout_msec;
} __packed;

struct aahbt_disconn_req_t {
	bdaddr_t MAC;
} __packed;

struct aahbt_conn_settings_t
{
	uint16_t conn_interval;
	uint16_t conn_latency;
	uint16_t svc_timeout;
} __packed;

struct aahbt_enc_req_t
{
	bdaddr_t MAC;
	uint8_t  key[AAH_BT_LTK_SZ];
} __packed;

struct aahbt_get_rand_req_t
{
	uint8_t  rand[16];
} __packed;

struct aahbt_aes128_req_t
{
	uint8_t  key[AAH_BT_LTK_SZ];
	uint8_t  data[AAH_BT_LTK_SZ];
} __packed;

struct aahbt_filter_flags_t
{
	bdaddr_t MAC;
	uint32_t flags;
} __packed;

#define AAHBT_IOCTL_SET_LISTEN_MODE  _IOW('B',  1, int)
#define AAHBT_IOCTL_CONNECT          _IOW('B',  2, struct aahbt_conn_req_t)
#define AAHBT_IOCTL_DISCONNECT       _IOW('B',  3, struct aahbt_disconn_req_t)
#define AAHBT_IOCTL_GET_CONN_PARAMS  _IOR('B',  4, struct aahbt_conn_settings_t)
#define AAHBT_IOCTL_SET_CONN_PARAMS  _IOW('B',  5, struct aahbt_conn_settings_t)
#define AAHBT_IOCTL_ENCRYPT_LINK     _IOW('B',  6, struct aahbt_enc_req_t)
#define AAHBT_IOCTL_GET_RANDOM       _IOR('B',  7, struct aahbt_get_rand_req_t)
#define AAHBT_IOCTL_AES128_ENCRYPT  _IOWR('B',  8, struct aahbt_aes128_req_t)
#define AAHBT_IOCTL_GET_EVT_FILTER   _IOR('B',  9, struct aahbt_filter_flags_t)
#define AAHBT_IOCTL_SET_EVT_FILTER   _IOW('B', 10, struct aahbt_filter_flags_t)
#define AAHBT_IOCTL_SET_PKT_LOG_ENB  _IOW('B', 127, int)

struct aahbt_adv_evt_t {
	uint8_t  event_type;
	uint8_t  addr_type;
	bdaddr_t MAC;
	int8_t   rssi;
	uint8_t  data_len;
	uint8_t  data[];
} __packed;

struct aahbt_connect_result_t {
	bdaddr_t MAC;

	/* Connection status values are to be interpreted as follows.
	 *
	 * 1) A value of 0 indicates success.
	 * 2) A value of 0x01-0x0FF indicates an error which came from the radio
	 *    level and is defined in the v4.0 BT spec, Vol 2, Section D.2
	 * 3) A value less than 0 indicates an errno representing some type of
	 *    host side failure (such as an error on the BT chip bus, or a
	 *    general timeout)).
	 */
	int status;
} __packed;

struct aahbt_disconnected_t {
	bdaddr_t MAC;
	uint8_t reason;
} __packed;

struct aahbt_data_t {
	bdaddr_t MAC;
	uint8_t  payload[0];
} __packed;

struct aahbt_enc_status_t {
	bdaddr_t MAC;
	int status;
} __packed;

/* An event sent to userland to indicate that the stack is up and ready to do
 * work.  is_ready will either be 0, to indicate that the stack is not ready, or
 * non-zero to indicate that it is ready.  The non-zero value is actually a
 * sequence number which increments every time the stack starts and can be used
 * by the userland level to detect a stack bounce in case it misses the
 * not-ready message.
 */
struct aahbt_ready_status_t {
	int is_ready;
} __packed;

#define BT_ATHOME_EVT_ADV               0x81
#define BT_ATHOME_EVT_CONNECT_RESULT    0x82
#define BT_ATHOME_EVT_DISCONNECTED      0x83
#define BT_ATHOME_EVT_DATA              0x84
#define BT_ATHOME_EVT_ENC_STATUS        0x85
#define BT_ATHOME_EVT_RDY_STATUS        0x86

#endif

