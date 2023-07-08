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

#include <linux/miscdevice.h>
#include <linux/semaphore.h>
#include <linux/atomic.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>
#include "bt_athome_user.h"
#include "bt_athome_logging.h"
#include "bt_athome_le_stack.h"

#define MAX_USERSPACE_MSGS		128

/* api_lock is used to serialize API access so that there can only ever be one
 * API call in flight at once.
 */
static DEFINE_MUTEX(api_lock);

/* user message queue state */
static DEFINE_MUTEX(usr_msg_q_lock);
static DECLARE_WAIT_QUEUE_HEAD(usr_msg_wait_q);
static DECLARE_WAIT_QUEUE_HEAD(tx_has_room_wait_q);
static struct sk_buff_head usr_msg_q;
static size_t usr_msg_q_len     = 0;
static bool   usr_connected     = false;
static bool   usr_module_inited = false;
static bool   usr_stack_rdy     = false;
static int    usr_stack_rdy_seq = 0;

static void aahbt_usr_purge_queues_l(void)
{
	struct sk_buff *skb;

	while (!skb_queue_empty(&usr_msg_q)) {
		skb = skb_dequeue(&usr_msg_q);
		if (skb)
			kfree_skb(skb);
	}
	usr_msg_q_len = 0;
}

/* aahbt_usr_enqueue is called from the main stack thread in order to push
 * data to the user.  It is critically important that it never attempt to
 * acquire the api_lock.
 */
static void aahbt_usr_enqueue_l(uint8_t typ,
				const void *data,
				unsigned len,
				const bdaddr_t* macP)
{
	struct sk_buff *skb;
	size_t needed_len;
	uint8_t *tmp;

	/* Note: macP address should only ever be provided when the event type is
	 * ACL data, and when the event type is ACL data, the macP address is
	 * required.
	 */
	BUG_ON((!!macP) != (typ == BT_ATHOME_EVT_DATA));

	/* do not enqueue if file's not opened */
	if (!usr_connected)
		return;

	/* drop if queue is full */
	if (usr_msg_q_len >= MAX_USERSPACE_MSGS) {
		BUG_ON((int)usr_msg_q_len < 0);
		aahlog("dropped userspace msg (0x%02x) due to full queue\n",
			typ);
		return;
	}

	/* drop if we fail to allocate our skb */
	needed_len = len + 1;
	if (macP)
		needed_len += sizeof(*macP);
	skb = bt_skb_alloc(needed_len, GFP_ATOMIC);
	if (!skb) {
		aahlog("fail to alloc skb for %u usr bytes\n", needed_len);
		return;
	}

	usr_msg_q_len++;

	tmp = ((uint8_t*)skb->data);
	tmp[0] = typ;
	tmp++;
	if (macP) {
		memcpy(tmp, macP, sizeof(*macP));
		tmp += sizeof(*macP);
	}
	memcpy(tmp, data, len);
	skb_put(skb, needed_len);
	skb_queue_tail(&usr_msg_q, skb);
	wake_up_interruptible(&usr_msg_wait_q);
}

static void aahbt_usr_queue_ready_message_l(void) {
	struct aahbt_ready_status_t rdy;

	if (usr_stack_rdy) {
		usr_stack_rdy_seq++;
		if (!usr_stack_rdy_seq)
			usr_stack_rdy_seq = 1;
		rdy.is_ready = usr_stack_rdy_seq;
	} else {
		rdy.is_ready = 0;
	}

	aahbt_usr_enqueue_l(BT_ATHOME_EVT_RDY_STATUS,
			    &rdy, sizeof(rdy), NULL);
}

void aahbt_usr_purge_queues(void)
{
	mutex_lock(&usr_msg_q_lock);
	aahbt_usr_purge_queues_l();
	mutex_unlock(&usr_msg_q_lock);
}

void aahbt_usr_enqueue(uint8_t typ,
		       const void *data,
		       unsigned len,
		       const bdaddr_t* macP)
{
	mutex_lock(&usr_msg_q_lock);
	aahbt_usr_enqueue_l(typ, data, len, macP);
	mutex_unlock(&usr_msg_q_lock);
}

