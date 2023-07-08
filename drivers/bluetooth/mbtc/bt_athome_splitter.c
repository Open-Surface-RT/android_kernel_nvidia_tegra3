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
#include <linux/semaphore.h>
#include <linux/kernel.h>
#include "bt_athome_hci_extra.h"
#include "bt_athome_le_stack.h"
#include "bt_athome_splitter.h"
#include "bt_athome_logging.h"
#include "bt_athome_util.h"

/* If we can manage it, we would like 4 ACL buffers for packet transmission */
#define AAH_TX_PKT_TGT 4

/*
 * Note: we assume all splitter functions are called from the main
 * thread of bt driver. No synchronization is necessary between them.
 */
/* We allow only one command in flight at a time */
static int cmd_credit = 1;
/*
 * Only one copy of this data exists in the kernel:
 * This is the driver-private data for the MBT driver whom we're piggy-backing
 * off. We make no use of it ourselves, but we must pass it back to MBT anytime
 * we call into it. MBT itself is thread-safe and handles all necesary safety
 * and locking for this data.
 */
static void *drv_priv = NULL;

/* event masks for things we need */
  /* disconnect, encr_change, encr_refresh */
static const uint64_t evtsNeeded = 0x0000800000000090ULL;
  /* LE meta event*/
static const uint64_t evtsLE = 0x2000000000000000ULL;

/* State used by the splitter level to track active LE connections.  Messages
 * which contain a connection ID are checked against this set to determine if
 * they should be passed to the AAH level of things, or if they should be sent
 * up the normal processing path.
 *
 * In addition, this set is use to track ACL data transmissions in flight and
 * properly manage the flow-control bookkeeping.
 *
 * The set is manipulated by only the Marvell driver thread, and the driver
 * thread always processes messages in the order which they came from the
 * BT/BTLE chip, therefor the set of active LE connections should always be
 * correct with respect to the current message being processed by the Marvell
 * driver thread.
 */
static DEFINE_SPINLOCK(conn_tracking_lock);
struct aahbt_le_conn {
	uint16_t conn_id;
	size_t tx_in_flight;
};
static struct aahbt_le_conn aahbt_tracked_le_conns[ATHOME_RMT_MAX_CONNS];
static uint32_t aahbt_max_tx_in_flight;
static uint32_t aahbt_cur_tx_in_flight;

static inline void reset_tracked_le_conns(void)
{
	size_t i;
	spin_lock(&conn_tracking_lock);

	for (i = 0; i < ARRAY_SIZE(aahbt_tracked_le_conns); ++i) {
		aahbt_tracked_le_conns[i].conn_id = INVALID_CONN_ID;
		aahbt_tracked_le_conns[i].tx_in_flight = 0;
	}

	aahbt_cur_tx_in_flight = 0;

	spin_unlock(&conn_tracking_lock);
}

static inline struct aahbt_le_conn *cid_find_le_conn_l(uint16_t cid)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(aahbt_tracked_le_conns); ++i)
		if (cid == aahbt_tracked_le_conns[i].conn_id)
			return aahbt_tracked_le_conns + i;

	return NULL;
}

static inline bool cid_is_le_conn(uint16_t cid)
{
	bool ret;
	spin_lock(&conn_tracking_lock);
	ret = (NULL != cid_find_le_conn_l(cid));
	spin_unlock(&conn_tracking_lock);
	return ret;
}

static bool aahbt_filter_disconn(const void *dataP)
{
	const struct hci_ev_disconn_complete *evt = dataP;
	uint16_t cid = get_unaligned_le16(&evt->handle);
	size_t i;
	bool ret = false;

	spin_lock(&conn_tracking_lock);

	for (i = 0; i < ARRAY_SIZE(aahbt_tracked_le_conns); ++i) {
		struct aahbt_le_conn *c = aahbt_tracked_le_conns + i;
		if (c->conn_id == cid) {
			BUG_ON(aahbt_cur_tx_in_flight < c->tx_in_flight);

			aahbt_cur_tx_in_flight -= c->tx_in_flight;
			c->conn_id = INVALID_CONN_ID;
			c->tx_in_flight = 0;
			aahbt_purge_acl_for_conn_id(drv_priv, cid);

			ret = true;
			break;
		}
	}

	spin_unlock(&conn_tracking_lock);
	return ret;
}

