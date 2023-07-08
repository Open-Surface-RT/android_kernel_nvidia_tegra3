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

#include <asm/unaligned.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include <linux/kthread.h>
#include <linux/wakelock.h>
#include "bt_athome_hci_extra.h"
#include "bt_athome_le_stack.h"
#include "bt_athome_splitter.h"
#include "bt_athome_logging.h"
#include "bt_athome_proto.h"
#include "bt_athome_user.h"
#include "bt_athome_input.h"
#include "bt_athome_util.h"

#define EVT_PACKET_LEN 270

/* Calculate the timeout value for (num) missed replies.
 * The +9 is to round up when dividing by 10.
 * The 1+ is so that the timeout will occur slightly after the missed reply, for
 * safety.
 */
#define AAH_BT_CALC_SVC_TIMEOUT(conint, latency, num) \
		((uint16_t)(1 + ((num) * \
			((((conint) * 1.25 * ((latency) + 1)) + 9) / 10))))

/*
 * Increasing AAH_BT_ACTIVE_CONN_INTERVAL will reduce the quality of recorded
 * audio and touch but improves WiFi bandwidth.  Time interval is 1.25 msec per
 * unit.
 *   6 =>  7.50 msec
 *   7 =>  8.75 msec
 *   8 => 10.00 msec
 *   9 => 11.25 msec
 *  10 => 12.50 msec
 *  11 => 13.75 msec
 *  12 => 15.00 msec
 */
#define AAH_BT_ACTIVE_CONN_INTERVAL     8

/*
 * The scan interval determines how often we will listen for advertising
 * packets.  Time interval per channel is 0.625 msec per unit.
 * To calculate the time in msec, multiply the last number of the scan setting
 * by the connection interval time. For example, if the connection interval is 8
 * then (AAH_BT_ACTIVE_CONN_INTERVAL * 2 * 23) will give us a scan interval of
 *
 *    10.00 msec * 23 = 230 msec
 *
 * Increasing the normal scan interval can improve WiFi bandwidth slightly.
 * But increasing it will also increase the time it takes for devices
 * to be discovered. But it is very quick in human time.
 */

/* Scan interval while looking for the first device to connect with. */
#define AAH_BT_NORMAL_SCAN_INTERVAL    (AAH_BT_ACTIVE_CONN_INTERVAL * 2 * 11)

/* Scan interval while actively connecting to a remote device. */
#define AAH_BT_CONN_SCAN_INTERVAL      (AAH_BT_ACTIVE_CONN_INTERVAL * 2 * 2)

/*
 * Increasing AAH_BT_ACTIVE_SLAVE_LATENCY will improve battery life in an
 * attached BTLE device.  But increasing it will also increase the time it takes
 * for the remote to respond to commands from the master, and can increase the
 * time it takes to exchange keys.
 */
#define AAH_BT_ACTIVE_SLAVE_LATENCY    75

/*
 * Increasing AAH_BT_SERVICE_TIMEOUT_COUNT will make it less likely for the
 * remote control to spontaneously disconnect. This is mainly a problem in
 * spatial coexistence mode when WiFi is transmitting, causing BTLE replies to
 * be lost.  But increasing it will also make it take longer for the master to
 * detect a dead remote control and allow reconnection.
 *
 * Spec requires that this be set to a minimum of 2!
 */
#define AAH_BT_SERVICE_TIMEOUT_COUNT   4

#define AAH_BT_ACTIVE_SVC_TIMEOUT AAH_BT_CALC_SVC_TIMEOUT( \
		AAH_BT_ACTIVE_CONN_INTERVAL,               \
		AAH_BT_ACTIVE_SLAVE_LATENCY,               \
		AAH_BT_SERVICE_TIMEOUT_COUNT)

/* AAH_BT_SCAN_WINDOW = time spent listening per interval, 1.6 units/msec,
 * eg. 10 => 6.25 msec  RECOMMENDED
 * eg. 20 => 12.5 msec
 * */
#define AAH_BT_SCAN_WINDOW             10

/*
 * Set "max event length", which determines the number of
 * times to retry when BTLE TX fails.
 * 6 = 2 retries
 * 4 = 1 retry
 * 2 = 0 retries
 * This was ignored by some Marvell drivers before July 2013.
 */
#define AAH_BT_MAX_EVENT_LENGTH         4

/* Tell Marvell to give passive BTLE scans a lower priority than WiFi TX. */
#define AAH_BT_USE_LOWER_SCAN_PRIORITY   1

enum conn_state {
	/* not a connection */
	CONN_STATE_INVALID,
	/* userland wants this connection state to begin connecting. */
	CONN_STATE_PRE_CONNECTING,
	/* connect cmd sent to radio, waiting for result */
	CONN_STATE_CONNECTING,
	/* connected, but not encrypted */
	CONN_STATE_CONNECTED,
	/* connected and starting the process of encrypting. */
	CONN_STATE_ENCRYPTING,
	/* connected and encrypted */
	CONN_STATE_ENCRYPTED,
	/* disconnect sent to radio, waiting for response. */
	CONN_STATE_DISCONNECTING,
};

#define MAX_CONNECTION_TIMEOUT_MSEC	60000
#define CONN_CANCEL_TIMEOUT_MSEC	5000

#define MAC_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC_DATA(m) (m).b[5], (m).b[4], (m).b[3], (m).b[2], (m).b[1], (m).b[0]

#define AAH_BTLE_CMD_TIMEOUT_MSEC 	(30000)
#define INFINITE			((u32)(-1))
#define AAH_BT_LISTEN_RETRY_MSEC	(2000)

/* Per-remote state struct */
struct aahbt_conn
{
	/* BT state */
	enum conn_state              state;
	uint16_t                     handle;
	bdaddr_t                     MAC;
	struct aahbt_conn_settings_t conn_settings;
	uint64_t                     conn_timeout;
	bool                         reconnect_after_discon;
	bool                         connection_cancel_in_progress;
	bool                         disconnect_asap;
	struct sk_buff_head          tx_pending;
	uint32_t                     evt_filter_flags;
};

/* stack state */
static struct sk_buff_head rx_msgs;
static DECLARE_WAIT_QUEUE_HEAD(rx_wait); /* wait to get evt/data/chg */
static atomic_t state_chg = ATOMIC_INIT(0);
static atomic_t in_shutdown = ATOMIC_INIT(0);
static struct task_struct *aah_thread;

/* Transmit flow control */
static uint32_t max_total_tx_pending = 32;
static uint32_t cur_total_tx_pending;

/* Completion for command response */
static DECLARE_COMPLETION(cmd_response);

/* protects the following data */
static DEFINE_SPINLOCK(stack_state_lock);
static uint16_t inflight_opcode = INVALID_OCF_OGF;
static struct aahbt_conn connection_states[ATHOME_RMT_MAX_CONNS];
static uint8_t *cmd_rsp_buf;
static size_t cmd_rsp_buf_size;

static struct aahbt_conn_settings_t default_conn_settings = {
	.conn_interval = AAH_BT_ACTIVE_CONN_INTERVAL,
	.conn_latency  = AAH_BT_ACTIVE_SLAVE_LATENCY,
	.svc_timeout   = AAH_BT_ACTIVE_SVC_TIMEOUT,
};

static DEFINE_MUTEX(state_lock);
static bool is_listening = false;
static bool should_be_listening = false;
static bool connection_in_progress = false;
static u64 next_listen_attempt_time;

static struct wake_lock wl_suspend; /* used in aahbt_thread only */

/* Utility function used to get a simple, hi-res, monotonic, 64-bit timestamp */
uint64_t aahbt_get_time(void)
{
	struct timespec t;
	uint64_t ret;

	ktime_get_ts(&t);
	ret = t.tv_sec;
	ret *= NSEC_PER_SEC;
	ret += t.tv_nsec;

	return ret;
}

/* Reset a connection state structure.  Optionally, preserve the connection info
 * (MAC, connection settings, connection timeout, etc...) if we are about to
 * trigger an automatic re-connect.
 */
static void aahbt_reset_conn_state_l(struct aahbt_conn *c,
				     bool about_to_reconn)
{
	/* No one should be calling reset on a connection state which still has
	 * transmissions in flight or which has transmissions waiting to be
	 * queued to the radio (pending).  ASSERT that this is the case.
	 */
	BUG_ON(!c);
	BUG_ON(!skb_queue_empty(&c->tx_pending));

	c->state  = about_to_reconn
		  ? CONN_STATE_PRE_CONNECTING
		  : CONN_STATE_INVALID;
	c->handle = INVALID_CONN_ID;
	c->reconnect_after_discon = false;
	c->connection_cancel_in_progress = false;
	c->disconnect_asap = false;
	c->evt_filter_flags = 0;

	if (!about_to_reconn) {
		memset(&c->MAC, 0, sizeof(c->MAC));
		memset(&c->conn_settings, 0, sizeof(c->conn_settings));
		c->conn_timeout = 0;
	}
}

/* Utility function which computes the timeout in mSec (rounded up) from two 64
 * bit timestamps taken from the aahbt_get_time timeline.
 */
static uint32_t compute_msec_timeout(uint64_t now, uint64_t timeout)
{
	uint64_t tmp;
	if ((int64_t)(now - timeout) >= 0)
		return 0;

	tmp = (timeout - now + 999999);
	do_div(tmp, 1000000);
	if (tmp > 0xFFFFFFFE)
		return 0xFFFFFFFE;

	return  (uint32_t)tmp;
}

/* utility function used to find a connection state by MAC addr */
static struct aahbt_conn* aahbt_find_conn_by_mac_l(const bdaddr_t *macP)
{
	int i;

	for (i = 0; i < ATHOME_RMT_MAX_CONNS; i++) {
		if (connection_states[i].state == CONN_STATE_INVALID)
			continue;

		if (bacmp(&connection_states[i].MAC, macP))
			continue;

		return (connection_states + i);
	}

	return NULL;
}

/* utility function used to find a connection state by handle */
static struct aahbt_conn* aahbt_find_conn_by_handle_l(uint16_t handle)
{
	int i;

	for (i = 0; i < ATHOME_RMT_MAX_CONNS; i++) {
		if (connection_states[i].state == CONN_STATE_INVALID)
			continue;

		if (handle != connection_states[i].handle)
			continue;

		return (connection_states + i);
	}

	return NULL;
}

/* utility function used to find a free connection state */
static struct aahbt_conn* aahbt_find_free_conn_l(void)
{
	int i;

	for (i = 0; i < ATHOME_RMT_MAX_CONNS; i++) {
		struct aahbt_conn* c = connection_states + i;
		if (c->state == CONN_STATE_INVALID) {
			aahbt_reset_conn_state_l(c, false);
			return c;
		}
	}

	return NULL;
}

/******************************************************************************
 *
 * Begin Splitter Hooks
 *
 * These hooks are called by the splitter level in response to certain radio
 * level events (data received, connection established, etc...)
 *
 ******************************************************************************/

/* Called by the splitter when it receives messages from the radio we might be
 * interested in.
 */
