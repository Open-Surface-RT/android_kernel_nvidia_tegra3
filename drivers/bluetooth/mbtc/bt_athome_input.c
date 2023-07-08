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
#include <linux/input/mt.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include "bt_athome_gestures.h"
#include "bt_athome_input.h"
#include "bt_athome_logging.h"
#include "bt_athome_proto.h"

#if HACK_DEBUG_USING_LED

int aah_io_led_hack( uint rgb_color );
int aah_io_led_hack_enable( bool enable );

#define HACK_LED_BLUE     0x0000FF
#define HACK_LED_GREEN    0x00FF00
#define HACK_LED_RED      0xFF0000
#define HACK_LED_ORANGE   0xFF8000
#define HACK_LED_YELLOW   0x808000
#define HACK_LED_WHITE    0xFFFFFF

/* we either show every event or just disconnect state */
#define SHOW_DISCONNECT_ONLY 1

#if (SHOW_DISCONNECT_ONLY == 0)
/* Map event types to colors. */
static int event_colors[HACK_LED_EVENT_COUNT] = {
	[HACK_LED_EVENT_CONNECT] = HACK_LED_YELLOW,
	[HACK_LED_EVENT_AWAKE] = HACK_LED_YELLOW,
	[HACK_LED_EVENT_TOUCH_DOWN] = HACK_LED_GREEN,
	[HACK_LED_EVENT_BUTTON_DOWN] = HACK_LED_RED,
	[HACK_LED_EVENT_INPUT_UP] = HACK_LED_ORANGE,
	[HACK_LED_EVENT_ASLEEP] = HACK_LED_BLUE,
	[HACK_LED_EVENT_DISCONNECT] = HACK_LED_WHITE,
	};

void aahbt_input_led_show_event(int event_type)
{
	BUG_ON(event_type >= ARRAY_SIZE(event_colors));
	aah_io_led_hack(event_colors[event_type]);
}
#else /* SHOW_DISCONNECT_ONLY */
void aahbt_input_led_show_event(int event_type)
{
	if (event_type == HACK_LED_EVENT_DISCONNECT) {
		aah_io_led_hack_enable(true);
	} else if (event_type == HACK_LED_EVENT_CONNECT) {
		aah_io_led_hack_enable(false);
	}
}
#endif /* SHOW_DISCONNECT_ONLY */

#endif /* HACK_DEBUG_USING_LED */

static void aahbt_input_apply_ab_filter(unsigned which,
					uint16_t* x,
					uint16_t* y);
static void aahbt_input_apply_distance_filter(unsigned which,
					      uint16_t* x,
					      uint16_t* y);

struct athome_input_state {
	/* Values which stay as they are for as long as the module is loaded.
	 * If any variables are added to this set, they must be restored
	 * properly in athome_bt_input_reset_state.
	 */
	struct input_dev *idev;
	char uniq[16];

	/* Values are reset any time the stack gets reset.  If a reset value
	 * other than 0 is needed, it must be handled explicitly in
	 * athome_bt_input_reset_state.
	 */
	uint8_t fingers_down;
	int32_t vx, vy;
	int32_t px, py;
	int32_t last_x, last_y;
	struct timespec last_evt;
	struct timespec last_touch_evt;
	bool last_evt_time_valid;
	bool last_touch_evt_time_valid;
	uint32_t touch_count; /* cleared on finger up */
};
static struct athome_input_state inputs[ATHOME_RMT_MAX_CONNS];

static struct {
	u32 enabled;
	u16 alpha;
	u16 beta;
	u16 min_square_distance;
} filter_params = {
	.enabled = 1,
	.alpha = 50,
	.beta = 0,
	.min_square_distance = 9 // in 100s of micrometers squared
};

static struct dentry *debugfs_dir;
static u32 send_dpad_events = true;

/* to see events live: in adb do "getevent -lt /dev/input/event1" */

static unsigned short ikeys[] = {
	KEY_BACK,
	KEY_HOMEPAGE,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
	AAH_BT_KEY_DPAD_CENTER,
	AAH_BT_KEY_POWER,
	KEY_SEARCH,
	/* dpad starts at 7 */
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
};