void aahbt_usr_queue_ready_message(void) {
	mutex_lock(&usr_msg_q_lock);
	aahbt_usr_queue_ready_message_l();
	mutex_unlock(&usr_msg_q_lock);
}

void aahbt_usr_signal_tx_has_room(void)
{
	wake_up_interruptible(&tx_has_room_wait_q);
}

void aahbt_usr_set_stack_ready(bool stack_is_ready)
{
	if (usr_stack_rdy == stack_is_ready)
		return;

	usr_stack_rdy = stack_is_ready;
}

void aahbt_usr_sync_with_userland(void)
{
	/* See comments in bt_athome_user.h */
	mutex_lock(&api_lock);
	mutex_unlock(&api_lock);
}

static ssize_t aahbt_read(struct file *file,
			  char __user *userbuf,
			  size_t bytes,
			  loff_t *off)
{
	ssize_t ret;
	struct sk_buff *skb;

	mutex_lock(&api_lock);
	mutex_lock(&usr_msg_q_lock);

	BUG_ON((int)usr_msg_q_len < 0);
	BUG_ON(usr_msg_q_len > MAX_USERSPACE_MSGS);

	while (!usr_msg_q_len) {
		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto bailout;
		}

		mutex_unlock(&usr_msg_q_lock);
		if (wait_event_interruptible(usr_msg_wait_q, usr_msg_q_len)) {
			ret = -EINTR;
			goto bailout_q_unlocked;
		}
		mutex_lock(&usr_msg_q_lock);
	}

	skb = skb_dequeue(&usr_msg_q);
	BUG_ON(!skb);

	if (bytes > skb->len)
		bytes = skb->len;

	if (skb->len > bytes) {
		ret = -ETOOSMALL;
	} else if (copy_to_user(userbuf, skb->data, bytes)) {
		ret = -EFAULT;
	} else {
		usr_msg_q_len--;
		kfree_skb(skb);
		ret = (ssize_t)bytes;
	}

	if (ret < 0)
		skb_queue_head(&usr_msg_q, skb);

bailout:
	mutex_unlock(&usr_msg_q_lock);
bailout_q_unlocked:
	mutex_unlock(&api_lock);
	return ret;
}

static ssize_t aahbt_write(struct file *file,
			   const char __user *userbuf,
			   size_t bytes,
			   loff_t *off)
{
	ssize_t ret;
	uint8_t kbuf[sizeof(bdaddr_t) + AAH_MAX_BTLE_PAYLOAD_SZ];

	/* Write operations are always packed with the target MAC address
	 * followed by the payload for that target.  Also, a valid ACL payload
	 * can never be more than 27 bytes long, and because of the way that we
	 * use ACL payloads, they can never be shorter than 3 bytes.  Enforce
	 * all of these various length restrictions here before we even attempt
	 * to obtain any locks.  By the time we call into the stack level,
	 * violations of the payload length restrictions will result in a kernel
	 * panic as a BUG_ON check is failed.
	 */
	if ((bytes < (sizeof(bdaddr_t) + AAH_MIN_BTLE_PAYLOAD_SZ)) ||
	    (bytes > (sizeof(bdaddr_t) + AAH_MAX_BTLE_PAYLOAD_SZ)))
		return -EINVAL;

	/* Make the data available at the kernel level.  Again, no need to grab
	 * any locks before discovering that this may have failed.
	 */
	if (copy_from_user(kbuf, (void*)userbuf, bytes))
		return -EFAULT;

	mutex_lock(&api_lock);

	while (true) {
		/* If the driver is not ready to do work, reject the write
		 * operation with an appropriate error code.
		 */
		if (!usr_stack_rdy) {
			ret = -ESHUTDOWN;
			goto bailout;
		}

		/* Did the devnode become closed since the last time we
		 * attempted to queue this data?  If so, the user code has some
		 * interesting threading going on, but we should abort this
		 * operation ASAP.
		 */
		if (!usr_connected) {
			ret = -ENODEV;
			goto bailout;
		}

		/* Attempt to queue this data to the stack level. */
		ret = aahbt_queue_acl_for_tx((const bdaddr_t *)kbuf,
					     kbuf  + sizeof(bdaddr_t),
					     bytes - sizeof(bdaddr_t));

		/* At this point, we are finished unless this device node is in
		 * blocking mode and there is no more room in the TX queue.
		 */
		if ((ret != -EAGAIN) || (file->f_flags & O_NONBLOCK))
			break;

		/* We are in blocking mode and failed to queue because there was
		 * no more room left in the TX queue.  Release the API lock and
		 * block until there may be some room to send.  If we are
		 * interrupted during this wait operation, get out without
		 * re-obtaining the lock.
		 */
		mutex_unlock(&api_lock);
		if (wait_event_interruptible(tx_has_room_wait_q,
					     aahbt_tx_pipeline_has_room()))
			return -EINTR;
		mutex_lock(&api_lock);
	}

	/* yay, we are finished.  If we succeeded, replace the 0 return code
	 * with the number of bytes we successfully "wrote" to this file.
	 */
	if (!ret)
		ret = bytes;

bailout:
	mutex_unlock(&api_lock);
	return ret;
}

