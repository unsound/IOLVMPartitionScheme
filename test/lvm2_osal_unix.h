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

#if !defined(_LVM2_OSAL_UNIX_H)
#define _LVM2_OSAL_UNIX_H

#include <stdio.h>
#include <stddef.h>

#define FMThhd "hhd"
#define FMThhu "hhu"
#define FMThhx "hhx"
#define FMThhX "hhX"

#define FMThd  "hd"
#define FMThu  "hu"
#define FMThx  "hx"
#define FMThX  "hX"

#define FMTd   "d"
#define FMTu   "u"
#define FMTx   "x"
#define FMTX   "X"

#define FMTld  "ld"
#define FMTlu  "lu"
#define FMTlx  "lx"
#define FMTlX  "lX"

#define FMTlld "lld"
#define FMTllu "llu"
#define FMTllx "llx"
#define FMTllX "llX"

#define FMTzu  "zd"
#define FMTzd  "zu"
#define FMTzx  "zx"
#define FMTzX  "zX"

#define ARGhhd(value)   (value)
#define ARGhhu(value)   (value)
#define ARGhhx(value)   (value)
#define ARGhhX(value)   (value)

#define ARGhd(value)    (value)
#define ARGhu(value)    (value)
#define ARGhx(value)    (value)
#define ARGhX(value)    (value)

#define ARGd(value)     (value)
#define ARGu(value)     (value)
#define ARGx(value)     (value)
#define ARGX(value)     (value)

#define ARGld(value)    (long) (value)
#define ARGlu(value)    (unsigned long) (value)
#define ARGlx(value)    (unsigned long) (value)
#define ARGlX(value)    (unsigned long) (value)

#define ARGlld(value)   (long long) (value)
#define ARGllu(value)   (unsigned long long) (value)
#define ARGllx(value)   (unsigned long long) (value)
#define ARGllX(value)   (unsigned long long) (value)

#define ARGzd(value)    (ssize_t) (value)
#define ARGzu(value)    (size_t) (value)
#define ARGzx(value)    (size_t) (value)
#define ARGzX(value)    (size_t) (value)

/* Logging macros. */
#if !defined(LOG_CLASSNAME)
#define LOG_CLASSNAME ""
#endif /* !defined(LOG_CLASSNAME) */

#define LogError(...) \
	do { \
		if(LOG_CLASSNAME[0] == '\0') \
			fprintf(stderr, "[%s(...)] ", __FUNCTION__); \
		else \
			fprintf(stderr, "[%s::%s(...)] ", LOG_CLASSNAME, \
				__FUNCTION__); \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} while(0)

#if defined(DEBUG)
#define LogDebug(...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} while(0)
#else
#define LogDebug(...) do {} while(0)
#endif /* defined(DEBUG) */

#endif /* !defined(_LVM2_OSAL_UNIX_H) */
