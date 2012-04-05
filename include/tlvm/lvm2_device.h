/*-
 * Copyright (C) 2012 Erik Larsson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#if !defined(_LVM2_DEVICE_H)
#define _LVM2_DEVICE_H

#include "lvm2_types.h"

struct lvm2_io_buffer;

int lvm2_io_buffer_create(size_t size, struct lvm2_io_buffer **out_buf);

const void* lvm2_io_buffer_get_bytes(struct lvm2_io_buffer *buf);

void lvm2_io_buffer_destroy(struct lvm2_io_buffer **buf);

struct lvm2_device;

int lvm2_device_read(struct lvm2_device *dev, u64 pos, size_t count,
		struct lvm2_io_buffer *buf);

#endif /* !defined(_LVM2_DEVICE_H) */
