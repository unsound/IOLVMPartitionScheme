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

#include <libkern/libkern.h>
#include <mach/kmod.h>
#include <IOKit/IOLib.h>

kern_return_t IOLVMPartitionScheme_start(kmod_info_t *ki, void *d);
kern_return_t IOLVMPartitionScheme_stop(kmod_info_t *ki, void *d);

static const char* get_arch_string()
{
#if defined(__i386__)
	return "i386";
#elif defined(__x86_64__)
	return "x86_64";
#elif defined(__ppc__) && !defined(__LP64__)
	return "ppc";
#else
#error "Unknown architecture."
	return "<unknown>";
#endif
}

kern_return_t IOLVMPartitionScheme_start(
		kmod_info_t *const ki __attribute__((unused)),
		void *const d __attribute__((unused)))
{
	IOLog("IOLVMPartitionScheme (%s) loading...\n", get_arch_string());

	return KERN_SUCCESS;
}

kern_return_t IOLVMPartitionScheme_stop(
		kmod_info_t *const ki __attribute__((unused)),
		void *const d __attribute__((unused)))
{
	IOLog("IOLVMPartitionScheme (%s) unloading...\n", get_arch_string());

	return KERN_SUCCESS;
}