void aahbt_enqueue_msg(const void *ptr, uint32_t len, uint8_t type)
{
	struct sk_buff *skb;

	skb = bt_skb_alloc(len, GFP_ATOMIC);
	if (!skb) {
		aahlog("fail to alloc skb (%u bytes)\n", len);
		return;
	}

	memcpy(skb->data, ptr, len);
	skb_put(skb, len);
	bt_cb(skb)->pkt_type = type;
	skb_queue_tail(&rx_msgs, skb);
	wake_up_interruptible(&rx_wait);
}

bool aahbt_process_cmd_status_or_complete(uint16_t opcode,
					  const uint8_t *buf,
					  uint32_t len)
{
	unsigned long flags;
	/* Guarantee atomic update to these three globals
	 * and that SMP access to them have fresh data.
	 */
	spin_lock_irqsave(&stack_state_lock, flags);

	if (opcode == inflight_opcode) {
		/* definitely a reply for us */
		inflight_opcode = INVALID_OCF_OGF;
		if (cmd_rsp_buf_size > 0) {
			WARN(len > cmd_rsp_buf_size,
			     "btle response size %d larger than rsp buf "
			     "size %d\n", len, cmd_rsp_buf_size);
			memcpy(cmd_rsp_buf, buf, min(cmd_rsp_buf_size, len));
		}
		spin_unlock_irqrestore(&stack_state_lock, flags);
		complete(&cmd_response);
		return true;
	}
	spin_unlock_irqrestore(&stack_state_lock, flags);
	return false;
}

/******************************************************************************
 *
 *
 * End Splitter Hooks
 *
 *
 ******************************************************************************/

/* send command and then wait for complete or status event. nonzero -> quit */
static int aahbt_simple_cmd_l(uint16_t ogf,
			      uint16_t ocf,
			      size_t plen,
			      const uint8_t *dataIn,
			      size_t outLen,
			      uint8_t *dataOut)
{
	uint8_t cmd[255 + HCI_COMMAND_HDR_SIZE];
	uint16_t opcode;
	struct hci_command_hdr *hdr = (struct hci_command_hdr*)cmd;
	unsigned long flags;
	long res;

	if (atomic_read(&in_shutdown))
		return -ESHUTDOWN;

	opcode = hci_opcode_pack(ogf, ocf);

	BUG_ON(!dataIn && plen);
	if (dataIn && plen) {
		BUG_ON(plen > ((sizeof(cmd) - HCI_COMMAND_HDR_SIZE)));
		memcpy(cmd + HCI_COMMAND_HDR_SIZE, dataIn, plen);
	}
	hdr->plen = plen;
	put_unaligned_le16(opcode, &hdr->opcode);

	/* Guarantee atomic update to the globals
	 * and that SMP access to them have fresh data.
	 */
	spin_lock_irqsave(&stack_state_lock, flags);
	BUG_ON(inflight_opcode != INVALID_OCF_OGF);
	inflight_opcode = opcode;
	cmd_rsp_buf = dataOut;
	cmd_rsp_buf_size = outLen;
	spin_unlock_irqrestore(&stack_state_lock, flags);

	aahbt_send_to_chip(HCI_COMMAND_PKT, cmd,
			   plen + HCI_COMMAND_HDR_SIZE);

	res = wait_for_completion_timeout(&cmd_response,
			msecs_to_jiffies(AAH_BTLE_CMD_TIMEOUT_MSEC));

	if (res <= 0) {
		aahbt_dump_pkt_log(0);
		panic("Timeout or error waiting for command acknowledgement "
		      "from underlying BTLE device. (res = %ld)", res);
	}

	return atomic_read(&in_shutdown) ? -ESHUTDOWN : 0;
}

static int aahbt_get_random_bytes_l(uint8_t *bytes, size_t amt)
{
	int err;
	struct {
		struct hci_event_hdr       ev_hdr;
		struct hci_ev_cmd_complete cc_hdr;
		struct hci_ev_le_rand_resp rand;
	} __packed resp;

	BUG_ON(!bytes);

	while (amt) {
		size_t todo;

		memset(&resp, 0, sizeof(resp));
		err = aahbt_simple_cmd_l(HCI_OGF_LE, HCI_LE_Rand,
					 0, NULL,
					 sizeof(resp), (uint8_t*)&resp);
		if (err)
			return err;

		if ((resp.ev_hdr.plen != (sizeof(resp) - sizeof(resp.ev_hdr)))
		    || (0 != resp.rand.status))
			return -ECOMM;

		todo = min(amt, sizeof(resp.rand.bytes));
		memcpy(bytes, resp.rand.bytes, todo);
		bytes += todo;
		amt -= todo;
	}

	return 0;
}

static int aahbt_do_aes128_l(const uint8_t* key,
				 const uint8_t* in,
				 uint8_t* out)
{
	int err;
	struct hci_cp_le_encrypt_req req;
	struct {
		struct hci_event_hdr          ev_hdr;
		struct hci_ev_cmd_complete    cc_hdr;
		struct hci_ev_le_encrypt_resp enc;
	} __packed resp;

	BUG_ON(!key);
	BUG_ON(!in);
	BUG_ON(!out);

	memcpy(req.key, key, sizeof(req.key));
	memcpy(req.in,   in, sizeof(req.in));
	err = aahbt_simple_cmd_l(HCI_OGF_LE, HCI_LE_Encrypt,
				 sizeof(req),  (const uint8_t*)&req,
				 sizeof(resp), (uint8_t*)&resp);

	if (err)
		return err;

	if ((resp.ev_hdr.plen != (sizeof(resp) - sizeof(resp.ev_hdr)))
	    || (0 != resp.enc.status))
		return -ECOMM;

	memcpy(out, resp.enc.out, sizeof(resp.enc.out));
	return 0;
}

/* Convenience routine for sending a command to the 8797.
 * Returns non-zero on error sending command.
 * Warning: does not return error if command completion status is bad.
 */
static int aahbt_send_command_easy(uint16_t ogf,
			      uint16_t ocf,
			      uint8_t *buf_p,
			      size_t inLen,
			      size_t outLen,
			      const char *message)
{
	uint8_t *evt_data = buf_p + HCI_EVENT_HDR_SIZE;
	uint8_t *cmd_cmpl_data = evt_data + sizeof(struct hci_ev_cmd_complete);
	int sta;
	int err = aahbt_simple_cmd_l(ogf, ocf,
				 inLen, buf_p,
				 outLen, buf_p);
	if (err) {
		aahlog("failed to %s, err = %d\n", message, err);
		return err;
	}
	sta = *cmd_cmpl_data;
	if (sta) {    /* log failure but do not return error code */
		aahlog("failed to %s, sta = 0x%02x.\n",
			message, sta);
	}
	return 0;
}

/*
 * This is a special vendor command provided by Marvell
 * to reserve extra frames after BTLE so BT does not clobber last frame.
 */
static int aahbt_set_extra_attempts(int extra, uint8_t *buf_p, size_t blen)
{
	uint8_t *parP = buf_p;
	aahlog("set extra attempts to %d\n", extra);
	put8LE(&parP, extra); /* how many extra frames to reserve for BTLE */
	return aahbt_send_command_easy(HCI_OGF_Vendor,
				 HCI_CMD_Marvell_BLE_Set_Extra_Spacing,
				 buf_p,
				 parP - buf_p,
				 blen, "set extra attempts");
}

/*
 * Enable or disable scan.
 * The buf array should contain at least EVT_PACKET_LEN bytes.
 * We pass in the buf to avoid allocating large arrays on the stack.
 */
static int aahbt_set_scan_enabled(bool enabled, uint8_t *buf_p, size_t blen)
{
	uint8_t *parP = buf_p;
	put8LE(&parP, enabled ? 1 : 0);	/* scanning enabled or disabled */
	put8LE(&parP, 0);	/* duplicate filter off */
	return aahbt_send_command_easy(HCI_OGF_LE,
				 HCI_LE_Set_Scan_Enable,
				 buf_p,
				 parP - buf_p,
				 blen, "set scan mode");
}

/*
 * Set scan timing interval and window length.
 * The buf array should contain at least EVT_PACKET_LEN bytes.
 * We pass in the buf to avoid allocating large arrays on the stack.
 */
static int aahbt_set_scan_timing(int scan_interval, int scan_window,
				 uint8_t *buf_p, size_t blen)
{
	uint8_t *parP = buf_p;
	aahlog("set scan timing: interval = %d, window = %d\n",
	       scan_interval, scan_window);
	put8LE(&parP, 0);	/* passive scan */
	put16LE(&parP, scan_interval); /* time between listens per channel */
	put16LE(&parP, scan_window); /* time spent listening per interval */
	put8LE(&parP,  0);	/* use real mac address on packets we send */
	put8LE(&parP, 0);	/* accept all advertisement packets */
	return aahbt_send_command_easy(HCI_OGF_LE,
				 HCI_LE_Set_Scan_Parameters,
				 buf_p,
				 parP - buf_p,
				 blen, "set scan timing");
}

