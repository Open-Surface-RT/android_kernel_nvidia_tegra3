/*
 * Copyright (C) 2014 Google, Inc.
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

#ifndef _MOAL_DLOG_H_
#define _MOAL_DLOG_H_

#define DLOG_RECORD_MAX		4096
#define DLOG_RECORD_BUFSIZE	116

struct woal_dlog_record {
	unsigned long long t;
	unsigned long nanosec_rem;
	char buf[DLOG_RECORD_BUFSIZE];
};

extern atomic_t woal_dlog_record_num;
extern struct woal_dlog_record *woal_dlog_record_data;

static inline void woal_dlog_print(const char *fmt, va_list ap)
{
	unsigned int n;
	struct woal_dlog_record *r;

	if (woal_dlog_record_data == NULL)
		return;

	n = atomic_inc_return(&woal_dlog_record_num);
	r = &woal_dlog_record_data[n % DLOG_RECORD_MAX];

	r->t = cpu_clock(smp_processor_id());
	r->nanosec_rem = do_div(r->t, 1000000000);

	vsnprintf(r->buf, DLOG_RECORD_BUFSIZE, fmt, ap);
}

#endif /* _MOAL_DLOG_H_ */