static int aahbt_input_init_device(struct input_dev **idevP)
{
	int err, i;
	struct input_dev *idev;

	BUG_ON(!idevP);
	*idevP = NULL;

	idev = input_allocate_device();
	if (!idev) {
		aahlog("Failed to allocate touch device\n");
		return -ENOMEM;
	}

	idev->name = "athome_remote";

	/* we have a select button under the touch pad */
	set_bit(INPUT_PROP_BUTTONPAD, idev->propbit);

	/* we support touch, buttons, misc */
	set_bit(EV_SYN, idev->evbit);
	set_bit(EV_ABS, idev->evbit);
	set_bit(EV_MSC, idev->evbit);

	/* we support the following buttons */
	set_bit(EV_KEY, idev->evbit);
	for(i = 0; i < ARRAY_SIZE(ikeys); i++)
		__set_bit(ikeys[i], idev->keybit);

	/* key mapping */
	idev->keycode = ikeys;
	idev->keycodesize = sizeof(unsigned short);
	idev->keycodemax = ARRAY_SIZE(ikeys);

	/* touch params */
	input_mt_init_slots(idev, ATHOME_MAX_FINGERS);
	input_set_abs_params(idev, ABS_MT_POSITION_X, 0, AAH_RAW_X_MAX, 0, 0);
	input_set_abs_params(idev, ABS_MT_POSITION_Y, 0, AAH_RAW_Y_MAX, 0, 0);
	input_abs_set_res(idev, ABS_MT_POSITION_X,
				AAH_RAW_X_MAX / AAH_BT_TOUCHPAD_WIDTH);
	input_abs_set_res(idev, ABS_MT_POSITION_Y,
				AAH_RAW_Y_MAX / AAH_BT_TOUCHPAD_HEIGHT);

	/* without misc scan capability, volume keys will do nothing */
	input_set_capability(idev, EV_MSC, MSC_SCAN);

	/* hacky way to give android timestamps for the events */
	input_set_capability(idev, EV_MSC, MSC_ANDROID_TIME_SEC);
	input_set_capability(idev, EV_MSC, MSC_ANDROID_TIME_USEC);

	err = input_register_device(idev);
	if (err) {
		aahlog("Failed to register input device\n");
		input_free_device(idev);
		return err;
	}

	*idevP = idev;

#if HACK_DEBUG_USING_LED
#if SHOW_DISCONNECT_ONLY
	aah_io_led_hack(HACK_LED_WHITE);
	aah_io_led_hack_enable(false);
#else
	aah_io_led_hack_enable(true);
#endif
#endif
	return 0;
}

static void aahbt_input_del_device(struct input_dev *idev)
{
	input_mt_destroy_slots(idev);
	input_unregister_device(idev);
	input_free_device(idev);
}

static int aahbt_input_init_debug(void)
{
	struct dentry *entry;
	debugfs_dir = debugfs_create_dir("aahbt_input", NULL);
	if (!debugfs_dir)
		return -ENODEV;

	entry = debugfs_create_u16("alpha", 0600, debugfs_dir,
			&filter_params.alpha);
	if (!entry) {
		aahlog("Failed to create debugfs file for alpha attr.\n");
		return -ENODEV;
	}
	entry = debugfs_create_u16("beta", 0600, debugfs_dir,
			&filter_params.beta);
	if (!entry) {
		aahlog("Failed to create debugfs file for beta attr.\n");
		return -ENODEV;
	}
	entry = debugfs_create_u16("min_square_distance", 0600, debugfs_dir,
			&filter_params.min_square_distance);
	if (!entry) {
		aahlog("Failed to create debugfs file for distance filter "
				"min_square_distance attr.\n");
		return -ENODEV;
	}
	entry = debugfs_create_bool("enabled", 0600, debugfs_dir,
			&filter_params.enabled);
	if (!entry) {
		aahlog("Failed to create debugfs file for alpha-beta filter "
				"enabled attr.\n");
		return -ENODEV;
	}
	entry = debugfs_create_bool("enable_dpad", 0600, debugfs_dir,
			&send_dpad_events);
	if (!entry) {
		aahlog("Failed to create dpad preferences attr.\n");
		return -ENODEV;
	}

	return aahbt_input_dpad_init(debugfs_dir);
}

static void aahbt_input_deinit_debug(void)
{
	debugfs_remove_recursive(debugfs_dir);
}

int aahbt_input_init(void)
{
	int err;
	size_t i;

	if (aahbt_input_init_debug())
		aahlog("Failed to create debugfs entries\n");

	aahlog("touch filter, alpha = %d, beta = %d, enabled = %d\n",
		filter_params.alpha, filter_params.beta, filter_params.enabled);

	aahbt_input_reset_state();

	for (i = 0; i < ATHOME_RMT_MAX_CONNS; i++) {
		scnprintf(inputs[i].uniq, sizeof(inputs[i].uniq),
						"athome_bt_%d", i);

		err = aahbt_input_init_device(&inputs[i].idev);
		if (err)
			break;
		inputs[i].idev->uniq = inputs[i].uniq;
	}
	if (i == ATHOME_RMT_MAX_CONNS)
		return 0;
	/* we failed */
	while (i) {
		i--;
		aahbt_input_del_device(inputs[i].idev);
		inputs[i].idev = NULL;
	}
	return err;
}