static int aahbt_host_setup(void)
{
	uint8_t buf[EVT_PACKET_LEN];
	uint8_t *evt_data = buf + HCI_EVENT_HDR_SIZE;
	uint8_t *cmd_cmpl_data = evt_data + sizeof(struct hci_ev_cmd_complete);
	uint8_t aclBufNum;
	uint16_t aclBufSz;
	uint8_t *parP;
	uint i;
	int err;

	aahlog("host setup, AAH_BT_MAX_EVENT_LENGTH = %d"
		", AAH_BT_ACTIVE_CONN_INTERVAL = %d\n",
		AAH_BT_MAX_EVENT_LENGTH, AAH_BT_ACTIVE_CONN_INTERVAL);
	parP = buf;

	aahlog("turning le and le simul on\n");
	put8LE(&parP, 1);		/* le on */
	put8LE(&parP, 1);		/* le simul on */
	err = aahbt_simple_cmd_l(HCI_OGF_Controller_And_Baseband,
				 HCI_Write_LE_Host_Support,
				 parP - buf, buf,
				 sizeof(buf), buf);
	if (err)
		return err;
	parP = cmd_cmpl_data;
	if (get8LE(&parP)) {	/* check status */
		aahlog("failed to set LE support. bailing.\n");
		return -3;
	}

	aahlog("read buffer size\n");
	err = aahbt_simple_cmd_l(HCI_OGF_LE,
				 HCI_LE_Read_Buffer_Size,
				 0, NULL,
				 sizeof(buf), buf);
	if (err)
		return err;
	parP = cmd_cmpl_data;
	if (get8LE(&parP)) {	/* check status */
		aahlog("failed to get buffer sizes. bailing. \n");
		return -3;
	}
	aclBufSz = get16LE(&parP);
	aclBufNum = get8LE(&parP);
	aahlog("LE chip features %d %d-byte buffers\n", aclBufNum, aclBufSz);

	aahlog("LE INFO:\n");

	aahlog("read local supported features\n");
	err = aahbt_simple_cmd_l(HCI_OGF_LE,
				 HCI_LE_Read_Local_Supported_Features,
				 0, NULL,
				 sizeof(buf), buf);
	if (err)
		return err;
	if (cmd_cmpl_data[1] & 1)
		aahlog(" -> chip supports LE encryption\n");
	cmd_cmpl_data[1] &= ~1;
	for (i = 0; i < 64; i++)
		if (cmd_cmpl_data[i / 8 + 1]  & (1 << (i & 7)))
			aahlog(" -> chip supports unknown LE feature %d\n", i);


	aahlog("read supported states\n");
	err = aahbt_simple_cmd_l(HCI_OGF_LE,
			         HCI_LE_Read_Supported_States,
				 0, NULL,
				 sizeof(buf), buf);
	if (err)
		return err;

	aahlog(" -> normally:\n");
	if (!cmd_cmpl_data[1])
		aahlog("    NONE\n");
	else {
		if (cmd_cmpl_data[1] & 0x01)
			aahlog("   -> non-connectible advertising\n");
		if (cmd_cmpl_data[1] & 0x02)
			aahlog("   -> scannable advertising\n");
		if (cmd_cmpl_data[1] & 0x04)
			aahlog("   -> connectible advertising\n");
		if (cmd_cmpl_data[1] & 0x08)
			aahlog("   -> directed advertising\n");
		if (cmd_cmpl_data[1] & 0x10)
			aahlog("   -> passive scanning\n");
		if (cmd_cmpl_data[1] & 0x20)
			aahlog("   -> active scanning\n");
		if (cmd_cmpl_data[1] & 0x40)
			aahlog("   -> master role\n");
		if (cmd_cmpl_data[1] & 0x80)
			aahlog("   -> slave role\n");
	}

	aahlog(" -> while passive scanning:\n");
	if (!(cmd_cmpl_data[2] & 0x0F))
		aahlog("    NONE\n");
	else {
		if (cmd_cmpl_data[2] & 0x01)
			aahlog("   -> non-connectible advertising\n");
		if (cmd_cmpl_data[2] & 0x02)
			aahlog("   -> scannable advertising\n");
		if (cmd_cmpl_data[2] & 0x04)
			aahlog("   -> connectible advertising\n");
		if (cmd_cmpl_data[2] & 0x08)
			aahlog("   -> directed advertising\n");
	}

	aahlog(" -> while active scanning:\n");
	if (!(cmd_cmpl_data[2] & 0xF0))
		aahlog("    NONE\n");
	else {
		if (cmd_cmpl_data[2] & 0x10)
			aahlog("   -> non-connectible advertising\n");
		if (cmd_cmpl_data[2] & 0x20)
			aahlog("   -> scannable advertising\n");
		if (cmd_cmpl_data[2] & 0x40)
			aahlog("   -> connectible advertising\n");
		if (cmd_cmpl_data[2] & 0x80)
			aahlog("   -> directed advertising\n");
	}

	aahlog(" -> while initiating as master:\n");
	if (!(cmd_cmpl_data[3] & 0x0F))
		aahlog("    NONE\n");
	else {
		if (cmd_cmpl_data[3] & 0x01)
			aahlog("   -> non-connectible advertising\n");
		if (cmd_cmpl_data[3] & 0x02)
			aahlog("   -> scannable advertising\n");
		if (cmd_cmpl_data[3] & 0x04)
			aahlog("   -> connectible advertising\n");
		if (cmd_cmpl_data[3] & 0x08)
			aahlog("   -> directed advertising\n");
	}

	aahlog(" -> while slave:\n");
	if (!(cmd_cmpl_data[3] & 0xF0))
		aahlog("    NONE\n");
	else {
		if (cmd_cmpl_data[3] & 0x10)
			aahlog("   -> non-connectible advertising\n");
		if (cmd_cmpl_data[3] & 0x20)
			aahlog("   -> scannable advertising\n");
		if (cmd_cmpl_data[3] & 0x40)
			aahlog("   -> connectible advertising\n");
		if (cmd_cmpl_data[3] & 0x80)
			aahlog("   -> directed advertising\n");
	}

	err = aahbt_set_scan_timing(AAH_BT_NORMAL_SCAN_INTERVAL,
				    AAH_BT_SCAN_WINDOW, buf, sizeof(buf));
	if (err)
		return err;

	/* Vendor command provided by Marvell to reserve extra frames
	 * so BT does not clobber last BTLE frame. */
	err = aahbt_set_extra_attempts( 1, buf, sizeof(buf));
	if (err)
		return err;

	aahlog("set clock accuracy\n");
	parP = buf;
	put8LE(&parP, AAH_BT_MARVELL_MCA);
	put8LE(&parP, AAH_BT_MARVELL_MCA);
	err = aahbt_simple_cmd_l(HCI_OGF_Vendor,
				 HCI_CMD_Marvell_Set_MCA,
				 parP - buf, buf,
				 0, NULL);
	if (err)
		return err;

#if (AAH_BT_USE_LOWER_SCAN_PRIORITY)
	aahlog("set coexistence mode configuration\n");
	/*
	 * Change coexistence behavior based on suggestions from Marvell.
	 * Changes LE scan priority to be lower than WiFi.
	 */
	{
		static const uint8_t coex_cmd_params[] = {
			0x03, 0x01, 0x01, 0x01,  0x01, 0x03, 0x01, 0x00,
			0x02, 0x02, 0x02, 0x04,  0x02, 0x00, 0x00, 0x00,
			0x04, 0x02, 0x00, 0x00,  0x00};
		err = aahbt_simple_cmd_l(HCI_OGF_Vendor,
					 HCI_CMD_Marvell_BLE_COEX_MODE_CONFIG,
					 sizeof(coex_cmd_params),
					 coex_cmd_params,
					 0, NULL);
		if (err)
			return err;
	}

#endif /* AAH_BT_USE_LOWER_SCAN_PRIORITY */
	return 0;
}

static inline bool aahbt_should_quit(void)
{
	return (kthread_should_stop() || atomic_read(&in_shutdown));
}

static inline bool aahbt_thread_has_work(void)
{
	bool has_rx_work = !skb_queue_empty(&rx_msgs);
	bool has_tx_work = cur_total_tx_pending && aahbt_splitter_may_tx_data();
	bool external_wakeup = (0 != atomic_read(&state_chg));

	return (aahbt_should_quit() ||
		has_rx_work ||
		has_tx_work ||
		external_wakeup);
}

void aahbt_wake_thread(void)
{
	atomic_set(&state_chg, 1);
	wake_up_interruptible(&rx_wait);
}

bool aahbt_running_on_aah_thread(void)
{
	return (aah_thread && (current == aah_thread));
}

static inline void aahbt_purge_pending_tx_l(struct aahbt_conn* c)
{
	/* If we are currently in the process of becoming encrypted, then any
	 * packets in our tx_pending queue are not being counted in the
	 * total_tx_pending count variable.  The work thread has work to do any
	 * time it has TX credits available and there are packets waiting to be
	 * sent (as defined by cur_total_tx_pending).  When a device is in the
	 * process of encrypting, its packets may not be sent and therefor
	 * should not be counted as part of the cur_tx_pending_count.
	 *
	 * The upshot of all of this is that when we are releasing the
	 * tx_resources for a device, we only need to adjust the
	 * cur_total_tx_pending count when we are not in the encrypting state,
	 * and if we do, we should be able to ASSERT that the number of packets
	 * in our tx_pending queue is <= cur_total_tx_pending.
	 */
	BUG_ON(!c);
	if (CONN_STATE_ENCRYPTING != c->state) {
		size_t qlen = skb_queue_len(&c->tx_pending);
		BUG_ON(qlen > cur_total_tx_pending);
		cur_total_tx_pending -= qlen;
	}

	while (!skb_queue_empty(&c->tx_pending)) {
		struct sk_buff *skb = skb_dequeue(&c->tx_pending);
		BUG_ON(!skb);
		kfree_skb(skb);
	}
}

static u32 aahbt_try_toggle_listen_state_l(u64 now)
{
	const char* start_stop = should_be_listening ? "start" : "stop";
	uint8_t buf[EVT_PACKET_LEN];
	int 	ret;

	BUG_ON(is_listening == should_be_listening);

	if (now < next_listen_attempt_time)
		goto calc_current_retry;

	aahlog("attempting to %s listening\n", start_stop);

	ret = aahbt_set_scan_enabled(should_be_listening, buf, sizeof(buf));
	if (!ret) {
		is_listening = should_be_listening;
		return INFINITE;
	}

	next_listen_attempt_time = now +
				   (AAH_BT_LISTEN_RETRY_MSEC * 1000000ull);

calc_current_retry:
	if (now >= next_listen_attempt_time)
		return 0;

	return compute_msec_timeout(now, next_listen_attempt_time);
}

static void aahbt_send_connect_result_to_usr_l(const struct aahbt_conn *c,
					       int status)
{
	struct aahbt_connect_result_t msg;
	BUG_ON(!c);

	if (!status)
		aahlog(MAC_FMT " : connected\n", MAC_DATA(c->MAC));
	else
		aahlog(MAC_FMT " : connect failed (status %d)\n",
				MAC_DATA(c->MAC), status);

	bacpy(&msg.MAC, &c->MAC);
	msg.status = status;

	aahbt_usr_enqueue(BT_ATHOME_EVT_CONNECT_RESULT,
			  &msg, sizeof(msg), NULL);
}

static void aahbt_send_disconnected_to_usr_l(const struct aahbt_conn *c,
					     uint8_t reason)
{
	struct aahbt_disconnected_t msg;
	BUG_ON(!c);

	aahlog(MAC_FMT " : disconnected (reason 0x%02x)\n",
			MAC_DATA(c->MAC), reason);

	bacpy(&msg.MAC, &c->MAC);
	msg.reason = reason;

	aahbt_usr_enqueue(BT_ATHOME_EVT_DISCONNECTED,
			  &msg, sizeof(msg), NULL);
}

static void aahbt_send_enc_status_to_usr_l(const struct aahbt_conn *c,
					   uint8_t status,
					   bool enc_on)
{
	struct aahbt_enc_status_t msg;
	BUG_ON(!c);

	aahlog(MAC_FMT " : encryption is now %s (status 0x%02x)\n",
	       MAC_DATA(c->MAC), enc_on ? "on" : "off", status);

	bacpy(&msg.MAC, &c->MAC);

	/* Once we have become encrypted, we never allow encryption to be shut
	 * off.  If we get any message indicating that encryption is suddenly
	 * off, report it as an error up to the user.
	 */
	if (!enc_on)
		msg.status = -EPROTO;
	else
		msg.status = status;

	aahbt_usr_enqueue(BT_ATHOME_EVT_ENC_STATUS,
			  &msg, sizeof(msg), NULL);
}

static uint32_t aahbt_handle_connection_timeout_l(uint64_t now,
						  struct aahbt_conn *c)
{
	uint32_t ret;
	BUG_ON(!c);

	/* Start by checking to see if the timeout is valid.  The timeout is
	 * valid in 3 situations.
	 *
	 * 1) We are waiting to start connecting, but have not done so yet and
	 *    are therefor in the in PRE_CONNECTING state.
	 * 2) We have successfully sent the message to the radio to start the
	 *    connection process (therefor we are in the CONNECTING state) but
	 *    it has not completed yet.
	 * 3) We are in the process of disconnecting, but would like to start to
	 *    connect again as soon as the disconnect operation is finished.
	 */
	if ((c->state != CONN_STATE_PRE_CONNECTING) &&
	    (c->state != CONN_STATE_CONNECTING) &&
	  !((c->state == CONN_STATE_DISCONNECTING) &&
	     c->reconnect_after_discon))
		return INFINITE;