static bool aahbt_filter_encr_change(const void *dataP)
{
	const struct hci_ev_encrypt_change *evt = dataP;
	return cid_is_le_conn(get_unaligned_le16(&evt->handle));
}

static bool aahbt_filter_encr_refresh(const void *dataP)
{
	const struct hci_ev_encrypt_refresh *evt = dataP;
	return cid_is_le_conn(get_unaligned_le16(&evt->handle));
}

static bool aahbt_filter_le_meta(const void *dataP)
{
	const struct hci_ev_le_conn_complete *evt;
	const uint8_t *d = dataP;
	/* We always pass LE meta events to the AAH half of things.  This
	 * filter's main job is to look for successful LE connection completed
	 * events and record the connection ID in the aahbt_tracked_le_conns
	 * array used for filtering subsequent messages.
	 */
	if (HCI_EV_LE_CONN_COMPLETE != d[0])
		return true;

	evt = (struct hci_ev_le_conn_complete *)(d + 1);
	if (!evt->status) {
		uint16_t cid = get_unaligned_le16(&evt->handle);
		size_t i;

		spin_lock(&conn_tracking_lock);
		for (i = 0; i < ARRAY_SIZE(aahbt_tracked_le_conns); ++i) {
			struct aahbt_le_conn *c = aahbt_tracked_le_conns + i;
			if (c->conn_id == INVALID_CONN_ID) {
				c->conn_id = cid;
				BUG_ON(c->tx_in_flight);
				break;
			}
		}
		spin_unlock(&conn_tracking_lock);

		/* At the point where we are receiving a connection completed
		 * event, we should always have room in our tracked array.  If
		 * we don't it can only be because of a bookkeeping bug at the
		 * AAH stack level.
		 */
		BUG_ON(i >= ARRAY_SIZE(aahbt_tracked_le_conns));
	}

	return true;
}

static void aahbt_filter_ftr_page_0(uint8_t *features)
{
	if (features[4] & 0x40) {
		aahlog("features[0]: clearing 4,6 - LE supported\n");
		features[4] &= ~0x40;
	}
	if (features[6] & 0x02) {
		aahlog("features[0]: clearing 6,1 - EDR/LE simult\n");
		features[4] &= ~0x02;
	}
}

void aahbt_reset(void)
{
	aahlog("resetting\n");

	smp_rmb();
	drv_priv = NULL;
	smp_wmb();

	aahbt_stack_shutdown();

	if (LOG_BT_CREDIT)
		aahlog("resetting cmd_credit to 1\n");
	cmd_credit = 1;

	reset_tracked_le_conns();

	aahlog("reset complete\n");
}