#define COPY_FROM_USER() do { \
	if (copy_from_user(&req, (void*)arg, sizeof(req))) { \
		ret = -EFAULT; \
		break; \
	} \
} while (0)

#define COPY_TO_USER() do { \
	if (copy_to_user((void*)arg, &req, sizeof(req))) { \
		ret = -EFAULT; \
		break; \
	} \
} while (0)

static long aahbt_ioctl(struct file *handle,
			unsigned int cmd,
			unsigned long arg)
{
	long ret = 0;

	mutex_lock(&api_lock);

	/* If the driver is not ready to do work, reject the ioctl operation
	 * with an appropriate error code.
	 */
	if (!usr_stack_rdy) {
		ret = -ESHUTDOWN;
		goto bailout;
	}

	switch (cmd) {
	case AAHBT_IOCTL_SET_LISTEN_MODE: {
		int req;
		COPY_FROM_USER();
		aahbt_set_listening(!!req);
	} break;

	case AAHBT_IOCTL_CONNECT: {
		struct aahbt_conn_req_t req;
		COPY_FROM_USER();
		ret = aahbt_start_connecting(&req.MAC, req.timeout_msec);
	} break;

	case AAHBT_IOCTL_DISCONNECT: {
		struct aahbt_disconn_req_t req;
		COPY_FROM_USER();
		ret = aahbt_start_disconnecting(&req.MAC);
	} break;

	case AAHBT_IOCTL_GET_CONN_PARAMS: {
		struct aahbt_conn_settings_t req;
		aahbt_get_default_conn_settings(&req);
		COPY_TO_USER();
	} break;

	case AAHBT_IOCTL_SET_CONN_PARAMS: {
		struct aahbt_conn_settings_t req;
		COPY_FROM_USER();
		aahbt_set_default_conn_settings(&req);
	} break;

	case AAHBT_IOCTL_ENCRYPT_LINK: {
		struct aahbt_enc_req_t req;
		COPY_FROM_USER();
		ret = aahbt_start_encrypting(&req.MAC, req.key);
	} break;

	case AAHBT_IOCTL_GET_RANDOM:{
		struct aahbt_get_rand_req_t req;

		ret = aahbt_get_random_bytes(req.rand, sizeof(req.rand));
		if (ret)
			break;

		COPY_TO_USER();
	} break;

	case AAHBT_IOCTL_AES128_ENCRYPT: {
		struct aahbt_aes128_req_t req;
		uint8_t cipher_text[sizeof(req.data)];

		COPY_FROM_USER();

		ret = aahbt_do_aes128(req.key, req.data, cipher_text);
		if (ret)
			break;
		memcpy(req.data, cipher_text, sizeof(req.data));

		COPY_TO_USER();
	} break;

	case AAHBT_IOCTL_GET_EVT_FILTER: {
		struct aahbt_filter_flags_t req;
		COPY_FROM_USER();

		ret = aahbt_get_evt_filter_flags(&req.MAC, &req.flags);
		if (ret)
			break;

		COPY_TO_USER();
	} break;

	case AAHBT_IOCTL_SET_EVT_FILTER: {
		struct aahbt_filter_flags_t req;
		COPY_FROM_USER();
		ret = aahbt_set_evt_filter_flags(&req.MAC, req.flags);
	} break;

	case AAHBT_IOCTL_SET_PKT_LOG_ENB: {
		int req;
		COPY_FROM_USER();
		aahbt_set_pkt_log_enabled(!!req);
	} break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

bailout:
	mutex_unlock(&api_lock);
	return ret;
}

#undef COPY_FROM_USER
#undef COPY_TO_USER

static unsigned int aahbt_poll(struct file *file,
			       struct poll_table_struct *wait)
{
	int ret = 0;
	mutex_lock(&api_lock);

	/* Note: {}s used just to make lock scope a little more clear */
	mutex_lock(&usr_msg_q_lock);
	{
		BUG_ON((int)usr_msg_q_len < 0);
		BUG_ON(usr_msg_q_len > MAX_USERSPACE_MSGS);

		poll_wait(file, &usr_msg_wait_q, wait);
		if (usr_msg_q_len)
			ret |= (POLLIN | POLLRDNORM);

	}
	mutex_unlock(&usr_msg_q_lock);

	poll_wait(file, &tx_has_room_wait_q, wait);
	if (aahbt_tx_pipeline_has_room())
		ret |= (POLLOUT | POLLWRNORM);

	mutex_unlock(&api_lock);
	return ret;
}

static int aahbt_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	mutex_lock(&api_lock);
	mutex_lock(&usr_msg_q_lock);

	/* Note: it seems a little odd, but usr_connected is protected by the
	 * usr_msg_q_lock.  It allows allows the main stack thread to atomically
	 * decide whether or not to push a message to the queue without needing
	 * to obtain the API lock.
	 */
	if (usr_connected) {
		ret = -EBUSY;
	} else {
		usr_connected = true;
		aahbt_usr_purge_queues_l();
		aahbt_usr_queue_ready_message_l();
	}

	mutex_unlock(&usr_msg_q_lock);
	mutex_unlock(&api_lock);
	return ret;
}