	/* Timeout is valid.  If its anything but 0, we have not timed out yet.
	 */
	ret = compute_msec_timeout(now, c->conn_timeout);
	if (ret)
		return ret;

	/* Looks like we have timed out, take appropriate action based on the
	 * state of the connection attempt, then report the timeout up to
	 * userland.
	 */
	switch (c->state) {
	/* If we never started the process of connecting, just release the
	 * connection state structure back to the free pool.  No further action
	 * should be needed.
	 */
	case CONN_STATE_PRE_CONNECTING:
		aahbt_reset_conn_state_l(c, false);
		break;

	/* If we are in the process of disconnecting, let the disconnection run
	 * its course, but we are no longer interested in trying to re-connect
	 * once the disconnection is finished.
	 */
	case CONN_STATE_DISCONNECTING:
		c->reconnect_after_discon = false;
		break;

	/* Looks like the radio is actively in the process of attempting to make
	 * this connection.  Attempt to abort the operation by sending a cancel
	 * command to the radio.  If the cancel command itself fails, it should
	 * only be because the connection status has already changed (success or
	 * failure) and the LE_Connection Complete event is already in the
	 * pipeline waiting to be processed.  If the cancel command succeeds,
	 * the radio will send a LE_Connection Complete with a status code of
	 * 0x02 (Unknown Connection) when it finishes the cancel operation and
	 * it is OK to begin a new connection operation.  Either way, reset the
	 * timeout on this connection so we don't spin while waiting for the
	 * response to come back from the radio.
	 */
	case CONN_STATE_CONNECTING:
		BUG_ON(!connection_in_progress);

		/* If we have already tried to cancel this connection once
		 * already, then something Very Bad has happened with the chip.
		 * It is unclear of how to proceed here.  Bouncing the chip
		 * seems like the "best" thing to do, but it is unclear how to
		 * synchronize this operation with the bluedroid stack.  For
		 * now, we log a big'ole warning and try to issue the command
		 * once again in an attempt to unstick the situation.
		 */
		if (c->connection_cancel_in_progress)
			aahlog("WARNING - attempts to cancel connecting to "
			       MAC_FMT " appear to be failing!\n",
			       MAC_DATA(c->MAC));

		c->connection_cancel_in_progress = true;
		c->conn_timeout = now
			        + ((u64)CONN_CANCEL_TIMEOUT_MSEC * 1000000);

		aahbt_simple_cmd_l(HCI_OGF_LE,
				   HCI_LE_Create_Connection_Cancel,
				   0, NULL, 0, NULL);
		return CONN_CANCEL_TIMEOUT_MSEC;

	default:
		return INFINITE;
	}

	aahbt_send_connect_result_to_usr_l(c, -ETIMEDOUT);
	return INFINITE;
}

static uint32_t aahbt_connect_if_needed_l(uint64_t now,
					  struct aahbt_conn *c)
{
	const struct aahbt_conn_settings_t* cs;
	struct hci_cp_le_create_conn cmd;
	uint8_t resp_buf[EVT_PACKET_LEN];
	struct hci_ev_cmd_status *status;
	int res;
	uint32_t timeout_msec;

	BUG_ON(!c);
	BUG_ON(connection_in_progress);

	if (c->state != CONN_STATE_PRE_CONNECTING)
		return INFINITE;

	/* thread should have pruned this state already if it had timed out */
	BUG_ON(now >= c->conn_timeout);

	/* Pack and send the connection command to the radio */
	cs = &c->conn_settings;
	bacpy(&cmd.peer_addr, &c->MAC);
	put_unaligned_le16(AAH_BT_CONN_SCAN_INTERVAL, &cmd.scan_interval);
	put_unaligned_le16(AAH_BT_SCAN_WINDOW,        &cmd.scan_window);
	cmd.filter_policy    = 0;	/* no whitelist */
	cmd.peer_addr_type   = ADDR_LE_DEV_RANDOM;	/* peer addr is random */
	cmd.own_address_type = ADDR_LE_DEV_PUBLIC;	/* use our public MAC */
	put_unaligned_le16(cs->conn_interval,       &cmd.conn_interval_min);
	put_unaligned_le16(cs->conn_interval,       &cmd.conn_interval_max);
	put_unaligned_le16(cs->conn_latency,        &cmd.conn_latency);
	put_unaligned_le16(cs->svc_timeout,         &cmd.supervision_timeout);
	put_unaligned_le16(AAH_BT_MAX_EVENT_LENGTH, &cmd.min_ce_len);
	put_unaligned_le16(AAH_BT_MAX_EVENT_LENGTH, &cmd.max_ce_len);

	res = aahbt_simple_cmd_l(HCI_OGF_LE,
				 HCI_LE_Create_Connection,
				 sizeof(cmd), (uint8_t*)&cmd,
				 sizeof(resp_buf), resp_buf);
	if (res != 0) {
		aahlog(MAC_FMT " : failed to send connect cmd (res = %d)\n",
		       MAC_DATA(c->MAC), res);
		aahbt_send_connect_result_to_usr_l(c, res);
		goto failure;
	}

	status = (struct hci_ev_cmd_status*)(resp_buf + HCI_EVENT_HDR_SIZE);
	if (status->status) {
		aahlog(MAC_FMT " : connect cmd rejected (status 0x%02x)\n",
		       MAC_DATA(c->MAC), status->status);
		aahbt_send_connect_result_to_usr_l(c, status->status);
		goto failure;
	}

	aahlog(MAC_FMT " : connection attempt started"
		", interval = %d, latency = %d, timeout = %d msec\n",
		MAC_DATA(c->MAC), cs->conn_interval,
		cs->conn_latency, cs->svc_timeout);
	c->state = CONN_STATE_CONNECTING;
	connection_in_progress = true;

	timeout_msec = compute_msec_timeout(now, c->conn_timeout);

	/* Stay awake while the connection is being attempted */
	wake_lock_timeout(&wl_suspend, msecs_to_jiffies(timeout_msec));

	return timeout_msec;

failure:
	aahbt_reset_conn_state_l(c, false);
	return INFINITE;
}

static void aahbt_send_disconnect_l(struct aahbt_conn *c)
{
	struct hci_cp_disconnect msg;

	BUG_ON(!c);
	put_unaligned_le16(c->handle, &msg.handle);
	msg.reason = 0x13; /* remote user requested */

	aahbt_simple_cmd_l(HCI_OGF_Link_Control,
			   HCI_CMD_Disconnect,
			   sizeof(msg), (const uint8_t*)&msg,
			   0, NULL);

	aahbt_purge_pending_tx_l(c);
	c->state = CONN_STATE_DISCONNECTING;
}

static void aahbt_clear_white_list_l(void)
{
	int err;
	struct {
		struct hci_event_hdr                   ev_hdr;
		struct hci_ev_cmd_complete             cc_hdr;
		struct hci_ev_le_clear_white_list_resp white_list;
	} __packed resp;

	err = aahbt_simple_cmd_l(HCI_OGF_LE,
				 HCI_LE_Clear_White_List,
				 0, NULL,
				 sizeof(resp), (uint8_t*)&resp);
	if (err != 0) {
		aahlog("failed to send clear_white_list"
		       " (err = %d)\n", err);
		return;
	}

	if (resp.ev_hdr.plen != (sizeof(resp) - sizeof(resp.ev_hdr))) {
		aahlog("clear_white_list resp had bad len %d (expected %d)\n",
		       resp.ev_hdr.plen, (sizeof(resp) - sizeof(resp.ev_hdr)));
		return;
	}

	if (resp.white_list.status) {
		aahlog("clear_white_list rejected (status 0x%02x)\n",
		       resp.white_list.status);
		return;
	}

	aahlog("clear_white_list succeeded.\n");
}

static void aahbt_add_device_to_white_list_l(const struct aahbt_conn *c)
{
	int err;
	struct hci_cp_le_add_device_to_white_list cmd;
	struct {
		struct hci_event_hdr                           ev_hdr;
		struct hci_ev_cmd_complete                     cc_hdr;
		struct hci_ev_le_add_device_to_white_list_resp white_list;
	} __packed resp;

	BUG_ON(!c);

	bacpy(&cmd.addr, &c->MAC);
	cmd.addr_type = ADDR_LE_DEV_RANDOM;

	err = aahbt_simple_cmd_l(HCI_OGF_LE,
				 HCI_LE_Add_Device_To_White_List,
				 sizeof(cmd), (uint8_t*)&cmd,
				 sizeof(resp), (uint8_t*)&resp);
	if (err != 0) {
		aahlog(MAC_FMT " : failed to send add_device_to_white_list"
		       " (err = %d)\n",
		       MAC_DATA(c->MAC), err);
		return;
	}

	if (resp.ev_hdr.plen != (sizeof(resp) - sizeof(resp.ev_hdr))) {
		aahlog(MAC_FMT " : add_device_to_white_list resp had bad len %d"
		       " (expected %d)\n",
		       MAC_DATA(c->MAC), resp.ev_hdr.plen,
		       (sizeof(resp) - sizeof(resp.ev_hdr)));
		return;
	}

	if (resp.white_list.status) {
		aahlog(MAC_FMT " : add_device_to_white_list rejected (status 0x%02x)\n",
		       MAC_DATA(c->MAC), resp.white_list.status);
		return;
	}

	aahlog(MAC_FMT " : add_device_to_white_list succeeded.\n", MAC_DATA(c->MAC));
}

static void aahbt_remove_device_from_white_list_l(const struct aahbt_conn *c)
{
	int err;
	struct hci_cp_le_remove_device_from_white_list cmd;
	struct {
		struct hci_event_hdr                                ev_hdr;
		struct hci_ev_cmd_complete                          cc_hdr;
		struct hci_ev_le_remove_device_from_white_list_resp white_list;
	} __packed resp;

	BUG_ON(!c);

	bacpy(&cmd.addr, &c->MAC);
	cmd.addr_type = ADDR_LE_DEV_RANDOM;

	err = aahbt_simple_cmd_l(HCI_OGF_LE,
				 HCI_LE_Remove_Device_From_White_List,
				 sizeof(cmd), (uint8_t*)&cmd,
				 sizeof(resp), (uint8_t*)&resp);
	if (err != 0) {
		aahlog(MAC_FMT " : failed to send remove_device_from_white_list"
		       " (err = %d)\n",
		       MAC_DATA(c->MAC), err);
		return;
	}

	if (resp.ev_hdr.plen != (sizeof(resp) - sizeof(resp.ev_hdr))) {
		aahlog(MAC_FMT " : remove_device_from_white_list resp had bad len %d"
		       " (expected %d)\n",
		       MAC_DATA(c->MAC), resp.ev_hdr.plen,
		       (sizeof(resp) - sizeof(resp.ev_hdr)));
		return;
	}

	if (resp.white_list.status) {
		aahlog(MAC_FMT " : remove_device_from_white_list rejected (status 0x%02x)\n",
		       MAC_DATA(c->MAC), resp.white_list.status);
		return;
	}

	aahlog(MAC_FMT " : remove_device_from_white_list succeeded.\n", MAC_DATA(c->MAC));
}

