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

/* <Declarations from lvm tools> */

/* Declaration from: lib/metadata/metadata-exported.h */
#define LVM_SECTOR_SIZE         512U

/* Declaration from: lib/label/label.h */
#define LVM_LABEL_SCAN_SECTORS  4U

/* Declaration from: lib/misc/crc.h */
#define LVM_INITIAL_CRC         0xf597a6cfUL

/* Declaration from: lib/format_text/layout.h */
#define LVM_LVM2_LABEL          "LVM2 001"

/* Declaration from: lib/format_text/layout.h */
#define LVM_MDA_HEADER_SIZE         512

/* Declaration from: lib/uuid/uuid.h */
#define LVM_ID_LEN              32

/* Declaration from: lib/label/label.h */
struct label_header {
	int8_t id[8];		/* LABELONE */
	uint64_t sector_xl;	/* Sector number of this label */
	uint32_t crc_xl;	/* From next field to end of sector */
	uint32_t offset_xl;	/* Offset from start of struct to contents */
	int8_t type[8];		/* LVM2 001 */
} __attribute__ ((packed));

/* Declaration from: lib/format_text/layout.h */
struct disk_locn {
	uint64_t offset;	/* Offset in bytes to start sector */
	uint64_t size;		/* Bytes */
} __attribute__ ((packed));

/* Declaration from: lib/format_text/layout.h */
struct pv_header {
	int8_t pv_uuid[LVM_ID_LEN];

	/* This size can be overridden if PV belongs to a VG */
	uint64_t device_size_xl;	/* Bytes */

	/* NULL-terminated list of data areas followed by */
	/* NULL-terminated list of metadata area headers */
	struct disk_locn disk_areas_xl[0];	/* Two lists */
} __attribute__ ((packed));

/* Declaration from: lib/format_text/layout.h */
struct raw_locn {
	uint64_t offset;	/* Offset in bytes to start sector */
	uint64_t size;		/* Bytes */
	uint32_t checksum;
	uint32_t filler;
} __attribute__ ((packed));

/* Declaration from: lib/format_text/layout.h */
struct mda_header {
	uint32_t checksum_xl;	/* Checksum of rest of mda_header */
	int8_t magic[16];	/* To aid scans for metadata */
	uint32_t version;
	uint64_t start;		/* Absolute start byte of mda_header */
	uint64_t size;		/* Size of metadata area */
	
	struct raw_locn raw_locns[0];	/* NULL-terminated list */
} __attribute__ ((packed));

/* </Declarations from lvm tools> */

#ifdef __cplusplus
}
#endif

#endif /* !defined(_TLVM_LVM2_LAYOUT_H) */
