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
#include <linux/debugfs.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include "bt_athome_input.h"
#include "bt_athome_le_stack.h"
#include "bt_athome_logging.h"

static uint64_t start;
static uint8_t curr_key;
static uint16_t x_0, y_0;
static uint16_t analog_dist;
static bool is_analog;
static bool in_deadzone;
static bool sent_swipe;
struct timer_list analog_timer;
struct input_dev *gdev;

static uint32_t analog_debug = false;
static uint16_t last_tick_rate_ms;
/* The minimum width to register a swipe */
static uint16_t swipe_width = 16384;
/* The maximum width for analog gestures */
static uint16_t analog_width = 24576;
/* The radius of the center deadzone when analog motion is in play */
static uint16_t analog_deadzone_radius = 8192;
/* the longest time allowed between repeat ticks */
static uint16_t max_analog_rate_ms = 700;
/* the shortest time allowed between repeat ticks */
static uint16_t min_analog_rate_ms = 150;
/* Delay between a swipe occurring and switching to analog mode */
static uint16_t analog_switch_delay_ms = 300;

static void send_gesture_key(int code, int value)
{
	if (gdev == NULL) {
		aahlog("Err: trying to send input to a NULL device");
		return;
	}

	input_event(gdev, EV_KEY, code, value);
	input_sync(gdev);
}

static void recenter_origin(int32_t *_x_dist, int32_t *_y_dist) {
	int32_t diff;
	int32_t x_dist;
	int32_t y_dist;

	BUG_ON(!_x_dist);
	BUG_ON(!_y_dist);

	x_dist = *_x_dist;
	y_dist = *_y_dist;
	in_deadzone = false;

	/* If the distance between our gesture's center point is more than the
	 * analog width then we shift the center to facilitate changing direction
	 * after starting from a swipe somewhere off-center
	 */
	if (x_dist > analog_width) {
		diff = x_dist - analog_width;
		x_dist -= diff;
		x_0 += diff;
	} else if (x_dist < -analog_width) {
		diff = x_dist + analog_width;
		x_dist -= diff;
		x_0 += diff;
	}

	if (y_dist > analog_width) {
		diff = y_dist - analog_width;
		y_dist -= diff;
		y_0 += diff;
	} else if (y_dist < -analog_width) {
		diff = y_dist + analog_width;
		y_dist -= diff;
		y_0 += diff;
	}

	if (curr_key == KEY_UP || curr_key == KEY_DOWN) {
		analog_dist = abs(y_dist);
	} else {
		analog_dist = abs(x_dist);
	}

	/* Now that we've recentered and calculated the absolute distance we
	 * need to check to see if we're within the deadzone. The main input
	 * handling code will react based on this.
	 */
	if (analog_dist < analog_deadzone_radius)
		in_deadzone = true;

	*_x_dist = x_dist;
	*_y_dist = y_dist;
}

/* Updates the global curr_key and returns the key we need to release */
static uint8_t determine_gesture_direction(int32_t x_dist, int32_t y_dist)
{
	uint8_t next_key = 0;
	uint8_t old_key = curr_key;
	bool is_vert = false;

	if (is_analog ||
		(abs(y_dist) > swipe_width) || (abs(x_dist) > swipe_width)) {
		if (abs(y_dist) > abs(x_dist))
			is_vert = true;

		if (is_vert) {
			if (y_dist < 0)
				next_key = KEY_UP;
			else
				next_key = KEY_DOWN;
		} else {
			if (x_dist < 0)
				next_key = KEY_LEFT;
			else
				next_key = KEY_RIGHT;
		}
	}

	if (next_key) {
		bool change_key = false;

		if (!curr_key) {
			change_key = true;
		} else {
			/* We want to lock the user to the X or Y axis once they've
			 * swiped so they do not accidentally move around without meaning to
			 */
			if ((curr_key == KEY_RIGHT || curr_key == KEY_LEFT) &&
					(next_key == KEY_RIGHT || next_key == KEY_LEFT))
				change_key = true;

			if ((curr_key == KEY_UP || curr_key == KEY_DOWN) &&
					(next_key == KEY_UP || next_key == KEY_DOWN))
				change_key = true;
		}

		if (change_key) {
			curr_key = next_key;
			return old_key;
		}
	}

	return 0;
}

static uint16_t calc_tick_rate(void)
{
	uint16_t tick_delta_ms = max_analog_rate_ms - min_analog_rate_ms;
	int32_t tick_tm_ms = max_analog_rate_ms;

	/* The distance between center and the finger position is scaled to match
	 * the delta between the configured min/max analog rate values. Then we can
	 * settle on an appropriate linear value. Due to the size of the touchpad
	 * and a finger we can't do an exponential curve in a manner that feels
	 * good to a user */
	tick_tm_ms -= (tick_delta_ms *
		(((uint32_t)(analog_dist - analog_deadzone_radius)* 100) /
		(analog_width - analog_deadzone_radius))) / 100;

	if (tick_tm_ms < min_analog_rate_ms)
		tick_tm_ms = min_analog_rate_ms;
	else if (tick_tm_ms > max_analog_rate_ms)
		tick_tm_ms = max_analog_rate_ms;

	last_tick_rate_ms = (uint16_t)tick_tm_ms;
	return last_tick_rate_ms;
}

