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

#endif /*(defined(__DARWIN__) || defined (__APPLE__)) && defined(KERNEL) */

typedef int lvm2_bool;
enum {
	LVM2_FALSE = 0,
	LVM2_TRUE = 1,
};

#endif /* !defined(_LIBTLVM_LVM2_TYPES_H) */