#define CHECK_LEN(needed) \
	do { if (len < (needed)) { \
		aahlog("SKB too short (%d < %d) left to process at line %d\n", \
				len, (needed), __LINE__); \
		return -ENODATA; \
	} } while (0)

static int aahbt_process_le_adv_report_l(const uint8_t* data, size_t len)
{
	uint8_t cnt;

	/* If userland does not want to be listening at this point, go ahead and
	 * ignore the reports contained in this message.
	 */
	if (!should_be_listening)
		return 0;

	/* Make sure the packet is not lying about its length */
	CHECK_LEN(1);
	cnt = data[0];
	data += 1;
	len  -= 1;

	while (cnt--) {
		uint8_t buf[sizeof(struct aahbt_adv_evt_t) + 0x1F];
		struct hci_ev_le_advertising_info *adv;
		struct aahbt_adv_evt_t* msg;
		size_t total_len;

		adv = (struct hci_ev_le_advertising_info*)data;

		/* need to be able to hold the basic header */
		CHECK_LEN(sizeof(*adv));

		/* now that we can safely access length, check to make
		 * sure we can also hold the payload as well as the RSSI
		 * field after the payload.
		 */
		total_len = sizeof(*adv) + adv->length + 1;
		CHECK_LEN(total_len);

		/* Sanity check the payload length against the spec */
		if (adv->length > 0x1F) {
			aahlog("Adv payload too long (%d); skipping the"
			       " rest of the report.\n", adv->length);
			return -EINVAL;
		}

		BUG_ON(total_len > sizeof(buf));
		BUG_ON(total_len != sizeof(*msg) + adv->length);

		/* Everything looks good, repack the payload and send it
		 * up to userland.
		 */
		msg = (struct aahbt_adv_evt_t*)buf;
		msg->event_type	= adv->evt_type;
		msg->addr_type	= adv->bdaddr_type;
		msg->rssi	= (int8_t)(adv->data + adv->length)[0];
		msg->data_len	= adv->length;
		bacpy(&msg->MAC, &adv->bdaddr);
		memcpy(msg->data, adv->data, adv->length);

		aahbt_usr_enqueue(BT_ATHOME_EVT_ADV, buf, total_len, NULL);
		data += total_len;
		len -= total_len;
	}

	return 0;
}

static int aahbt_process_le_conn_complete_l(const uint8_t* data, size_t len)
{
	struct hci_ev_le_conn_complete *evt;
	struct aahbt_conn *c;
	size_t i;
	int status;

	if (!connection_in_progress) {
		aahlog("RXed unexpected connection complete event\n");
		return -EINVAL;
	}

	/* One way or the other, there is no longer a connection attempt in
	 * progress.
	 */
	connection_in_progress = false;

	for (i = 0; i < ARRAY_SIZE(connection_states); i++) {
		c = connection_states + i;
		if (CONN_STATE_CONNECTING == c->state)
			break;
	}

	if (i >= ARRAY_SIZE(connection_states)) {
		aahlog("RXed conn complete evt, but no states are "
		       "connecting!\n");
		return -EINVAL;
	}

	BUG_ON(len < sizeof(*evt));
	evt = (struct hci_ev_le_conn_complete *)data;

	/* Inform userland of the result of the connection operation.  There is
	 * one special case to handle here.  If the connection complete event
	 * has a status of 0x02 (Unknown Connection) it is because we canceled
	 * the connection operation as a result of a timeout, or because the
	 * user requested and explicit disconnect while connecting..  In this
	 * case, we re-write the error code to indicate timeout or abort as
	 * appropriate.
	 */
	status = (int)evt->status;
	if (status == 0x02)
		status = c->disconnect_asap ? -ECONNABORTED : -ETIMEDOUT;
	aahbt_send_connect_result_to_usr_l(c, status);

	/* If we succeeded, we are now in the CONNECTED state.  If we failed,
	 * then we are back to the INVALID state.
	 */
	if (!status) {
		/* if the connection was successful, change the diag LED to
		 * indicate that we are connected now.
		 */
		aahbt_input_led_show_event(HACK_LED_EVENT_CONNECT);

		c->handle = get_unaligned_le16(&evt->handle);
		c->state  = CONN_STATE_CONNECTED;

		/* Add this device's MAC into the white list so that the
		 * device filtering is in effect. Since the controller
		 * will ignore the requests from the devices not on the list,
		 * it reduces the number of transmissions the controller would
		 * be required to make, which reduces power consumption.
		 * Especially, we don't want the white list to be empty when the
		 * system goes into suspend, otherwise it would wake too often
		 * because the controller will respond to advertisement packets
		 * and scan requests from any BLE device.
		 *
		 * TODO: we might want to enable the device filtering earlier,
		 * e.g. before attempting the connection.
		 */
		aahbt_add_device_to_white_list_l(c);

		/* If the user attempted to abort this connection just as it was
		 * being made, then the disconnect_asap flag will be set, and we
		 * should start the process of disconnecting.
		 */
		if (c->disconnect_asap)
			aahbt_send_disconnect_l(c);
	} else {
		aahbt_reset_conn_state_l(c, false);
	}

	return 0;
}

static int aahbt_process_disconn_evt_l(const uint8_t* data, size_t len)
{
	struct hci_ev_disconn_complete *evt;
	struct aahbt_conn* c;
	uint16_t handle;
	bool tmp;

	CHECK_LEN(sizeof(*evt));

	evt = (struct hci_ev_disconn_complete *)(data);
	if (evt->status) {
		aahlog("WARNING; disconn complete with failed status code "
		       " (0x%02x)\n", evt->status);
		return -EINVAL;
	}

	handle = get_unaligned_le16(&evt->handle);
	c = aahbt_find_conn_by_handle_l(handle);
	if (!c) {
		aahlog("WARNING; disconn event for untracked connection "
		       "0x%04hx\n", handle);
		return -EINVAL;
	}

	/* Free any pending transmissions which were waiting on this connection.
	 */
	aahbt_purge_pending_tx_l(c);

	/* Great, we found our connection.  Report the disconnected status to
	 * the user along with the reason for disconnect.  Then either recycle
	 * the connection state or start to reconnect if the user wants this
	 * connection to be active.  ACL credits held by this connection ID have
	 * already been returned to the ACL credit pool at the splitter level
	 * when it processed this message and decided it was for us.
	 */
	aahbt_send_disconnected_to_usr_l(c, evt->reason);
	aahbt_input_led_show_event(HACK_LED_EVENT_DISCONNECT);

	/* If our number of pending TX operations has fallen below max, signal
	 * any userland threads which are waiting for there to be room to
	 * transmit.
	 */
	if (cur_total_tx_pending < max_total_tx_pending)
		aahbt_usr_signal_tx_has_room();

	/* Remove the device from the white list.
	 *
	 * TODO: this is a conservative approach. We might want to leave the
	 * device filtering enabled if we can safely ignore other LE devices
	 * until we re-connect to the same device.
	 */
	aahbt_remove_device_from_white_list_l(c);

	aahbt_reset_conn_state_l(c, (tmp = c->reconnect_after_discon));
	if (tmp)
		aahlog(MAC_FMT " : post-discon connection attempt scheduled.\n",
		       MAC_DATA(c->MAC));

	return 0;
}

static int aahbt_process_enc_complete_evt_l(uint16_t handle,
					    uint8_t status,
					    bool enc_on)
{
	struct aahbt_conn *c;

	c = aahbt_find_conn_by_handle_l(handle);
	if (NULL == c) {
		aahlog("WARNING; got encryption status change"
                       " (st 0x%08x, onoff %d) for unknown conn id %hd\n",
		       status, enc_on, handle);
		return -ENODATA;
	}

	if (CONN_STATE_ENCRYPTING != c->state) {
		aahlog(MAC_FMT
		       " : got encryption status change (st 0x%08x, onoff %d)"
		       " for a device which is not currently encrypting\n",
		       MAC_DATA(c->MAC), status, enc_on);
		return -EINVAL;
	}

	c->state = enc_on
		 ? CONN_STATE_ENCRYPTED
		 : CONN_STATE_CONNECTED;

	/* TXing may now resume.  Update the pending count appropriately */
	cur_total_tx_pending += skb_queue_len(&c->tx_pending);

	aahbt_send_enc_status_to_usr_l(c, status, enc_on);

	return 0;
}

static int aahbt_process_le_evt_l(const uint8_t* data, size_t len)
{
	uint8_t type;
	BUG_ON(NULL == data);

	/* LE Meta events all begin with a 1-byte sub-event code.  Extract this
	 * code, skip over it in the stream, and then process the event body
	 * based on the sub-event code.
	 */
	CHECK_LEN(1);
	type = data[0];
	data++;
	len--;

	switch (type) {
	case HCI_EV_LE_CONN_COMPLETE:
		return aahbt_process_le_conn_complete_l(data, len);
	case HCI_EV_LE_ADVERTISING_REPORT:
		return aahbt_process_le_adv_report_l(data, len);
	case HCI_EV_LE_CONN_UPDATE:	          // TODO: handle this event
	case HCI_EV_LE_READ_REMOTE_FEATURES_COMP: // TODO: handle this event
	case HCI_EV_LE_LTK_REQ: 		  // TODO: handle this event
	default: {
		aahlog("Dropping unexpected LE meta event (0x%02x)\n", type);
		return -EINVAL;
	} break;
	}
}

static int aahbt_process_normal_evt_l(const uint8_t* data, size_t len)
{
	uint8_t type;
	BUG_ON(NULL == data);

	/* aahbt_process_evt_skb_l should have already checked to be sure that
	 * we have enough data for a standard BT event header (IOW - there have
	 * to be at least 2 bytes).  ASSERT this.  Then extract the event ID,
	 * skip over the standard header, and process the event payload based on
	 * the extracted event ID.
	 */
	BUG_ON(len < 2);
	type = data[0];
	data += 2;
	len  -= 2;

	switch (type) {
	case HCI_EV_DISCONN_COMPLETE:
		return aahbt_process_disconn_evt_l(data, len);

	case HCI_EV_ENCRYPT_CHANGE: {
		struct hci_ev_encrypt_change *ev;
		CHECK_LEN(sizeof(*ev));
		ev = (struct hci_ev_encrypt_change*)(data);
		return aahbt_process_enc_complete_evt_l(
				get_unaligned_le16(&ev->handle),
				ev->status,
				!!ev->encrypt);
	} break;

	case HCI_EV_ENCR_REFRESH: {
		struct hci_ev_encrypt_refresh *ev;
		CHECK_LEN(sizeof(*ev));
		ev = (struct hci_ev_encrypt_refresh*)(data);
		return aahbt_process_enc_complete_evt_l(
				get_unaligned_le16(&ev->handle),
				ev->status,
				true);
	} break;

	/* NUM_COMP_PKTS should have been handled already at the splitter level
	 */
	case HCI_EV_NUM_COMP_PKTS:
	default: {
		aahlog("Dropping unexpected event (0x%02x)\n", type);
		return -EINVAL;
	} break;
	}
}

