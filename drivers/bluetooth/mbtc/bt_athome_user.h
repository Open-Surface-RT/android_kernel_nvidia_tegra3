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

#ifndef _BT_ATHOME_USER_H_
#define _BT_ATHOME_USER_H_


#include "bt_athome_proto.h"

/* Init the user-facind devnode & structures */
int aahbt_usr_init(void);

/* Deinit user-facing devnode & structures */
void aahbt_usr_deinit(void);

/* Send some data to user through the user-facing devnode. */
void aahbt_usr_enqueue(uint8_t typ,
			   const void *data,
			   unsigned len,
			   const bdaddr_t *macP);

/* Indicate that there is room to queue more data for TX */
void aahbt_usr_signal_tx_has_room(void);

/* Called from the stack core when it either starts up, or shuts down in
 * response to the Marvell driver stopping or starting the stack.
 */
void aahbt_usr_set_stack_ready(bool stack_is_ready);

/* Called from the core to purge the queues while the stack is in a safe,
 * shutdown state, or in response to the userland closing the device node.
 */
void aahbt_usr_purge_queues(void);

/* Called from various places to add a message to the user msg queue indicating
 * the current ready status of the driver.
 */
void aahbt_usr_queue_ready_message(void);

/* Called during a shutdown operation by the stack's core to make
 * certain that it is in sync with calls from userland.  The idea is
 * that during a shutdown operation, the core will call set_stack_ready
 * in order to set the flag indicating that subsequent API calls will be
 * rejected with either a status message to indicate that the stack is
 * not ready, or (in the case of read) an event indicating that the
 * stack is not ready.  At this point, no new calls may come in and the
 * stack core will abort any operations which may already be in
 * progress (possibly from a userland thread) and shutting down the work
 * thread.  Once all of this is finished, it will call
 * sync_with_userland which will obtain and then release the API lock.
 * This ensures that any user threads which may have been in the middle
 * of an operation when the shutdown started have exited with an
 * appropriate response.  After syncing, it is safe for the core to
 * start the process of purging any state (message queues, connection
 * state, etc...)
 */
void aahbt_usr_sync_with_userland(void);

#endif