static int aahbt_filter_cmd_complete(const uint8_t *rx_buf,
				     uint32_t len,
				     bool *enq_if_oursP)
{
	const struct hci_ev_cmd_complete *evt =
			(const struct hci_ev_cmd_complete *)
			(rx_buf + HCI_EVENT_HDR_SIZE);
	uint16_t opcode = get_unaligned_le16(&evt->opcode);
	uint16_t ogf = hci_opcode_ogf(opcode);
	uint16_t ocf = hci_opcode_ocf(opcode);
	int i, forus = 0;

	if (aahbt_process_cmd_status_or_complete(opcode, rx_buf, len)) {
		forus = 1;
		*enq_if_oursP = 0;
	}

	if (ogf == HCI_OGF_Controller_And_Baseband && ocf == HCI_Reset) {

		/*
		 *	any and all command credits we have/had go out the
		 *	window after a reset, and we instead have one. This
		 *	code assures our semaphore acts that way too.
		 */
		cmd_credit = 1;
		if (LOG_BT_CREDIT)
			aahlog("resetting cmd_credit to 1 on reset\n");

	 } else if (ogf == HCI_OGF_Controller_And_Baseband &&
		ocf == HCI_Read_LE_Host_Support && !forus) {

		struct read_le_host_supported_reply *repl =
			(struct read_le_host_supported_reply*)(evt + 1);

		BUG_ON(len < sizeof(*repl) + sizeof(*evt));

		if (repl->le)
			aahlog("host_support: clearing le\n");
		if (repl->simul)
			aahlog("host_support: clearing simul\n");
		repl->le = 0;
		repl->simul = 0;

	} else if (ogf == HCI_OGF_Informational) {

		if (ocf == HCI_Read_Local_Version_Information && !forus) {
			struct read_local_version_reply *repl =
				(struct read_local_version_reply*)(evt + 1);

			BUG_ON(len < sizeof(*repl) + sizeof(*evt));

			if (!repl->status) {

				if (repl->hci_version >= BT_HCI_VERSION_4) {
					aahlog("Down BT HCI version from %d\n",
						repl->hci_version);
					repl->hci_version = BT_HCI_VERSION_3;
				}
				if (repl->lmp_version >= BT_LMP_VERSION_4) {
					aahlog("Down bt LMP version from %d\n",
						repl->lmp_version);
					repl->lmp_version = BT_LMP_VERSION_3;
				}
			}
		}
		if (ocf == HCI_Read_Local_Supported_Commands && !forus) {
			struct read_support_cmds_reply *repl =
				(struct read_support_cmds_reply*)(evt + 1);

			BUG_ON(len < sizeof(*repl) + sizeof(*evt));

			if (repl->commands[24] & 0x60)
				aahlog("supported commands: ~LE in byte 24\n");
			repl->commands[24] &= ~0x60;

			/* these are LE commands */
			for (i = 25; i <= 28; i++) {
				if (repl->commands[i])
					aahlog("supported commands: "
						"clearing byte %d\n", i);
				repl->commands[i] = 0;
			}
		}
		if (ocf == HCI_Read_Local_Supported_Features && !forus) {
			struct read_support_ftrs_reply *repl =
				(struct read_support_ftrs_reply*)(evt + 1);

			BUG_ON(len < sizeof(*repl) + sizeof(*evt));

			aahbt_filter_ftr_page_0(repl->features);
		}
		if (ocf == HCI_Read_Local_Supported_Extended_Features &&
			!forus) {
			struct read_support_ext_ftrs_reply *repl =
				(struct read_support_ext_ftrs_reply*)(evt + 1);

			BUG_ON(len < sizeof(*repl) + sizeof(*evt));

			if (repl->page_nr == 0)
				aahbt_filter_ftr_page_0(repl->features);
			else if (repl->page_nr == 1) {

				if (repl->features[0] & 0x02) {
					aahlog("features[1]: clearing 0,1 - "
						"LE supported\n");
					repl->features[4] &= ~0x02;
				}
				if (repl->features[0] & 0x04) {
					aahlog("features[1]: clearing 0,2 - "
						"EDR/LE simult\n");
					repl->features[4] &= ~0x04;
				}
			}
		}
		if (ocf == HCI_Read_Buffer_Size) {
			struct hci_rp_read_buffer_size *repl =
				(struct hci_rp_read_buffer_size*)(evt + 1);
			uint16_t pkts, normal_pkts, aah_pkts, reported_pkts;

			BUG_ON(len < sizeof(*repl) + sizeof(*evt));

			pkts = get_unaligned_le16(&repl->acl_max_pkt);

			/* If we cannot hit our target number of payloads and
			 * leave at least that many payloads for the normal
			 * stack, then give 1/2 rounded up to the normal stack,
			 * and 1/2 rounded down to AAH.
			 */
			if (pkts < (2 * AAH_TX_PKT_TGT))
				aah_pkts = pkts >> 1;
			else
				aah_pkts = AAH_TX_PKT_TGT;

			normal_pkts = pkts - aah_pkts;

			reported_pkts = forus ? aah_pkts : normal_pkts;
			aahlog("Chip has %hu buffers, telling %s that it may "
			       "have %hu buffers.\n",
			       pkts,
			       forus ? "aahbt" : "bluedroid",
			       reported_pkts);

			put_unaligned_le16(reported_pkts, &repl->acl_max_pkt);

			/* If max_tx_in_flight has been assigned a value, then
			 * it should never change.  ASSERT this.
			 */
			BUG_ON(aahbt_max_tx_in_flight &&
			      (aahbt_max_tx_in_flight != aah_pkts));

		      	aahbt_max_tx_in_flight = aah_pkts;
		}
	} else if (ogf == HCI_OGF_LE && !forus) {
		uint8_t len = ((uint8_t *)rx_buf)[-1];
		char log_buf[len * 3];
		/*
		 * This is ugly and bad. This code here is for LOGGING ONLY and
		 * it logs our userspace EDR stack doing bad things (sending LE
		 * commands to a chip claiming no LE support). Once these bugs
		 * in the userspace stack are fixed, this code can go away
		 */
		aahlog_bytes(log_buf, sizeof(log_buf), rx_buf, len);
		aahlog("LE command complete not for us "
		       "(ogf=0x%x ocf=0x%x):\n\t%s\n",
		       ogf, ocf, log_buf);
	} else if (ogf == HCI_OGF_Controller_And_Baseband &&
						ocf == HCI_Set_Event_Mask) {

		/* it is now safe to start our stack */
		aahbt_stack_start();
	}

	return forus;
}