static int aahbt_process_evt_skb_l(struct sk_buff *skb)
{
	const uint8_t* data;
	size_t len;
	uint8_t evt_code, p_total;
	BUG_ON(NULL == skb);

	data = skb->data;
	len  = skb->len;

	/* All BT/BTLE events start with a 1-byte event code and a 1-byte length
	 * field indicating the total length of the event specific parmeters
	 * following the header.  Make sure we have enough data for the basic
	 * header, and at least as much data as is implied by the parameter
	 * length field in the header.
	 */
	CHECK_LEN(2);

	evt_code = data[0];
	p_total  = data[1];

	CHECK_LEN(p_total + 2);

	/* LE events all have the same event code (LE_META) and have another
	 * field after the standard BT event header indicating their sub event
	 * code.  Skip the 2-byte standard header when we pass LE events off to
	 * the LE event processor.
	 */
	if (evt_code == HCI_EV_LE_META)
		return aahbt_process_le_evt_l(data + 2, len - 2);
	else
		return aahbt_process_normal_evt_l(data, len);
}

static int aahbt_process_input_evt_l(struct aahbt_conn *c,
				     uint8_t *data, size_t len)
{
	uint8_t type;
	uint32_t which;

	BUG_ON(!c);
	BUG_ON(!data);
	CHECK_LEN(3);

	if (c->evt_filter_flags & AAHBT_EFF_SEND_INPUT_TO_USER)
		aahbt_usr_enqueue(BT_ATHOME_EVT_DATA, data, len, &c->MAC);

	/* If we are not allowed to send this payload to the system, then there
	 * is no point in wasting the time to even parse it.
	 */
	if (!(c->evt_filter_flags & AAHBT_EFF_SEND_INPUT_TO_SYSTEM))
		return 0;

	/* Repack the payload so we don't have to deal with the type byte in the
	 * middle of our structure.
	 */
	type = data[2];
	data[2] = data[1];
	data[1] = data[0];
	data++;
	len--;

	which = (((uint8_t*)c) - ((uint8_t*)connection_states)) / sizeof(*c);
	BUG_ON(which >= ATHOME_RMT_MAX_CONNS);

	switch (type) {
	case ATHOME_PKT_RX_TOUCH_V2: {
		struct bm_input_packet_v2_touch_t *evt;
		uint16_t tmp;
		long ts_delta;

		CHECK_LEN(sizeof(*evt));
		evt = (struct bm_input_packet_v2_touch_t*)data;

		tmp = get_unaligned_le16(&evt->ts_delta);
		ts_delta = (tmp == ((uint16_t)(-1)))
			 ? AAH_BT_UNKNOWN_TS_DELTA
			 : ((long)tmp * 10);
		aahbt_input_calculate_time(which, ts_delta);

		tmp = get_unaligned_le16(&evt->x);
		aahbt_input_send_touch(which, 0,
				       tmp << 1,
				       get_unaligned_le16(&evt->y) << 1,
				       (0 != (tmp & 0x8000)));

		/* DPAD code handles its own sync frames so we don't want the touch
		 * handler to do it. Otherwise we'll send a great deal of needless
		 * sync events
		 */
		if (!aahbt_input_dpad_enabled())
			aahbt_input_frame(which);
	} break;

	case ATHOME_PKT_RX_BTN_V2: {
		struct bm_input_packet_v2_btn_t *evt;
		uint16_t tmp;
		long ts_delta;

		CHECK_LEN(sizeof(*evt));
		evt = (struct bm_input_packet_v2_btn_t*)data;

		tmp = get_unaligned_le16(&evt->ts_delta);
		ts_delta = (tmp == ((uint16_t)(-1)))
			 ? AAH_BT_UNKNOWN_TS_DELTA
			 : ((long)tmp * 10);
		aahbt_input_calculate_time(which, ts_delta);

		aahbt_input_send_button(which,
				(evt->data & ~0x80),
				(0 != (evt->data & 0x80)));

		aahbt_input_frame(which);
	} break;

	case ATHOME_PKT_RX_INPUT: {
		uint32_t btn_flags = 0;
		uint16_t x = 0xFFFF;
		uint16_t y = 0xFFFF;
		long     ts_delta;
		bool     has_touch, has_btn;

		CHECK_LEN(1);
		has_touch = (data[0] & ATHOME_INPUT_V1_HAS_TOUCH_FLAG) != 0;
		has_btn   = (data[0] & ATHOME_INPUT_V1_HAS_BTN_FLAG) != 0;
		ts_delta  = ATHOME_INPUT_V1_GET_TS(data[0]);
		data++;
		len--;


		if (has_btn) {
			CHECK_LEN(4);
			btn_flags = get_unaligned_le16(data);
			data += 4;
			len  -= 4;
		}

		if (has_touch) {
			CHECK_LEN(4);
			x = get_unaligned_le16(data);
			y = get_unaligned_le16(data + 2);
		}

		if (has_touch || has_btn) {
			aahbt_input_calculate_time(which, ts_delta);

			if (has_btn)
				aahbt_input_send_buttons(which, btn_flags);

			if (has_touch)
				aahbt_input_send_touch(
						which, 0, x, y,
						(x != 0xFFFF) || (y != 0xFFFF));

			aahbt_input_frame(which);
		}
	} break;

	default: {
		aahlog(MAC_FMT " : unsupported input pkt type 0x%02x\n",
		       MAC_DATA(c->MAC), type);
	} break;
	}

	return 0;
}

static int aahbt_process_audio_evt_l(struct aahbt_conn *c,
				     uint8_t *data, size_t len)
{
#ifdef CONFIG_SND_MIC_BTLE_SBC
	uint8_t type;
	uint32_t which;
#endif

	BUG_ON(!c);
	BUG_ON(!data);
	CHECK_LEN(3);

	if (c->evt_filter_flags & AAHBT_EFF_SEND_AUDIO_TO_USER)
		aahbt_usr_enqueue(BT_ATHOME_EVT_DATA, data, len, &c->MAC);

#ifdef CONFIG_SND_MIC_BTLE_SBC
	/* If we are not allowed to send this payload to the system, then there
	 * is no point in wasting the time to even parse it.
	 */
	if (!(c->evt_filter_flags & AAHBT_EFF_SEND_AUDIO_TO_SYSTEM))
		return 0;

	/* Repack the payload so we don't have to deal with the type byte in the
	 * middle of our structure.
	 */
	type = data[2];
	data[2] = data[1];
	data[1] = data[0];
	data++;
	len--;

	which = (((uint8_t*)c) - ((uint8_t*)connection_states)) / sizeof(*c);
	BUG_ON(which >= ATHOME_RMT_MAX_CONNS);

	athome_bt_audio_dec(which,
			    type - ATHOME_PKT_RX_AUDIO_0,
			    data, len, true);
#endif

	return 0;
}

static int aahbt_process_acl_skb_l(struct sk_buff *skb)
{
	struct aahbt_conn* c;
	struct hci_acl_hdr* h;
	uint8_t* data;
	size_t len;
	uint16_t handle;
	uint8_t type = 0;

	BUG_ON(NULL == skb);

	len = skb->len;
	CHECK_LEN(sizeof(*h));

	h  = (struct hci_acl_hdr*)skb->data;
	data  = (uint8_t*)(h + 1);
	len -=  sizeof(*h);

	CHECK_LEN(get_unaligned_le16(&h->dlen));
	handle = get_unaligned_le16(&h->handle) & ACL_CONN_MASK;
	c = aahbt_find_conn_by_handle_l(handle);

	if (!c) {
		aahlog("WARNING; dropping %d payload for untracked connection "
		       "ID %hd", len, handle);
		return -ENODEV;
	}

	if (len >= 3)
		type = data[2];

	switch (type) {
	case ATHOME_PKT_RX_INPUT:
	case ATHOME_PKT_RX_TOUCH_V2:
	case ATHOME_PKT_RX_BTN_V2:
		aahbt_process_input_evt_l(c, data, len);
		break;

	case ATHOME_PKT_RX_AUDIO_0:
	case ATHOME_PKT_RX_AUDIO_1:
	case ATHOME_PKT_RX_AUDIO_2:
	case ATHOME_PKT_RX_AUDIO_3:
		aahbt_process_audio_evt_l(c, data, len);
		break;

	/* If we see a power mode switch packet, update the diag LED state
	 * before passing the message through to the service level.
	 */
	case ATHOME_PKT_RX_MODESWITCH:
		if (len >= sizeof(struct bm_pkt_modeswitch_t) + 1)
			aahbt_input_led_show_event(
					(data[3] == ATHOME_PKT_PMODE_ACTIVE)
					? HACK_LED_EVENT_AWAKE
					: HACK_LED_EVENT_ASLEEP);
	/* Deliberate fallthrough to default case */

	default:
		aahbt_usr_enqueue(BT_ATHOME_EVT_DATA, data, len, &c->MAC);
		break;
	}

	return 0;
}
#undef CHECK_LEN

static bool aahbt_process_rx_msgs_l(void)
{
	bool did_work = false;
	while (!skb_queue_empty(&rx_msgs)) {
		struct sk_buff *skb;
		uint8_t pt;

		if (aahbt_should_quit())
			break;

		skb = skb_dequeue(&rx_msgs);
		if (NULL == skb)
			continue;

		did_work = true;
		pt = bt_cb(skb)->pkt_type;

		switch (pt) {
		case HCI_ACLDATA_PKT:
			aahbt_process_acl_skb_l(skb);
			break;
		case HCI_EVENT_PKT:
			aahbt_process_evt_skb_l(skb);
			break;
		default:
			aahlog("WARNING; unrecognized HCI pkt type 0x%02x\n",
					pt);
			break;
		}

		kfree_skb(skb);
	}

	return did_work;
}

static bool aahbt_process_tx_msgs_l(void)
{
	/* Use a static iterator for round-robining over our current connection
	 * states to ensure fair TX BW allocation across the set of active
	 * devices.
	 */
	static size_t rr;
	bool did_work = false;

	while (!aahbt_should_quit() &&
	       cur_total_tx_pending &&
	       aahbt_splitter_may_tx_data()) {
		static struct aahbt_conn *c;
		struct sk_buff *skb;
		size_t i, ndx;

		/* Find the next connection in the RR sequence which has data
		 * waiting to be sent (note; we pause all data transmission
		 * while encrypting or re-keying).
		 */
		for (i = 0; i < ARRAY_SIZE(connection_states); ++i) {
			ndx = (rr + i) % ARRAY_SIZE(connection_states);
			c = connection_states + ndx;
			if (!skb_queue_empty(&c->tx_pending) &&
			   (c->state != CONN_STATE_ENCRYPTING))
				break;
		}

		/* Bookkeeping indicates that we should have packets to send, if
		 * we could not find any, we have some form of bug.
		 */
		BUG_ON(i >= ARRAY_SIZE(connection_states));

		/* Bump the RR tracker */
		rr = (ndx + 1) % ARRAY_SIZE(connection_states);

		/* Grab the payload to send */
		skb = skb_dequeue(&c->tx_pending);
		BUG_ON(NULL == skb);
		aahbt_splitter_send_data(c->handle, skb);

		/* Stay awake for 10 seconds */
		wake_lock_timeout(&wl_suspend, 10 * HZ);

		/* update bookkeeping and go around again. */
		cur_total_tx_pending--;
		did_work = true;
	}

	/* If we pooped anything off of any pending tx queue, make sure to inform
	 * user-land that there is now room to queue more data.
	 */
	if (did_work)
		aahbt_usr_signal_tx_has_room();

	return did_work;
}

