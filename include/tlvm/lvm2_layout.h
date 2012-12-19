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

#if !defined(_TLVM_LVM2_LAYOUT_H)
#define _TLVM_LVM2_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvm2_types.h"

/* <Declarations from lvm tools> */

/** Declaration from: lib/metadata/metadata-exported.h */
#define LVM_SECTOR_SIZE         512U

/** Declaration from: lib/label/label.h */
#define LVM_LABEL_SCAN_SECTORS  4U

/** Declaration from: lib/misc/crc.h */
#define LVM_INITIAL_CRC         0xf597a6cfU

/** Declaration from: lib/format_text/layout.h */
#define LVM_LVM2_LABEL          "LVM2 001"

/** Declaration from: lib/format_text/layout.h */
#define LVM_MDA_HEADER_SIZE     512

/** Declaration from: lib/uuid/uuid.h */
#define LVM_ID_LEN              32

/** Declaration from: lib/label/label.h */
struct label_header {
	u8 id[8];		/**< LABELONE */
	le64 sector_xl;		/**< Sector number of this label */
	le32 crc_xl;		/**< From next field to end of sector */
	le32 offset_xl;		/**< Offset from start of struct to contents */
	u8 type[8];		/**< LVM2 001 */
} __attribute__ ((packed));

/** Declaration from: lib/format_text/layout.h */
struct disk_locn {
	le64 offset;		/**< Offset in bytes to start sector */
	le64 size;		/**< Bytes */
} __attribute__ ((packed));

/** Declaration from: lib/format_text/layout.h */
struct pv_header {
	u8 pv_uuid[LVM_ID_LEN];

	/* This size can be overridden if PV belongs to a VG */
	le64 device_size_xl;	/**< Bytes */

	/* NULL-terminated list of data areas followed by */
	/* NULL-terminated list of metadata area headers */
	struct disk_locn disk_areas_xl[0];	/**< Two lists */
} __attribute__ ((packed));

/** Declaration from: lib/format_text/layout.h */
struct raw_locn {
	le64 offset;		/**< Offset in bytes to start sector */
	le64 size;		/**< Bytes */
	le32 checksum;
	le32 filler;
} __attribute__ ((packed));

/** Declaration from: lib/format_text/layout.h */
struct mda_header {
	le32 checksum_xl;	/**< Checksum of rest of mda_header */
	u8 magic[16];		/**< To aid scans for metadata */
	le32 version;
	le64 start;		/**< Absolute start byte of mda_header */
	le64 size;		/**< Size of metadata area */
	
	struct raw_locn raw_locns[0];	/**< NULL-terminated list */
} __attribute__ ((packed));

/* </Declarations from lvm tools> */

#ifdef __cplusplus
}
#endif

#endif /* !defined(_TLVM_LVM2_LAYOUT_H) */
