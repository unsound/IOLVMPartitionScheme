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

#if !defined(_LIBTLVM_LVM2_TEXT_H)
#define _LIBTLVM_LVM2_TEXT_H

#include "lvm2_device.h"
#include "lvm2_layout.h"
#include "lvm2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct lvm2_bounded_string {
	int length;
	char content[0];
};

struct lvm2_bounded_array {
	int length;
	void *content[0];
};

typedef enum {
	 LVM2_DOM_TYPE_VALUE,
	 LVM2_DOM_TYPE_SECTION,
	 LVM2_DOM_TYPE_ARRAY,
} lvm2_dom_type;

struct lvm2_dom_obj {
	lvm2_dom_type type;
	struct lvm2_bounded_string *name;
};

struct lvm2_dom_value {
	struct lvm2_dom_obj obj_super;
	struct lvm2_bounded_string *value;
};

struct lvm2_dom_section {
	struct lvm2_dom_obj obj_super;
	struct lvm2_dom_obj **children;
	size_t children_len;
};

struct lvm2_dom_array {
	struct lvm2_dom_obj obj_super;
	struct lvm2_dom_value **elements;
	size_t elements_len;
};

struct lvm2_stripe {
	struct lvm2_bounded_string *pv_name;
	u64 extent_start;
};

struct lvm2_segment {
	u64 start_extent;
	u64 extent_count;
	struct lvm2_bounded_string *type;
	u64 stripe_count;
	size_t stripes_len;
	struct lvm2_stripe **stripes;
};

typedef enum {
	LVM2_LOGICAL_VOLUME_STATUS_NONE = 0x0,
	LVM2_LOGICAL_VOLUME_STATUS_READ = 0x1,
	LVM2_LOGICAL_VOLUME_STATUS_WRITE = 0x2,
	LVM2_LOGICAL_VOLUME_STATUS_VISIBLE = 0x3,
} lvm2_logical_volume_status;

typedef enum {
	LVM2_LOGICAL_VOLUME_FLAG_NONE = 0x0,
} lvm2_logical_volume_flags;

struct lvm2_logical_volume {
	struct lvm2_bounded_string *name;
	struct lvm2_bounded_string *id;
	lvm2_logical_volume_status status;
	lvm2_logical_volume_flags flags;
	u64 segment_count;
	size_t segments_len;
	struct lvm2_segment **segments;
};

typedef enum {
	LVM2_PHYSICAL_VOLUME_STATUS_NONE = 0x0,
	LVM2_PHYSICAL_VOLUME_STATUS_ALLOCATABLE = 0x1,
} lvm2_physical_volume_status;

typedef enum {
	LVM2_PHYSICAL_VOLUME_FLAG_NONE = 0x0,
} lvm2_physical_volume_flags;

struct lvm2_physical_volume {
	struct lvm2_bounded_string *name;
	struct lvm2_bounded_string *id;
	struct lvm2_bounded_string *device;
	lvm2_physical_volume_status status;
	lvm2_physical_volume_flags flags;
	u64 dev_size;
	u64 pe_start;
	u64 pe_count;
};

typedef enum {
	LVM2_VOLUME_GROUP_STATUS_NONE = 0x0,
	LVM2_VOLUME_GROUP_STATUS_RESIZEABLE = 0x1,
	LVM2_VOLUME_GROUP_STATUS_READ = 0x2,
	LVM2_VOLUME_GROUP_STATUS_WRITE = 0x4,
} lvm2_volume_group_status;

typedef enum {
	LVM2_VOLUME_GROUP_FLAG_NONE = 0x0,
} lvm2_volume_group_flags;

struct lvm2_volume_group {
	struct lvm2_bounded_string *id;
	u64 seqno;
	lvm2_volume_group_status status;
	lvm2_volume_group_flags flags;
	u64 extent_size;
	u64 max_lv;
	u64 max_pv;
	u64 metadata_copies;
	size_t physical_volumes_len;
	struct lvm2_physical_volume **physical_volumes;
	size_t logical_volumes_len;
	struct lvm2_logical_volume **logical_volumes;
};

struct lvm2_layout {
	struct lvm2_bounded_string *vg_name;
	struct lvm2_volume_group *vg;
	struct lvm2_bounded_string *contents;
	u64 version;
	struct lvm2_bounded_string *description;
	struct lvm2_bounded_string *creation_host;
	u64 creation_time;
};

struct parsed_lvm2_text_builder_section {
	struct lvm2_dom_obj *obj;
	struct parsed_lvm2_text_builder_section *parent;
};

struct parsed_lvm2_text_builder {
	struct lvm2_dom_section *root;

	int stack_depth;
	struct parsed_lvm2_text_builder_section *stack;
};

u32 lvm2_calc_crc(u32 initial, const void *buf, size_t size);

lvm2_bool lvm2_parse_text(const char *const text, const size_t text_len,
		struct lvm2_dom_section **const out_result);

void lvm2_dom_section_destroy(struct lvm2_dom_section **section,
		lvm2_bool recursive);

int lvm2_layout_create(const struct lvm2_dom_section *root_section,
		struct lvm2_layout **out_layout);

void lvm2_layout_destroy(struct lvm2_layout **parsed_text);

int lvm2_read_text(struct lvm2_device *dev, u64 metadata_offset,
		u64 metadata_size, const struct raw_locn *locn,
		struct lvm2_layout **out_layout);

int lvm2_parse_device(struct lvm2_device *dev,
		lvm2_bool (*volume_callback)(void *private_data,
			u64 device_size, const char *volume_name,
			u64 volume_start, u64 volume_length),
		void *private_data);

lvm2_bool lvm2_check_layout(void);

#ifdef __cplusplus
}
#endif

#endif /* !defined(_LIBTLVM_LVM2_TEXT_H) */
