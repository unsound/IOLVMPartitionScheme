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

#include "lvm2_osal.h"

#include <stdlib.h>
#include <errno.h>

int lvm2_malloc(size_t size, void **out_ptr)
{
	void *ptr;

	ptr = malloc(size);
	if(ptr) {
		*out_ptr = ptr;
		return 0;
	}
	else {
		return errno ? errno : ENOMEM;
	}
}

void lvm2_free(void **ptr,
		size_t size __attribute__((unused)))
{
	free(*ptr);
	*ptr = NULL;
}