static int aahbt_release(struct inode *inode, struct file *file)
{
	mutex_lock(&api_lock);

	/* clear the usr_connected flag (so that the stack thread will stop
	 * pushing messages to the q), and then purge the q.
	 */
	mutex_lock(&usr_msg_q_lock);
	usr_connected = false;
	aahbt_usr_purge_queues_l();
	mutex_unlock(&usr_msg_q_lock);

	/* Inform the stack so it can start the process of disconnecting all
	 * devices, and shutting down listening.
	 */
	aahbt_reset_stack();

	mutex_unlock(&api_lock);
	return 0;
}

static const struct file_operations aahbt_fops =
{
	.read           = aahbt_read,
	.write          = aahbt_write,
	.open           = aahbt_open,
	.poll           = aahbt_poll,
	.unlocked_ioctl = aahbt_ioctl,
	.compat_ioctl   = aahbt_ioctl,
	.release        = aahbt_release
};

static struct miscdevice mdev =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = "aah_btle_remote",
	.fops = &aahbt_fops
};

int aahbt_usr_init(void)
{
	int ret;
	skb_queue_head_init(&usr_msg_q);
	ret = misc_register(&mdev);
	usr_module_inited = !ret;
	return ret;
}

void aahbt_usr_deinit(void)
{
	if (usr_module_inited) {
		mutex_lock(&api_lock);
		BUG_ON (usr_connected);
		misc_deregister(&mdev);
		mutex_unlock(&api_lock);
		aahbt_usr_purge_queues();
	}
}