static int aahbt_filter_cmd_status(const uint8_t *rx_buf,
				   uint32_t len,
				   bool *enq_if_oursP)
{
	const struct hci_ev_cmd_status *evt =
			(const struct hci_ev_cmd_status *)
			(rx_buf + HCI_EVENT_HDR_SIZE);
	uint16_t opcode = get_unaligned_le16(&evt->opcode);
	uint16_t ogf = hci_opcode_ogf(opcode);
	uint16_t ocf = hci_opcode_ocf(opcode);
	bool forus = 0;

	if (aahbt_process_cmd_status_or_complete(opcode, rx_buf, len)) {
		forus = 1;
		*enq_if_oursP = 0;
	}

	if (ogf == HCI_OGF_LE && !forus) {
		/*
		 * This is ugly and bad. This code here is for LOGGING ONLY and
		 * it logs our userspace EDR stack doing bad things (sending LE
		 * commands to a chip claiming no LE support). Once these bugs
		 * in the userspace stack are fixed, this code can go away
		 */
		uint8_t len = ((uint8_t *)rx_buf)[-1];
		char log_buf[len * 3];
		aahlog_bytes(log_buf, sizeof(log_buf), rx_buf, len);
		aahlog("LE command status not for us (ogf=0x%x ocf=0x%x):",
			ogf, ocf);
	}

	return forus;
}

static int aahbt_filter_num_comp_pkt(void *dataP, uint32_t *rx_len)
{
	/* Go over this NumberOfCompletedPackets event and pull out all of the
	 * records related to LE connections so that the main stack does not
	 * hear about packets being completed for connections it does not know
	 * about.  At the same time, build one or more new NumOfCompletedPackets
	 * events to send to the AAH LE portion of the stack which contain the
	 * completed packets info for active LE connections.
	 */
	struct num_comp_pkts *evt = dataP;
	struct comp_pkts_info *in = evt->handles, *out = evt->handles;
	int num_skipped = 0;
	int i, new_creds = 0;

	spin_lock(&conn_tracking_lock);

	for (i = 0; i < evt->num_hndl; i++, in++) {
		uint16_t conn = get_unaligned_le16(&in->handle);
		uint16_t pkts = get_unaligned_le16(&in->count);
		struct aahbt_le_conn *c = cid_find_le_conn_l(conn);

		if (c) {
			BUG_ON(c->tx_in_flight < pkts);
			BUG_ON(aahbt_cur_tx_in_flight < pkts);

			c->tx_in_flight -= pkts;
			aahbt_cur_tx_in_flight -= pkts;

			new_creds += pkts;
			num_skipped++;
		} else {
			put_unaligned_le16(conn, &out->handle);
			put_unaligned_le16(pkts, &out->count);
			out++;
		}
	}

	spin_unlock(&conn_tracking_lock);

	if (LOG_BT_EVTS)
		aahlog("got %u data credits back, hid %u handles\n",
			new_creds, num_skipped);

	*rx_len -= num_skipped * 2 * sizeof(uint16_t);
	evt->num_hndl -= num_skipped;

	if (num_skipped)
		aahbt_wake_thread();

	return !evt->num_hndl;
}

