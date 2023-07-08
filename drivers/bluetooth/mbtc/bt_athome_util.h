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

#ifndef _BT_ATHOME_UTIL_H_
#define _BT_ATHOME_UTIL_H_

#include <asm/unaligned.h>
#include <linux/kernel.h>

#define ATHOME_RMT_MAX_CONNS 2
#define INVALID_CONN_ID	     0xFFFF
#define INVALID_OCF_OGF	     0xFFFF

/* access to packed non-typedefed structures with autoincrement */
static inline uint8_t get8LE(uint8_t **parPP)
{
	uint8_t ret = **parPP;
	(*parPP)++;

	return ret;
}

static inline uint16_t get16LE(uint8_t **parPP)
{
	uint16_t ret = get_unaligned_le16(*parPP);
	(*parPP) += 2;

	return ret;
}

static inline void put8LE(uint8_t **parPP, uint8_t v)
{
	*(*parPP)++ = v;
}

static inline void put16LE(uint8_t **parPP, uint16_t v)
{
	put_unaligned_le16(v, *parPP);
	(*parPP) += 2;
}

static inline void put64LE(uint8_t **parPP, uint64_t v)
{
	put_unaligned_le64(v, *parPP);
	(*parPP) += 8;
}

#endif