void aahbt_input_deinit(void)
{
	int i;

	aahbt_input_deinit_debug();

	for(i = 0; i < ATHOME_RMT_MAX_CONNS; i++)
		aahbt_input_del_device(inputs[i].idev);
}

void aahbt_input_reset_state(void)
{
	size_t i;
	for (i = 0; i < ARRAY_SIZE(inputs); i++) {
		struct athome_input_state backup;
		struct athome_input_state *I = inputs + i;

		/* back up the no-reset variables */
		memcpy(&backup, I, sizeof(backup));

		/* zero out the yes-reset variables */
		memset(I, 0, sizeof(*I));

		/* restore the no-reset variables */
		I->idev = backup.idev;
		memcpy(backup.uniq, I->uniq, sizeof(backup.uniq));
	}

	aahbt_input_led_show_event(HACK_LED_EVENT_DISCONNECT);
}

/* Print touch event log less often as the count goes up. */
static bool aahbt_input_should_report_touch(uint32_t count)
{
	if (count < (0x3F00))
		return ((count & 0x3F) == 0);
	else if (count < (0x3FF00))
		return ((count & 0x3FF) == 0);
	else
		return ((count & 0x3FFF) == 0);
}

void aahbt_input_send_touch(unsigned which,
			    int pointer_idx,
			    uint16_t x,
			    uint16_t y,
			    bool is_down)
{
	struct input_dev *idev;
	uint32_t mask = 1UL << pointer_idx;
	bool wasdown;

	BUG_ON(which >= ARRAY_SIZE(inputs));

	idev = inputs[which].idev;
	wasdown = !!(inputs[which].fingers_down & mask);

	if (is_down && !wasdown) {
		BUG_ON(!inputs[which].last_evt_time_valid);
		aahbt_input_led_show_event(HACK_LED_EVENT_TOUCH_DOWN);
		inputs[which].last_touch_evt = inputs[which].last_evt;
		inputs[which].last_touch_evt_time_valid = true;
		inputs[which].vx = 0;
		inputs[which].vy = 0;
		inputs[which].px = x;
		inputs[which].py = y;
		inputs[which].last_x = x;
		inputs[which].last_y = y;
	} else if (!is_down && wasdown)
		aahbt_input_led_show_event(HACK_LED_EVENT_INPUT_UP);

	if (!is_down && !wasdown)
		return;

	/* if we're configured to send dpad events then send them and then
	 * skip the rest of the touch processing. The bookkeeping below
	 * will only confuse the input system otherwise */
	if (!send_dpad_events) {
		input_mt_slot(idev, pointer_idx);
		input_mt_report_slot_state(idev, MT_TOOL_FINGER, is_down);
	}
	inputs[which].touch_count++;

	if (is_down) {

		if (!send_dpad_events) {
			aahbt_input_apply_ab_filter(which, &x, &y);
			aahbt_input_apply_distance_filter(which, &x, &y);
			input_report_abs(idev, ABS_MT_POSITION_X, x);
			input_report_abs(idev, ABS_MT_POSITION_Y, y);
		} else {
			aahbt_input_handle_dpad_down(idev, which, is_down, wasdown, x, y);
		}

		if (LOG_INPUT_EVENTS) {
			if (aahbt_input_should_report_touch(inputs[which].touch_count))
				aahlog("[%d] finger down, %4u touch events, x = %5d, y = "
						"%5d\n", pointer_idx, inputs[which].touch_count, x, y);
		}

		inputs[which].fingers_down |= mask;

		if (LOG_INPUT_SPEW)
			aahlog("[%d] raw: (%5d , %5d),"
					" predicted: (%5d , %5d),"
					" delta (%d, %d)\n",
					pointer_idx, x, y,
					inputs[which].px, inputs[which].py,
					x - inputs[which].px,
					y - inputs[which].py);
	} else { /* was down */
		if (send_dpad_events)
			aahbt_input_handle_dpad_up(idev, which);

		inputs[which].fingers_down &= ~mask;

		if (LOG_INPUT_EVENTS)
			aahlog("[%d] finger release after %4u touch events\n",
				pointer_idx, inputs[which].touch_count);
		inputs[which].touch_count = 0;
	}
}