static int aahbt_thread(void *unusedData)
{
	wake_lock_init(&wl_suspend, WAKE_LOCK_SUSPEND, "aahbt");
	mutex_lock(&state_lock);

	if (aahbt_host_setup()) {
		/* TODO : what to do here?  This should never fail; chip bounce
		 * may be the only option, but its unclear how to coordinate
		 * such an operation with bluedroid.
		 */
		aahlog("host setup failed\n");
		goto shutdown;
	}

	/* Clear white list to make sure no device filtering is going on. */
	aahbt_clear_white_list_l();

	/* While its not time to shutdown, do work */
	while (!aahbt_should_quit()) {
		uint64_t now            = aahbt_get_time();
		uint32_t timeout_msec   = INFINITE;
		size_t i;

		/* If we should be listening for advertisements, but are not, do
		 * so.  Conversely, if we should not be listening but are, stop.
		 */
		if (is_listening != should_be_listening) {
			u32 tmp = aahbt_try_toggle_listen_state_l(now);

			if (tmp < timeout_msec)
				timeout_msec = tmp;

			/* Time has passed while we sent commands to the radio.
			 * We need to update now with the current time.
			 */
			now = aahbt_get_time();
		}

		/* Go over our current list of connections states, and for
		 * connections which should be in the process of connecting,
		 * deal with any timeouts.  In particular, we want to...
		 *
		 * 1) Handle timeout for any connection state which would like
		 *    to be connected, but is not currently.
		 * 2) Of the set of connections in progress, find the minimum
		 *    timeout and consider whether or not it should be the work
		 *    thread timeout.
		 */
		for (i = 0; i < ARRAY_SIZE(connection_states); i++) {
			struct aahbt_conn *c = connection_states + i;
			uint32_t tmp;

			tmp = aahbt_handle_connection_timeout_l(now, c);
			if (tmp < timeout_msec)
				timeout_msec = tmp;
		}

		/* If we have no connection operation in progress, check to see
		 * if there is a connection state which would like to start the
		 * process of connecting and kick off the operation if
		 * appropriate.
		 */
		for (i = 0; i < ARRAY_SIZE(connection_states); i++) {
			uint32_t tmp;

			if (connection_in_progress)
				break;

			tmp = aahbt_connect_if_needed_l(now,
							connection_states + i);
			if (tmp < timeout_msec)
				timeout_msec = tmp;
		}

		/* Process messages received from the splitter level while we
		 * have events to process.
		 */
		if (aahbt_process_rx_msgs_l())
			timeout_msec = 0;

		if (aahbt_should_quit())
			goto shutdown;

		/* Send any pending ACL payloads while we have room */
		if (aahbt_process_tx_msgs_l())
			timeout_msec = 0;

		if (aahbt_should_quit())
			goto shutdown;

		/* If we did work this pass, then our timeout will be zero and
		 * we should go around again looking for more to do.  Otherwise,
		 * wait until there is something we should be doing.
		 */
		if (!timeout_msec)
			continue;

		mutex_unlock(&state_lock);
		if (INFINITE == timeout_msec)
			wait_event_interruptible(
					rx_wait,
					aahbt_thread_has_work());
		else
			wait_event_interruptible_timeout(
					rx_wait,
					aahbt_thread_has_work(),
					msecs_to_jiffies(timeout_msec));
		atomic_set(&state_chg, 0);
		mutex_lock(&state_lock);
	}

shutdown:
	mutex_unlock(&state_lock);
	wake_lock_destroy(&wl_suspend);
	return 0;
}

/******************************************************************************
 *
 * Begin Userland API support
 *
 * These functions are called by the userland API in order to manipulate the
 * state of the stack.  They should not be called by the main work thread.
 *
 ******************************************************************************/

static void aahbt_set_listening_l(bool should_listen)
{
	if (should_be_listening != should_listen) {
		should_be_listening = should_listen;
		next_listen_attempt_time = aahbt_get_time();
		aahbt_wake_thread();
	}
}

void aahbt_set_listening(bool should_listen)
{
#if 0 /* disable scanning for now.  will disable driver completely soon */
	mutex_lock(&state_lock);
	aahbt_set_listening_l(should_listen);
	mutex_unlock(&state_lock);
#endif
}

int aahbt_start_connecting(const bdaddr_t *macP, uint32_t timeout_msec)
{
	int ret = 0;
	struct aahbt_conn* c;

	BUG_ON(!macP);
	if (timeout_msec > MAX_CONNECTION_TIMEOUT_MSEC) {
		aahlog(MAC_FMT " : connection timeout too large (%d > %d)\n",
		       MAC_DATA(*macP),
		       timeout_msec,
	       	       MAX_CONNECTION_TIMEOUT_MSEC);
		ret = -EINVAL;
		goto bailout;
	}

	mutex_lock(&state_lock);

	/* Are we already tracking a connection with this MAC?
	 *
	 * If we are in the process of disconnecting, then set the flag to start
	 * the process of reconnecting once the disconnect is finished.
	 *
	 * This may be a situation where the userland service crashed,
	 * triggering a stack reset and starting the process of disconnecting
	 * with all connected remotes, but then came back and issued a
	 * connection request before the disconnect/cleanup has finished.
	 *
	 * In theory, this should not happen during normal operation as the
	 * new instance of the service should be waiting for an advertisement
	 * from the remote before starting the process of re-connecting, and the
	 * remote should not be advertising until after the kernel level
	 * finishes disconnecting from it.  That said, there are some situations
	 * where this could happen, although they are extremely unlikely.  One
	 * example would be, the service crashes at exactly the same time as the
	 * remote crashes and reboots.  The kernel starts the disconnection
	 * process, but the remote is already advertising because it crashed and
	 * rebooted.  The new service instance could see this advertisement and
	 * attempt to start the connection process before the kernel has
	 * realized that remote has gone away for good and is actually
	 * disconnected.
	 */
	c = aahbt_find_conn_by_mac_l(macP);
	if (c) {
		BUG_ON(CONN_STATE_INVALID == c->state);

		switch (c->state) {
		case CONN_STATE_DISCONNECTING:
			c->reconnect_after_discon = true;
			break;

		case CONN_STATE_CONNECTING:
			c->disconnect_asap = false;
			break;

		default:
			aahlog(MAC_FMT " : attempted to connect while in "
			       "state %d\n", MAC_DATA(*macP), c->state);
			ret = -EBUSY;
			goto bailout;
		}

		c->conn_timeout = aahbt_get_time() +
				  (((uint64_t)timeout_msec) * 1000000);
		goto bailout;
	}

	/* Looks like we know nothing about this guy.  Try to find a free
	 * connection state and start the process of connecting to the target
	 * MAC address.
	 */
	c = aahbt_find_free_conn_l();
	if (!c) {
		aahlog(MAC_FMT " : Can't connect, no free state\n",
		       MAC_DATA(*macP));
		ret = -ENOMEM;
		goto bailout;
	}

	/* Setup the info for our connection attempt, then poke the work thread.
	 * The next time we have the chance to make a connection attempt, the
	 * work thread will do so.
	 */
	c->conn_timeout  = aahbt_get_time() + (((uint64_t)timeout_msec) * 1000000);
	c->state         = CONN_STATE_PRE_CONNECTING;
	c->conn_settings = default_conn_settings;
	bacpy(&c->MAC, macP);
	aahlog(MAC_FMT " : connection attempt scheduled.\n", MAC_DATA(*macP));
	aahbt_wake_thread();

bailout:
	mutex_unlock(&state_lock);
	return ret;
}

int aahbt_start_disconnecting_l(struct aahbt_conn* c)
{
	BUG_ON(!c);

	aahlog(MAC_FMT " : start disconnect while in state %d\n",
	       MAC_DATA(c->MAC), c->state);

	switch (c->state) {
	/* Have we not even started the process of connecting yet?  If so, this
	 * is simple, just recycle the connection state, queue a message to the
	 * user, and return success.
	 */
	case CONN_STATE_PRE_CONNECTING:
		// 0x16 ==> reason == local user req.
		aahbt_send_disconnected_to_usr_l(c, 0x16);
		aahbt_reset_conn_state_l(c, false);
		break;

	/* Are we in the process of connecting but have not connected yet?  If
	 * so, this is a little more complicated.  Flag this connecting state as
	 * needing a disconnect ASAP, set its timeout to now, and wake up the
	 * work thread.  If the device manages to become connected, then it will
	 * immediately start the process of disconnection and eventually inform
	 * userland of the disconnect.  If it fails to become connected, it will
	 * just return a connect result indicating the failure instead.
	 */
	case CONN_STATE_CONNECTING:
		c->disconnect_asap = true;
		c->conn_timeout = aahbt_get_time();
		aahbt_wake_thread();
		break;

	/* If we are in any of the connected states, then start the process of
	 * disconnecting.
	 */
	case CONN_STATE_CONNECTED:
	case CONN_STATE_ENCRYPTING:
	case CONN_STATE_ENCRYPTED:
		aahbt_send_disconnect_l(c);
		break;

	/* If we are already in the process of disconnecting, then just keep
	 * waiting.  We will disconnect eventually.
	 */
	case CONN_STATE_DISCONNECTING:
		break;

	default:
		BUG_ON(true);
		return -EINVAL;
	}

	return 0;
}

int aahbt_start_disconnecting(const bdaddr_t *macP)
{
	int ret;
	struct aahbt_conn* c;

	BUG_ON(!macP);
	mutex_lock(&state_lock);

	c = aahbt_find_conn_by_mac_l(macP);
	if (!c) {
		ret = -ENODATA;
		goto bailout;
	}

	ret = aahbt_start_disconnecting_l(c);

bailout:
	mutex_unlock(&state_lock);
	return ret;
}

