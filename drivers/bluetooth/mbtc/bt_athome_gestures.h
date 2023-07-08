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

#ifndef _BT_ATHOME_GESTURES_H_
#define _BT_ATHOME_GESTURES_H_
#include <linux/debugfs.h>
#include <linux/input.h>

int aahbt_input_dpad_init(struct dentry *debugfs_dir);
void aahbt_input_handle_dpad_down(struct input_dev *idev, unsigned int which,
		bool is_down, bool was_down, uint16_t x, uint16_t y);
void aahbt_input_handle_dpad_up(struct input_dev *idev, unsigned int which);

#endif