static void aahbt_input_apply_ab_filter(unsigned which,
					uint16_t* x,
					uint16_t* y) {
	struct timespec delta_timespec;
	s32 dt;
	s32 rx, ry;

	BUG_ON(which >= ARRAY_SIZE(inputs));

	if (!filter_params.enabled)
		return;

	if (!inputs[which].last_touch_evt_time_valid)
		return;

	delta_timespec = timespec_sub(inputs[which].last_evt,
			inputs[which].last_touch_evt);
	dt = delta_timespec.tv_sec * MSEC_PER_SEC;
	dt += delta_timespec.tv_nsec / NSEC_PER_MSEC;

	inputs[which].px = inputs[which].px + (inputs[which].vx * dt);
	inputs[which].py = inputs[which].py + (inputs[which].vy * dt);

	rx = *x - inputs[which].px;
	ry = *y - inputs[which].py;

	inputs[which].px += ((s32)filter_params.alpha * rx) / 100;
	inputs[which].py += ((s32)filter_params.alpha * ry) / 100;
	if (dt > 0) {
		inputs[which].vx += (s32)filter_params.beta * rx / (100 * dt);
		inputs[which].vy += (s32)filter_params.beta * ry / (100 * dt);
	}

	inputs[which].px = clamp(inputs[which].px, 0, AAH_RAW_X_MAX);
	inputs[which].py = clamp(inputs[which].py, 0, AAH_RAW_Y_MAX);
	inputs[which].last_touch_evt = inputs[which].last_evt;
	*x = inputs[which].px;
	*y = inputs[which].py;
}

static void aahbt_input_apply_distance_filter(unsigned which,
					      uint16_t* x,
					      uint16_t* y) {
	uint32_t delta_x, delta_y, dist;
	if (!filter_params.enabled) {
		return;
	}
	delta_x = (*x - inputs[which].last_x) * 10 *
			AAH_BT_TOUCHPAD_WIDTH / AAH_RAW_X_MAX;
	delta_y = (*y - inputs[which].last_y) * 10 *
			AAH_BT_TOUCHPAD_HEIGHT / AAH_RAW_Y_MAX;
	dist = delta_x * delta_x + delta_y * delta_y;

	if (dist < filter_params.min_square_distance) {
		*x = inputs[which].last_x;
		*y = inputs[which].last_y;
	} else {
		inputs[which].last_x = *x;
		inputs[which].last_y = *y;
	}
}

void aahbt_input_send_buttons(unsigned which, uint32_t mask)
{
	struct input_dev *idev;
	int i;

	BUG_ON(which >= ARRAY_SIZE(inputs));

	if (LOG_INPUT_EVENTS)
		aahlog("[%d] button mask = 0x%08X\n", which, mask);

	aahbt_input_led_show_event((mask != 0)
		? HACK_LED_EVENT_BUTTON_DOWN
		: HACK_LED_EVENT_INPUT_UP);

	idev = inputs[which].idev;

	for (i = 0; i < ARRAY_SIZE(ikeys); i++) {
		input_event(idev, EV_MSC, MSC_SCAN, i);
		input_report_key(idev, ikeys[i], mask & 1);
		mask >>= 1;
	}
}

void aahbt_input_send_button(unsigned which, uint8_t id, uint8_t state)
{
	struct input_dev *idev;
	bool down = (state >= AAH_KEY_DOWN);

	BUG_ON(which >= ARRAY_SIZE(inputs));
	BUG_ON(state > 2);

	if (LOG_INPUT_EVENTS)
		aahlog("[%d] button %d %s\n", which, id, (down ? "down" : "up"));

	aahbt_input_led_show_event(down
		? HACK_LED_EVENT_BUTTON_DOWN : HACK_LED_EVENT_INPUT_UP);

	if (id < ARRAY_SIZE(ikeys)) {
		idev = inputs[which].idev;
		input_event(idev, EV_MSC, MSC_SCAN, id);
		input_report_key(idev, ikeys[id], state);
	}
}

void aahbt_input_calculate_time(unsigned which, long usec_since_last)
{
	BUG_ON(which >= ARRAY_SIZE(inputs));

	if ((usec_since_last == AAH_BT_UNKNOWN_TS_DELTA) ||
	    !inputs[which].last_evt_time_valid) {
		ktime_get_ts(&inputs[which].last_evt);
		inputs[which].last_evt_time_valid = true;
	} else {
		timespec_add_ns(&inputs[which].last_evt,
				(uint64_t)usec_since_last * NSEC_PER_USEC);
	}
}

void aahbt_input_frame(unsigned which)
{
	BUG_ON(which >= ARRAY_SIZE(inputs));
	BUG_ON(!inputs[which].last_evt_time_valid);

	input_event(inputs[which].idev, EV_MSC, MSC_ANDROID_TIME_SEC,
					inputs[which].last_evt.tv_sec);
	input_event(inputs[which].idev, EV_MSC, MSC_ANDROID_TIME_USEC,
			inputs[which].last_evt.tv_nsec / NSEC_PER_USEC);
	input_sync(inputs[which].idev);
}

bool aahbt_input_dpad_enabled(void)
{
	return send_dpad_events;
}