int aahbt_start_encrypting(const bdaddr_t *macP, const uint8_t *key)
{
	/* Values set by the remote control protocol spec */
	static const uint16_t master_diversifier = ENCR_DIV;
	static const uint8_t  master_rand_data[] = ENCR_RND_NUM;

	int ret = 0;
	size_t qlen;
	struct aahbt_conn* c;
	struct hci_cp_le_start_enc cmd;
	struct {
		struct hci_event_hdr     ev_hdr;
		struct hci_ev_cmd_status cmd_status;
	} __packed resp;

	BUG_ON(!macP);
	BUG_ON(!key);

	mutex_lock(&state_lock);

	c = aahbt_find_conn_by_mac_l(macP);
	if (!c) {
		aahlog(MAC_FMT " : device not found when attempting to "
		       "encrypt\n", MAC_DATA(*macP));
		ret = -ENODATA;
		goto bailout;
	}

	if ((CONN_STATE_CONNECTED != c->state) &&
	    (CONN_STATE_ENCRYPTED != c->state)) {
		aahlog(MAC_FMT " : attempted to encrypt while in "
		       "state %d\n", MAC_DATA(*macP), c->state);
		ret = -EBUSY;
		goto bailout;
	}

	put_unaligned_le16(c->handle, &cmd.handle);
	put_unaligned_le16(master_diversifier, &cmd.ediv);
	memcpy(cmd.rand, master_rand_data, sizeof(cmd.rand));
	memcpy(cmd.ltk, key, sizeof(cmd.ltk));

	ret = aahbt_simple_cmd_l(HCI_OGF_LE, HCI_LE_Start_Encryption,
			             sizeof(cmd), (const uint8_t*)&cmd,
			             sizeof(resp), (uint8_t*)&resp);
	if (!ret && resp.cmd_status.status)
		ret = -ECOMM;

	if (ret) {
		aahlog(MAC_FMT " : failed begin encryption operation "
		       "(ret = %d)\n",
		       MAC_DATA(*macP), ret);
		goto bailout;
	}

	/* Yay, encryption has started for this connection.  Any messages
	 * waiting to be transmitted need to be ignored for the time being;
	 * remove their count from the total_pending count so that the main work
	 * thread does not think that there is work to do because of pending
	 * transmissions only to find that none of the devices have data which
	 * may be transmitted right now.
	 */
	c->state = CONN_STATE_ENCRYPTING;
	qlen = skb_queue_len(&c->tx_pending);
	BUG_ON(qlen > cur_total_tx_pending);
	cur_total_tx_pending -= qlen;

bailout:
	mutex_unlock(&state_lock);
	return ret;
}

void aahbt_get_default_conn_settings(struct aahbt_conn_settings_t *s)
{
	BUG_ON(!s);
	mutex_lock(&state_lock);
	memcpy(s, &default_conn_settings, sizeof(*s));
	mutex_unlock(&state_lock);
}

void aahbt_set_default_conn_settings(const struct aahbt_conn_settings_t *s)
{
	BUG_ON(!s);
	mutex_lock(&state_lock);
	memcpy(&default_conn_settings, s, sizeof(*s));
	mutex_unlock(&state_lock);

	aahlog("default_conn_settings changed to"
		", interval = %d, latency = %d, timeout = %d msec\n",
		s->conn_interval, s->conn_latency, s->svc_timeout);
}

bool aahbt_tx_pipeline_has_room(void)
{
	bool ret;

	mutex_lock(&state_lock);
	ret = (cur_total_tx_pending < max_total_tx_pending);
	mutex_unlock(&state_lock);

	return ret;
}

int aahbt_queue_acl_for_tx(const bdaddr_t *macP, const void* data, size_t len)
{
	int ret = 0;
	struct aahbt_conn* c;
	struct sk_buff *skb;

	mutex_lock(&state_lock);

	BUG_ON(!macP);
	BUG_ON(!data);

	/* Payload size restrictions are enforced at the time ::write is called
	 * at the user-land interface level.  Violations of payload size
	 * restrictions at this point are programming errors.
	 */
	BUG_ON(len > AAH_MAX_BTLE_PAYLOAD_SZ);
	BUG_ON(len < AAH_MIN_BTLE_PAYLOAD_SZ);

	/* Make sure we know about the connection the user is interested in. */
	c = aahbt_find_conn_by_mac_l(macP);
	if (!c) {
		ret = -ENODEV;
		goto bailout;
	}

	/* Make sure that the connection is in a state which can accept data for
	 * transmission.
	 */
	if ((CONN_STATE_CONNECTED != c->state) &&
	    (CONN_STATE_ENCRYPTED != c->state)) {
		ret = -ENETDOWN;
		goto bailout;
	}

	/* Enforce our flow control */
	if (cur_total_tx_pending >= max_total_tx_pending) {
		ret = -EAGAIN;
		goto bailout;
	}

	/* Finally, pack the payload and queue it for transmission. TX overhead
	 * is 4 bytes, 2 bytes for connID and flags and 2 bytes for length.
	 */
	if (NULL == (skb = bt_skb_alloc(len + 4, GFP_ATOMIC))) {
		ret = -ENOMEM;
		goto bailout;
	}

	/* All BTLE payloads are Non-flushable-first and point-to-point, so no
	 * special flags need to be packed into the connection ID.
	 */
	put_unaligned_le16(c->handle, skb->data);
	put_unaligned_le16((uint16_t)len, (uint16_t*)skb->data + 1);
	memcpy(skb->data + 4, data, len);
	skb_put(skb, len + 4);
	skb_queue_tail(&c->tx_pending, skb);
	cur_total_tx_pending++;
	aahbt_wake_thread();

bailout:
	mutex_unlock(&state_lock);
	return ret;
}

int aahbt_get_random_bytes(uint8_t *bytes, size_t amt)
{
	int ret;
	mutex_lock(&state_lock);

	ret = aahbt_get_random_bytes_l(bytes, amt);

	mutex_unlock(&state_lock);
	return ret;
}

int aahbt_do_aes128(const uint8_t* key, const uint8_t* in, uint8_t* out)
{
	int ret;
	mutex_lock(&state_lock);

	ret = aahbt_do_aes128_l(key, in, out);

	mutex_unlock(&state_lock);
	return ret;
}

int aahbt_get_evt_filter_flags(const bdaddr_t *macP, uint32_t *flags)
{
	int ret = 0;
	struct aahbt_conn* c;

	BUG_ON(!macP);
	BUG_ON(!flags);

	mutex_lock(&state_lock);

	c = aahbt_find_conn_by_mac_l(macP);
	if (!c)
		ret = -ENODEV;
	else
		*flags = c->evt_filter_flags;

	mutex_unlock(&state_lock);
	return ret;
}

int aahbt_set_evt_filter_flags(const bdaddr_t *macP, uint32_t flags)
{
	int ret = 0;
	struct aahbt_conn* c;

	BUG_ON(!macP);

	mutex_lock(&state_lock);

	c = aahbt_find_conn_by_mac_l(macP);
	if (!c)
		ret = -ENODEV;
	else
		c->evt_filter_flags = flags;

	mutex_unlock(&state_lock);
	return ret;
}

void aahbt_reset_stack(void)
{
	size_t i;

	aahlog("stack reset requested\n");
	mutex_lock(&state_lock);

	/* If we are currently listening for advertisements, we should stop. */
	aahbt_set_listening_l(false);

	/* Start the process of shutting down any connections */
	for (i = 0; i < ARRAY_SIZE(connection_states); i++) {
		struct aahbt_conn *c = connection_states + i;

		if (CONN_STATE_INVALID != c->state)
			aahbt_start_disconnecting_l(c);
	}

	mutex_unlock(&state_lock);
}

/******************************************************************************
 *
 *
 * End Userland API support
 *
 *
 ******************************************************************************/

void aahbt_stack_prepare(void)
{
	static struct task_struct *thread;

	if (!aah_thread) {
		aahlog("creating LE stack thread\n");
		aahbt_input_reset_state();
		atomic_set(&in_shutdown, 0);
		thread = kthread_create(aahbt_thread, NULL, "athome_bt");
		if (IS_ERR(thread))
			aahlog("Failed to start stack thread\n");
		else
			aah_thread = thread;
	}
	smp_wmb();
}

void aahbt_stack_start(void)
{
	smp_rmb();
	BUG_ON(!aah_thread);

	/* Set the flag allowing the userland to interact with the APIs and
	 * populate the queue a message informing the service level of the new
	 * dirver generation.
	 */
	aahbt_usr_purge_queues();
	aahbt_usr_set_stack_ready(true);
	aahbt_usr_queue_ready_message();

	/* Wake the main work thread up and let it run. */
	aahlog("starting LE stack!\n");
	wake_up_process(aah_thread);

}

static void aahbt_purge_queues(void)
{
	while (!skb_queue_empty(&rx_msgs))
		kfree_skb(skb_dequeue(&rx_msgs));
}

void aahbt_stack_shutdown(void)
{
	unsigned long flags;
	int i;

	/* Set the flag to start the shutdown of the core, and the flag to
	 * prevent further API interaction from userland.
	 */
	atomic_set(&in_shutdown, 1);
	aahbt_usr_set_stack_ready(false);

	/* Now shut down the work thread if it happens to be running. */
	if (aah_thread) {
		aahlog("shutting down thread\n");
		atomic_set(&state_chg, 1); /* force wake */
	}

	/* unblock anyone waiting for a cmd response */
	complete_all(&cmd_response);

	if (aah_thread) {
		wake_up_interruptible(&rx_wait);
		kthread_stop(aah_thread);
		aahlog("thread shut down\n");
		aah_thread = NULL;
	}

	aahbt_usr_sync_with_userland();
	atomic_set(&state_chg, 0);
	INIT_COMPLETION(cmd_response);

	/* Work thread is stopped, userland threads have all been evicted.  Now
	 * we can go about cleaning up all of our state.
	 */
	aahbt_purge_queues();
	aahbt_usr_purge_queues();
	aahbt_input_reset_state();

	spin_lock_irqsave(&stack_state_lock, flags);
	inflight_opcode = INVALID_OCF_OGF;
	cmd_rsp_buf = NULL;
	spin_unlock_irqrestore(&stack_state_lock, flags);

	for (i = 0; i < ARRAY_SIZE(connection_states); i++) {
		struct aahbt_conn *c = connection_states + i;
		aahbt_purge_pending_tx_l(c);
		aahbt_reset_conn_state_l(c, false);
	}

	BUG_ON(cur_total_tx_pending);

	connection_in_progress = false;
	is_listening           = false;
	should_be_listening    = false;

	/* Update the connection light to reflect the fact that we are now
	 * disconnected.
	 */
	aahbt_input_led_show_event(HACK_LED_EVENT_DISCONNECT);

	/* Finally, put a message into the usr queue telling the userland that
	 * the driver is being reset.
	 */
	aahbt_usr_queue_ready_message();
}

int aahbt_stack_init_module(void)
{
	int ret;
	size_t i;

	aahbt_splitter_init();
	skb_queue_head_init(&rx_msgs);

	for (i = 0; i < ARRAY_SIZE(connection_states); i++) {
		struct aahbt_conn *c = connection_states + i;
		memset(c, 0, sizeof(*c));
		skb_queue_head_init(&c->tx_pending);
		aahbt_reset_conn_state_l(c, false);
	}

	ret = aahbt_usr_init();
	if (ret) {
		aahlog("failed to register misc device\n");
		return ret;
	}

	ret = aahbt_input_init();
	if (ret) {
		aahlog("failed to register input device\n");
		aahbt_usr_deinit();
		return ret;
	}

	return 0;
}

void aahbt_stack_exit_module(void)
{
	BUG_ON(aah_thread);

	aahbt_purge_queues();
	aahbt_input_deinit();
	aahbt_usr_deinit();
}