static void set_repeat_timer(uint16_t delay_ms)
{
	mod_timer(&analog_timer, jiffies + msecs_to_jiffies(delay_ms));
}

static void analog_callback(unsigned long arg)
{
	send_gesture_key(curr_key, AAH_KEY_REPEAT);
	set_repeat_timer(calc_tick_rate());
}

int aahbt_input_dpad_init(struct dentry *debugfs_dir) {
	struct dentry *entry;

	entry = debugfs_create_u16("swipe_width", 0600, debugfs_dir,
			&swipe_width);
	if (!entry) {
		aahlog("Failed to create debugfs file for swipe width.\n");
		return -ENODEV;
	}

	entry = debugfs_create_u16("analog_width", 0600, debugfs_dir,
			&analog_width);
	if (!entry) {
		aahlog("Failed to create debugfs file for analog width.\n");
		return -ENODEV;
	}

	entry = debugfs_create_u16("analog_deadzone_radius", 0600, debugfs_dir,
			&analog_deadzone_radius);
	if (!entry) {
		aahlog("Failed to create debugfs file for analog deadzone radius.\n");
		return -ENODEV;
	}

	entry = debugfs_create_u16("max_analog_rate_ms", 0600, debugfs_dir,
			&max_analog_rate_ms);
	if (!entry) {
		aahlog("Failed to create debugfs file for max analog rate.\n");
		return -ENODEV;
	}

	entry = debugfs_create_u16("min_analog_rate_ms", 0600, debugfs_dir,
			&min_analog_rate_ms);
	if (!entry) {
		aahlog("Failed to create debugfs file for min analog rate.\n");
		return -ENODEV;
	}

	entry = debugfs_create_u16("analog_switch_delay_ms", 0600, debugfs_dir,
			&analog_switch_delay_ms);
	if (!entry) {
		aahlog("Failed to create debugfs file for analog switch delay.\n");
		return -ENODEV;
	}

	entry = debugfs_create_bool("analog_debug", 0600, debugfs_dir,
			&analog_debug);
	if (!entry) {
		aahlog("Failed to create debugfs file for analog debug.\n");
		return -ENODEV;
	}

	init_timer(&analog_timer);
	setup_timer(&analog_timer, analog_callback, 0);
	return 0;
}


void aahbt_input_handle_dpad_down(struct input_dev *idev, unsigned int which,
		bool is_down, bool was_down, uint16_t x, uint16_t y)
{
	uint64_t now = aahbt_get_time();
	int32_t x_dist, y_dist;
	uint8_t old_key;

	/* Although the input system is written to handle multiple fingers, we
	 * know the Bemote trackpad can only support one. In that case, we can cache
	 * the pointer to the input struct when a finger goes down and use it for
	 * the timer events
	 */
	gdev = idev;
	/* We shouldn't be called if the finger isn't down, but handle it properly
	 * just in case
	 */
	if (!is_down)
		return;

	/* If we had no finger down before but have one now then set up initial
	 * parameters so we can watch for a swipe
	 */
	if (!was_down) {
		start = now;
		x_0 = x;
		y_0 = y;
		return;
	}

	x_dist = x - x_0;
	y_dist = y - y_0;

	if (!sent_swipe) {
		determine_gesture_direction(x_dist, y_dist);
		if (curr_key) {
			send_gesture_key(curr_key, AAH_KEY_DOWN);
			sent_swipe = true;
		}
	} else if (!is_analog &&
			now - start > (analog_switch_delay_ms * NSEC_PER_MSEC)) {
		is_analog = true;
	}

	if (is_analog) {
		/* Adjust the center point if we received updated coordinates. If the
		 * direction changed then update it here and send up/down as needed.
		 * Repeats are handled via the timer callback
		 */
		recenter_origin(&x_dist, &y_dist);
		old_key = determine_gesture_direction(x_dist, y_dist);
		if (analog_debug) {
			aahlog("dist: %u, old_key: %u, curr_key: %u, x_dist: %d, y_dist: "
					"%d, x0: %u, y0: %u, tick: %u\n", analog_dist, old_key,
					curr_key, x_dist, y_dist, x_0, y_0, last_tick_rate_ms);
		}

		if (in_deadzone && timer_pending(&analog_timer)) {
			del_timer(&analog_timer);
		} else if (old_key && old_key != curr_key) {
			send_gesture_key(old_key, AAH_KEY_UP);
			send_gesture_key(curr_key, AAH_KEY_DOWN);
			set_repeat_timer(min_analog_rate_ms);
		} else if (!in_deadzone && !timer_pending(&analog_timer)) {
			set_repeat_timer(calc_tick_rate());
		}
	}
}

/* When a finger is released we reset all the state of the gesture recognition
 * and analog interpretation
 */
void aahbt_input_handle_dpad_up(struct input_dev *idev, unsigned which)
{
	if (curr_key) {
		if (sent_swipe)
			send_gesture_key(curr_key, AAH_KEY_UP);

		curr_key = 0;
		x_0 = 0;
		y_0 = 0;
		start = 0;
		is_analog = false;
		sent_swipe = false;
		del_timer(&analog_timer);
		gdev = NULL;
	}
}
