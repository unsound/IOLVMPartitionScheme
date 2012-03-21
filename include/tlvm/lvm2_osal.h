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

#if !defined(_LIBTLVM_LVM2_OSAL_H)
#define _LIBTLVM_LVM2_OSAL_H

#if (defined(__DARWIN__) || defined (__APPLE__)) && defined(KERNEL)
#include "lvm2_osal_iokit.h"
#else
#include "lvm2_osal_unix.h"
#endif

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int lvm2_malloc(size_t size, void **out_ptr);
void lvm2_free(void **ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* !defined(_LIBTLVM_LVM2_OSAL_H) */
