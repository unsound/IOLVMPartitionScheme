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

#if !defined(_LIBTLVM_LVM2_TYPES_H)
#define _LIBTLVM_LVM2_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#if (defined(__DARWIN__) || defined (__APPLE__)) && defined(KERNEL)

#include <sys/types.h>
#include <libkern/OSTypes.h>

typedef SInt8  s8;
typedef SInt16 s16;
typedef SInt32 s32;
typedef SInt64 s64;

typedef UInt8  u8;
typedef UInt16 u16;
typedef UInt32 u32;
typedef UInt64 u64;

#define LVM2_EXPORT __private_extern__

#else

#include <stddef.h> /* For making size_t available. */
#include <stdint.h>

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define LVM2_EXPORT

#endif /*(defined(__DARWIN__) || defined (__APPLE__)) && defined(KERNEL) */

typedef s16 sle16;
typedef s32 sle32;
typedef s64 sle64;
typedef u16 le16;
typedef u32 le32;
typedef u64 le64;

typedef s16 sbe16;
typedef s32 sbe32;
typedef s64 sbe64;
typedef u16 be16;
typedef u32 be32;
typedef u64 be64;

#define S8_MAX  0x7F
#define S16_MAX 0x7FFF
#define S32_MAX 0x7FFFFFFFL
#define S64_MAX 0x7FFFFFFFLL

#define S8_MIN  (-S8_MAX-1)
#define S16_MIN (-S16_MAX-1)
#define S32_MIN (-S32_MAX-1)
#define S64_MIN (-S64_MAX-1)

#define U8_MAX  0xFFU
#define U16_MAX 0xFFFFU
#define U32_MAX 0xFFFFFFFFUL
#define U64_MAX 0xFFFFFFFFFFFFFFFFULL

typedef int lvm2_bool;

enum {
	LVM2_FALSE = 0,
	LVM2_TRUE = 1,
};

#define lvm2_min(a, b) ((a) < (b) ? (a) : (b))
#define lvm2_max(a, b) ((a) > (b) ? (a) : (b))

#ifdef __cplusplus
};
#endif

#endif /* !defined(_LIBTLVM_LVM2_TYPES_H) */
