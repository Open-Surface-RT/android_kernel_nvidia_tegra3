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

#ifndef _BT_ATHOME_LE_STACK_H_
#define _BT_ATHOME_LE_STACK_H_

#define AAH_MAX_BTLE_PAYLOAD_SZ	27
#define AAH_MIN_BTLE_PAYLOAD_SZ	3

#include "bt_athome_proto.h"

/* Called at driver module load and unload time */
int  aahbt_stack_init_module(void);
void aahbt_stack_exit_module(void);

/* Command status or complete filter.  Return true if this matches the command
 * result the LE portion of the stack was waiting for.
 */
bool aahbt_process_cmd_status_or_complete(uint16_t opcode,
					  const uint8_t *buf,
					  uint32_t len);

/* Called by the splitter when it receives messages from the radio we might be
 * interested in.
 */
void aahbt_enqueue_msg(const void *ptr, uint32_t len, uint8_t type);

/*
 * Prepare the LE stack to start. This setus up all the stack needs to start
 * and does everything except starting. This function may sleep/block as
 * needed. If all goes well, aahbt_stack_start() may be called later.
 */
void aahbt_stack_prepare(void);

/*
 * Actually start the stack. Possibly called from atomic context, Cannot block.
 * Will always be preceded by a call to aahbt_stack_prepare().
 */
void aahbt_stack_start(void);

/*
 * Handles unexpected chip or bluedroid resets.  Calls aahbt_stack_shutdown().
 */
void aahbt_reset(void);

/*
 * Cleanup state in prep for future restart of LE stack
 */
void aahbt_stack_shutdown(void);


/*
 * This must exist elsewhere in the kernel. Used for audio packets
 */
void athome_bt_audio_dec(int which,
			 int format,
			 const uint8_t *data,
			 unsigned len,
			 bool new_timings);

/* Called by the user interface code when the devnode is closed and the stack
 * should become quiescent.
 */
void aahbt_reset_stack(void);

/* called in order to wake up the AAH work thread if needed.  Mostly used
 * internally, but called at least once from the splitter level.
 */
void aahbt_wake_thread(void);

/* Used in a coupe of places to implement ASSERTs to make certain that some
 * actions are taking place on the AAH work thread.
 */
bool aahbt_running_on_aah_thread(void);

/* driver interface IOCTL thunks. */
void aahbt_set_listening(bool should_listen);
int  aahbt_start_connecting(const bdaddr_t *MAC, uint32_t timeout_msec);
int  aahbt_start_disconnecting(const bdaddr_t *MAC);
void aahbt_get_default_conn_settings(struct aahbt_conn_settings_t *s);
void aahbt_set_default_conn_settings(const struct aahbt_conn_settings_t *s);
int  aahbt_start_encrypting(const bdaddr_t *MAC, const uint8_t *key);
int  aahbt_get_random_bytes(uint8_t *bytes, size_t amt);
int  aahbt_do_aes128(const uint8_t* key, const uint8_t* in, uint8_t* out);
int  aahbt_get_evt_filter_flags(const bdaddr_t *MAC, uint32_t *flags);
int  aahbt_set_evt_filter_flags(const bdaddr_t *MAC, uint32_t  flags);

/* driver interface ::write thunk */
int  aahbt_queue_acl_for_tx(const bdaddr_t *MAC, const void* data, size_t len);

/* used by the driver interface ::write and ::poll implementations for TX flow
 * control.
 */
bool aahbt_tx_pipeline_has_room(void);

/* Utility function used to get a simple, hi-res, monotonic, 64-bit timestamp */
uint64_t aahbt_get_time(void);

#endif

