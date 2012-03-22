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

#if !defined(_LVM2_OSAL_IOKIT_H)
#define _LVM2_OSAL_IOKIT_H

#include <IOKit/IOLib.h>

#define FMThhd "d"
#define FMThhu "u"
#define FMThhx "x"
#define FMThhX "X"

#define FMThd  "d"
#define FMThu  "u"
#define FMThx  "x"
#define FMThX  "X"

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

#define FMTzu  "lld"
#define FMTzd  "llu"
#define FMTzx  "llx"
#define FMTzX  "llX"

#define ARGhhd(value)   (int) (value)
#define ARGhhu(value)   (unsigned int) (value)
#define ARGhhx(value)   (unsigned int) (value)
#define ARGhhX(value)   (unsigned int) (value)

#define ARGhd(value)    (int) (value)
#define ARGhu(value)    (unsigned int) (value)
#define ARGhx(value)    (unsigned int) (value)
#define ARGhX(value)    (unsigned int) (value)

#define ARGd(value)     (int) (value)
#define ARGu(value)     (unsigned int) (value)
#define ARGx(value)     (unsigned int) (value)
#define ARGX(value)     (unsigned int) (value)

#define ARGld(value)    (long) (value)
#define ARGlu(value)    (unsigned long) (value)
#define ARGlx(value)    (unsigned long) (value)
#define ARGlX(value)    (unsigned long) (value)

#define ARGlld(value)   (long long) (value)
#define ARGllu(value)   (unsigned long long) (value)
#define ARGllx(value)   (unsigned long long) (value)
#define ARGllX(value)   (unsigned long long) (value)

#define ARGzd(value)    (long long) (value)
#define ARGzu(value)    (unsigned long long) (value)
#define ARGzx(value)    (unsigned long long) (value)
#define ARGzX(value)    (unsigned long long) (value)

/* Logging macros. */
#if !defined(LOG_CLASSNAME)
#define LOG_CLASSNAME ""
#endif /* !defined(LOG_CLASSNAME) */

#define LogError(...) \
	do { \
		if(LOG_CLASSNAME[0] == '\0') \
			IOLog("[%s(...)] ", __FUNCTION__); \
		else \
			IOLog("[%s::%s(...)] ", LOG_CLASSNAME, __FUNCTION__); \
		IOLog(__VA_ARGS__); \
		IOLog("\n"); \
	} while(0)

#if defined(DEBUG)
#define LogDebug(...) \
	do { \
		IOLog(__VA_ARGS__); \
		IOLog("\n"); \
	} while(0)
#else
#define LogDebug(...) do {} while(0)
#endif /* defined(DEBUG) */

#endif /* !defined(_LVM2_OSAL_IOKIT_H) */