void aahbt_send_to_chip(uint32_t pkt_type,
		        const uint8_t *data,
			uint32_t len)
{
	/* See comments in bt_main.c as to why this lock needs to be held */
	spin_lock(&conn_tracking_lock);

	if (!drv_priv)
		aahlog("Attempt to send to chip with no private data\n");
	else
		aahbt_pkt_to_chip(drv_priv, pkt_type, data, len);

	spin_unlock(&conn_tracking_lock);
}

int aahbt_remote_filter_rx_data(void *priv,
				u32 pkt_type,
				u8 *rx_buf,
				u32 *rx_len)
{
	bool isours = false, enq_if_ours = true;

	smp_rmb();
	if (!drv_priv)
		drv_priv = priv;
	smp_wmb();

	if (pkt_type == HCI_EVENT_PKT) {
		uint8_t *evt_data = rx_buf + HCI_EVENT_HDR_SIZE;
		struct hci_event_hdr *evt = (struct hci_event_hdr*)rx_buf;
		struct hci_ev_cmd_complete *cmd_complete =
			(struct hci_ev_cmd_complete*)evt_data;
		struct hci_ev_cmd_status *cmd_status =
			(struct hci_ev_cmd_status*)evt_data;

		switch (evt->evt) {

		case HCI_EV_DISCONN_COMPLETE:
			isours = aahbt_filter_disconn(evt_data);
			break;

		case HCI_EV_ENCRYPT_CHANGE:
			isours = aahbt_filter_encr_change(evt_data);
			break;

		case HCI_EV_CMD_COMPLETE:
			if (cmd_complete->ncmd) {
				if (LOG_BT_CREDIT)
					aahlog("cmd complete; cmd_credit = 1\n");
				cmd_credit = 1;
			}
			isours = aahbt_filter_cmd_complete(rx_buf,
							   *rx_len,
							   &enq_if_ours);
			break;

		case HCI_EV_CMD_STATUS:
			if (cmd_status->ncmd) {
				if (LOG_BT_CREDIT)
					aahlog("cmd status; cmd_credit = 1\n");
				cmd_credit = 1;
			}
			isours = aahbt_filter_cmd_status(rx_buf,
							 *rx_len,
							 &enq_if_ours);
			break;

		case HCI_EV_NUM_COMP_PKTS:
			isours = aahbt_filter_num_comp_pkt(evt_data, rx_len);
			enq_if_ours = 0;
			break;

		case HCI_EV_ENCR_REFRESH:
			isours = aahbt_filter_encr_refresh(evt_data);
			break;

		case HCI_EV_LE_META:
			isours = aahbt_filter_le_meta(evt_data);
			break;
		}

		if (isours && enq_if_ours)
			aahbt_enqueue_msg(rx_buf, *rx_len, HCI_EVENT_PKT);
	} else if (pkt_type == HCI_ACLDATA_PKT) {

		struct hci_acl_hdr *acl = (struct hci_acl_hdr*)rx_buf;
		uint16_t cid = get_unaligned_le16(&acl->handle) & ACL_CONN_MASK;

		isours = cid_is_le_conn(cid);
		if (isours)
			aahbt_enqueue_msg(rx_buf, *rx_len, HCI_ACLDATA_PKT);
	}

	aahbt_logpacket(1, pkt_type, rx_buf, *rx_len, isours);

	return isours ? AAH_BT_PKT_DROP : AAH_BT_PKT_PROCEED;
}

