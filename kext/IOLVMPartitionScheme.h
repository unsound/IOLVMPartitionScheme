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

/*
 * This header contains the IOLVMPartitionScheme class definition.
 */

#ifndef _IOLVMPARTITIONSCHEME_H
#define _IOLVMPARTITIONSCHEME_H

#include <IOKit/IOTypes.h>

#include "lvm2_layout.h"

#define kIOLVMPartitionSchemeClass "IOLVMPartitionScheme"

#pragma pack(push, 1)                        /* (enable 8-bit struct packing) */

#pragma pack(pop)                        /* (reset to default struct packing) */

#ifdef KERNEL
#ifdef __cplusplus

#include <IOKit/storage/IOPartitionScheme.h>

class IOLVMPartitionScheme : public IOPartitionScheme
{
	OSDeclareDefaultStructors(IOLVMPartitionScheme);

protected:

	struct ExpansionData { /* */ };
	ExpansionData *_expansionData;

	OSSet *_partitions; /* (set of media objects representing partitions) */

	/*
	 * Free all of this object's outstanding resources.
	 */

	virtual void free(void);

	/*
	 * Scan the provider media for an LVM layout.  Returns the set of media
	 * objects representing each of the partitions (the retain for the set
	 * is passed to the caller), or null should no LVM layout be found.  The
	 * default probe score can be adjusted up or down, based on the
	 * confidence of the scan.
	 */

	virtual OSSet* scan(SInt32 *score);

	/*
	 * Instantiate a new media object to represent the given partition.
	 */

	virtual IOMedia* instantiateMediaObject(
		int partitionNumber,
		UInt64 formattedLVMSize,
		const char *partitionName,
		UInt64 partitionBase,
		UInt64 partitionSize);

	/*
	 * Allocate a new media object (called from instantiateMediaObject).
	 */

	virtual IOMedia* instantiateDesiredMediaObject(
		/*int partitionNumber,
		UInt64 formattedLVMSize,
		const disk_locn *partition*/);

#ifndef __LP64__
	/*
	 * Attach the given media object to the device tree plane.
	 */

	virtual bool attachMediaObjectToDeviceTree(IOMedia *media)
		__attribute__ ((deprecated));

	/*
	 * Detach the given media object from the device tree plane.
	 */

	virtual void detachMediaObjectFromDeviceTree(IOMedia *media)
		__attribute__ ((deprecated));
#endif /* !__LP64__ */

public:

	/*
	 * Initialize this object's minimal state.
	 */

	virtual bool init(OSDictionary *properties = 0);

	/*
	 * Determine whether the provider media contains an LVM layout.
	 */

	virtual IOService* probe(IOService *provider, SInt32 *score);

	/*
	 * Publish the new media objects which represent our partitions.
	 */

	virtual bool start(IOService *provider);

	/*
	 * Clean up after the media objects we published before terminating.
	 */

	virtual void stop(IOService *provider);

	/*
	 * Request that the provider media be re-scanned for partitions.
	 */

	virtual IOReturn requestProbe(IOOptionBits options);

	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  0);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  1);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  2);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  3);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  4);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  5);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  6);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  7);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  8);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme,  9);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme, 10);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme, 11);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme, 12);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme, 13);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme, 14);
	OSMetaClassDeclareReservedUnused(IOLVMPartitionScheme, 15);
};

#endif /* __cplusplus */
#endif /* KERNEL */
#endif /* !_IOLVMPARTITIONSCHEME_H */