int aahbt_pkt_send_req(void *priv, struct sk_buff *skb)
{
	struct hci_command_hdr *cmd = (struct hci_command_hdr*)skb->data;
	bool wasours = 0, log_it = 1, skipsem = false;
	int i, ret = AAH_BT_PKT_PROCEED;

	smp_rmb();
	if (!drv_priv)
		drv_priv = priv;
	smp_wmb();

	if (bt_cb(skb)->pkt_type == AAH_BT_PKT_TYPE_CMD) {
		bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
		wasours = 1;
	}

	if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT) {

		uint16_t ogf = hci_opcode_ogf(cmd->opcode);
		uint16_t ocf = hci_opcode_ocf(cmd->opcode);

		if (ogf == HCI_OGF_Controller_And_Baseband &&
			ocf == HCI_Reset) {

			static int first = 1;

			if (!first)
				aahlog("unexpected chip reset\n");
			first = 0;

			aahbt_reset();
			skipsem = true;

		} else if (ogf == HCI_OGF_Controller_And_Baseband &&
				ocf == HCI_Write_LE_Host_Support &&
				!wasours) {

			struct hci_cp_write_le_host_supported *params =
				(struct hci_cp_write_le_host_supported*)
								(cmd + 1);

			if (params->le)
				aahlog("EDR stack enabling LE  -> bad\n");
			if (params->simul)
				aahlog("EDR stack enabling simul -> bad\n");

			params->le = 1;
			params->simul = 1;

		} else if (ogf == HCI_OGF_Controller_And_Baseband &&
				ocf == HCI_Set_Event_Mask) {
			void* mask_loc;
			uint64_t mask, oldmask;

			mask_loc = (((uint8_t*)skb->data) +
					HCI_COMMAND_HDR_SIZE);
			mask = get_unaligned_le64(mask_loc);
			oldmask = mask;

			if ((mask & evtsNeeded) != evtsNeeded) {
				aahlog("EDR stack blocked some vital "
					"events - BAD - fixing\n");
				mask |= evtsNeeded;
			}
			if ((mask & evtsLE) && !wasours) {
				aahlog("EDR stack unmasked some events "
					"unexpectedly - OK, just weird\n");
			}
			mask |= evtsLE;
			put_unaligned_le64(mask, mask_loc);

			aahlog("modified event mask 0x%08X%08X -> 0x%08X%08X"
				"\n", (uint32_t)(oldmask >> 32),
				(uint32_t)oldmask, (uint32_t)(mask >> 32),
				(uint32_t)mask);

			/* it is now safe to prepare our stack */
			aahbt_stack_prepare();

		} else if (ogf == HCI_OGF_LE && !wasours) {

			uint8_t buf[64];
			uint8_t *end = NULL;
			struct hci_event_hdr *evt = (struct hci_event_hdr*)buf;
			struct hci_ev_cmd_complete *comp =
					(struct hci_ev_cmd_complete*)(evt + 1);
			struct hci_rp_le_read_buffer_size *bs =
				(struct hci_rp_le_read_buffer_size*)(comp + 1);
			uint8_t *stat = (uint8_t*)(comp + 1);

			evt->evt = HCI_EV_CMD_COMPLETE;
			comp->ncmd = 1;
			put_unaligned_le16(cmd->opcode, &comp->opcode);

			aahlog("LE command (0x%x) from EDR stack!\n", ocf);
			switch (ocf) {
			case HCI_LE_Read_Buffer_Size:
				bs->status = 0;
				put_unaligned_le16(256, &bs->le_mtu); /* why not ? */
				bs->le_max_pkt = 16; /* why not */
				end = (uint8_t*)(bs + 1);
				break;

			case HCI_LE_Set_Scan_Parameters:
			case HCI_LE_Set_Advertising_Data:
			case HCI_LE_Set_Scan_Enable:
			case HCI_LE_Set_Event_Mask:
			case HCI_LE_Set_Random_Address:
			case HCI_LE_Set_Scan_Response_Data:
			case HCI_LE_Clear_White_List:
			case HCI_LE_Add_Device_To_White_List:
			case HCI_LE_Remove_Device_From_White_List:
			case HCI_LE_Set_Host_Channel_Classification:
				*stat = 0;
				end = stat + 1;
				break;

			case HCI_LE_Read_Advertising_Channel_Tx_Power:
				*stat++ = 0;  /* status = 0 */
				*stat = 0; /* good tx power */
				end = stat + 1;
				break;

			case HCI_LE_Read_White_List_Size:
				*stat++ = 0;  /* status = 0 */
				*stat = 1; /* small list */
				end = stat + 1;
				break;

			case HCI_LE_Read_Local_Supported_Features:
			case HCI_LE_Read_Supported_States:
				*stat++ = 0; /* status = 0 */
				for (i = 0; i < 8; i++)
					*stat++ = 0; /* nothing supported */
				end = stat;
				break;

			case HCI_LE_Encrypt:
			case HCI_LE_Rand:
				/* we allow this - it is safe */
				break;

			default:
				/* this is unsafe, but what can we do?*/
				aahlog(" -> command unknown - BAD!\n");
				break;
			}

			if (end) {

				evt->plen = end - (uint8_t*)(evt + 1);

				log_it = 0; /* log now so we can log "reply" */

				aahbt_logpacket(0, HCI_COMMAND_PKT,
						(const uint8_t*)skb->data,
						skb->len, 0);
				aahbt_logpacket(1, HCI_EVENT_PKT,
						buf, end - buf, 0);
				aahbt_pkt_to_user(drv_priv,
						  HCI_EVENT_PKT,
						  buf, end - buf);

				ret = AAH_BT_PKT_DROP;
			}
		}
	}

	if (ret == AAH_BT_PKT_PROCEED && !skipsem &&
			(bt_cb(skb)->pkt_type == HCI_COMMAND_PKT ||
				bt_cb(skb)->pkt_type == PKT_MARVELL)) {
		BUG_ON(!cmd_credit);
		--cmd_credit;
		if (LOG_BT_CREDIT)
			aahlog("cmd credit dec; credits now %d\n", cmd_credit);
	}

	if (log_it)
		aahbt_logpacket(0,
				bt_cb(skb)->pkt_type,
				(const uint8_t*)skb->data,
				skb->len,
				wasours);

	return ret;
}

int aahbt_ok_to_send(void)
{
	return cmd_credit > 0;
}

void aahbt_splitter_init(void)
{
	reset_tracked_le_conns();
}

bool aahbt_splitter_may_tx_data(void)
{
	bool ret;
	spin_lock(&conn_tracking_lock);
	ret = (aahbt_cur_tx_in_flight < aahbt_max_tx_in_flight);
	spin_unlock(&conn_tracking_lock);
	return ret;
}

void aahbt_splitter_send_data(uint16_t conn_id, struct sk_buff *skb)
{
	struct aahbt_le_conn *c;
	BUG_ON(!skb);
	BUG_ON(!drv_priv);

	spin_lock(&conn_tracking_lock);

	c = cid_find_le_conn_l(conn_id);
	if (!c) {
		aahlog("WARNING: dropping ACL packet sent to unknown conn_id "
		       "%hd\n", conn_id);
		goto bailout;
	}

	BUG_ON(aahbt_cur_tx_in_flight >= aahbt_max_tx_in_flight);
	aahbt_cur_tx_in_flight++;
	c->tx_in_flight++;

	/* TODO: optimize this; we should be able to send the SKB directly
	 * without needing to clone it.
	 */
	aahbt_pkt_to_chip(drv_priv, HCI_ACLDATA_PKT, skb->data, skb->len);

bailout:
	spin_unlock(&conn_tracking_lock);
	kfree_skb(skb);
}
