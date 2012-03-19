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

#include <IOKit/assert.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/OSByteOrder.h>

#include "IOLVMPartitionScheme.h"

#define DEBUG 1

/* Logging macros. */
#define LOG_CLASSNAME "IOLVMPartitionScheme"
#define LogError(...) \
	do { \
		IOLog("[%s::%s(...)] ", LOG_CLASSNAME, __FUNCTION__); \
		IOLog(__VA_ARGS__); \
		IOLog("\n"); \
	} while(0)

#if DEBUG
#define LogDebug(...) \
	do { \
		IOLog(__VA_ARGS__); \
		IOLog("\n"); \
	} while(0)
#else
#define LogDebug(....) do {} while(0)
#endif

/* Declarations from lvm tools. */
#define LVM_LOGICAL_SECTOR_SIZE 512U
#define LVM_LABEL_SCAN_SECTORS  4U
#define LVM_INITIAL_CRC         0xf597a6cfUL

struct label_header {
	int8_t id[8];		/* LABELONE */
	uint64_t sector_xl;	/* Sector number of this label */
	uint32_t crc_xl;	/* From next field to end of sector */
	uint32_t offset_xl;	/* Offset from start of struct to contents */
	int8_t type[8];		/* LVM2 001 */
} __attribute__ ((packed));

#define super IOPartitionScheme
OSDefineMetaClassAndStructors(IOLVMPartitionScheme, IOPartitionScheme);

bool IOLVMPartitionScheme::init(OSDictionary *properties)
{
	bool status;

	LogDebug("%s: Entering with: properties=%p.",
		__FUNCTION__, properties);

	/* Verify that the compiler didn't mess with our struct definitions. */
	assert(sizeof(struct label_header) == 32);

	/* First call the init method of super to get a go-ahead. */
	if(super::init(properties) == false)
		status = false;
	else {
		/* Initialize a minimal set of state variables. */
		_partitions = NULL;
		status = true;
	}

	return status;
}

void IOLVMPartitionScheme::free()
{
	LogDebug("%s: Entering.", __FUNCTION__);

	/* Clean up our state. */
	if(_partitions)
		_partitions->release();

	/* Ask super to clean up its state. */
	super::free();
}

IOService* IOLVMPartitionScheme::probe(IOService* provider, SInt32* score)
{
	IOService *res;

	LogDebug("%s: Entering with provider=%p score=%p.",
		__FUNCTION__, provider, score);

	/* Various assumptions. */
	assert(OSDynamicCast(IOMedia, provider));

	/* First call the probe method of super to get a go-ahead. */
	if(super::probe(provider, score) == NULL)
		res = NULL;
	else {
		/* Scan 'provider' for an LVM volume layout. */
		_partitions = scan(score);

		res =  (_partitions) ? this : NULL;
	}

	return res;
}

bool IOLVMPartitionScheme::start(IOService* provider)
{
	bool res;
	IOMedia *partition;
	OSIterator *partitionIterator;

	LogDebug("%s: Entering with provider=%p.", __FUNCTION__, provider);

	/* Various assumptions. */
	assert(_partitions);

	/* First call the start method of super to get a go-ahead. */
	if(super::start(provider) == false)
		res = false;
	else {
		partitionIterator =
			OSCollectionIterator::withCollection(_partitions);
		if(partitionIterator == NULL)
			res = false;
		else {
			while((partition = (IOMedia *)
				partitionIterator->getNextObject()))
			{
				if(partition->attach(this)) {
					attachMediaObjectToDeviceTree(
						partition);

					partition->registerService();
				}
			}

			partitionIterator->release();
			res = true;
		}
	}

	return res;
}

void IOLVMPartitionScheme::stop(IOService* provider)
{
	IOMedia *partition;
	OSIterator *partitionIterator;

	LogDebug("%s: Entering with provider=%p.", __FUNCTION__, provider);

	/* Various assumptions. */
	assert(_partitions);

	/* Detach the previously attached media objects from the device tree. */
	partitionIterator = OSCollectionIterator::withCollection(_partitions);
	if(partitionIterator) {
		while((partition = (IOMedia*)
			partitionIterator->getNextObject()))
		{
			detachMediaObjectFromDeviceTree(partition);
		}

		partitionIterator->release();
	}

	super::stop(provider);
}

/**
 * "Request that the provider media be re-scanned for partitions."
 */
IOReturn IOLVMPartitionScheme::requestProbe(IOOptionBits options)
{
	OSSet *partitions = NULL;
	OSSet *partitionsNew;
	SInt32 score = 0;

	LogDebug("%s: Entering with options=0x%08lX.", __FUNCTION__, options);

	/* Scan for an LVM layout. */
	partitionsNew = scan(&score);

	if(partitionsNew) {
		if(lockForArbitration(false)) {
			partitions = juxtaposeMediaObjects(_partitions,
				partitionsNew);

			if(partitions) {
				_partitions->release();

				_partitions = partitions;
			}

			unlockForArbitration();
		}

		partitionsNew->release();
	}

	return partitions ? kIOReturnSuccess : kIOReturnError;
}

#if !defined(NO_LGPL_CODE)
static uint32_t calc_crc(uint32_t initial, const void *buf, uint32_t size)
{
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	uint32_t i, crc = initial;
	const uint8_t *data = (const uint8_t *) buf;

	LogDebug("%s: Entering with initial=0x%08lX buf=%p size=%lu.",
		__FUNCTION__, (unsigned long) initial, buf,
		(unsigned long) size);

	for (i = 0; i < size; i++) {
		crc ^= *data++;
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}
	return crc;
}
#endif /* !defined(NO_LGPL_CODE) */

OSSet* IOLVMPartitionScheme::scan(SInt32 *score)
{
	IOBufferMemoryDescriptor *buffer = NULL;
	UInt64 bufferReadAt = 0;
	UInt32 bufferSize = 0;
	UInt64 contentOffset = 0;
	IOMedia *media = getProvider();
	UInt64 mediaBlockSize = media->getPreferredBlockSize();
	bool mediaIsOpen = false;
	OSSet *partitions = NULL;
	IOReturn status = kIOReturnError;
	UInt32 i;
	SInt32 firstLabel = -1;

	LogDebug("%s: Entering with score=%p.", __FUNCTION__, score);

	/* Media must be formatted. */

	if(media->isFormatted() == false)
		goto scanErr;

	LogDebug("\tMedia is formatted.");

	/* Allocate a suitably sized buffer. */

	bufferSize = IORound(LVM_LOGICAL_SECTOR_SIZE, mediaBlockSize);
	buffer = IOBufferMemoryDescriptor::withCapacity(
		/* capacity      */ bufferSize,
		/* withDirection */ kIODirectionIn);
	if(buffer == NULL) {
		LogError("\tError while allocating 'buffer' (%lu bytes).",
			 (unsigned long) bufferSize);
		goto scanErr;
	}

	LogDebug("\tAllocated 'buffer': %lu bytes",
		(unsigned long) bufferSize);

	/* Allocate a set to hold the set of media objects representing
	 * partitions. */

	partitions = OSSet::withCapacity(8);
	if(partitions == NULL) {
		LogError("\tError while allocating 'partitions'.");
		goto scanErr;
	}

	LogDebug("\tAllocated 'partitions'.");

	/* Open the media with read-only access. */

	mediaIsOpen = open(this, 0, kIOStorageAccessReader);
	if(mediaIsOpen == false) {
		LogError("\tError while opening media.");
		goto scanErr;
	}

	LogDebug("\tMedia is open.");

	for(i = 0; i < LVM_LABEL_SCAN_SECTORS; ++i) {
		const struct label_header *labelHeader;
		UInt64 labelSector;
		UInt32 labelCrc;
		UInt32 calculatedCrc;

		bufferReadAt = i * LVM_LOGICAL_SECTOR_SIZE;

		LogDebug("Searching for LVM label at sector %lu...",
			 (unsigned long) i);

		status = media->read(this, bufferReadAt, buffer);
		if(status != kIOReturnSuccess) {
			LogError("\tError while reading sector %lu.",
				 (unsigned long) i);
			goto scanErr;
		}

		labelHeader = (struct label_header*) buffer->getBytesNoCopy();

		LogDebug("\tlabel_header = {");
		LogDebug("\t\t.id = '%.*s'", 8, labelHeader->id);
		LogDebug("\t\t.sector_xl = %llu",
			(unsigned long long)
			OSSwapLittleToHostInt64(labelHeader->sector_xl));
		LogDebug("\t\t.crc_xl = 0x%08lX",
			(unsigned long)
			OSSwapLittleToHostInt32(labelHeader->crc_xl));
		LogDebug("\t\t.offset_xl = %lu",
			(unsigned long)
			OSSwapLittleToHostInt32(labelHeader->offset_xl));
		LogDebug("\t\t.type = '%.*s'", 8, labelHeader->type);
		LogDebug("\t}");

		if(memcmp(labelHeader->id, "LABELONE", 8)) {
			LogDebug("\t'id' magic does not match.");
			continue;
		}

		labelSector = OSSwapLittleToHostInt64(labelHeader->sector_xl);
		if(labelSector != i) {
			LogError("'sector_xl' does not match actual sector "
				 "(%llu != %lu).",
				 (unsigned long long) labelSector,
				 (unsigned long) i);
			continue;
		}

		/* TODO: Verfiy sector CRC. */
		labelCrc = OSSwapLittleToHostInt32(labelHeader->crc_xl);
		calculatedCrc = calc_crc(LVM_INITIAL_CRC,
			&labelHeader->offset_xl, LVM_LOGICAL_SECTOR_SIZE -
			offsetof(label_header, offset_xl));
		if(labelCrc != calculatedCrc) {
			LogError("Stored and calculated CRC32 checksums don't "
				"match (0x%08lX != 0x%08lX).",
				(unsigned long) labelCrc,
				(unsigned long) calculatedCrc);
			continue;
		}

#if 0 /* This check is not in the LVM tools, so maybe not valid. */
		if(memcmp(labelHeader->type, "LVM2 001", 8)) {
			LogError("'type' magic does not match.");
			continue;
		}
#endif

		if(firstLabel == -1) {
			firstLabel = i;
			contentOffset =
				bufferReadAt +
				OSSwapLittleToHostInt32(labelHeader->offset_xl);
			/* ...? */
		}
		else {
			LogError("Ignoring additional label at sector %lu",
				(unsigned int) i);
		}
	}

	if(firstLabel == -1) {
		LogDebug("No LVM label found on volume.");
		goto scanErr;
	}

	/*
	bufferReadAt = 512 * contentOffset;

	LogDebug("Reading first sector of content from byte offset: %llu",
		 (unsigned long long) bufferReadAt);

	status = media->read(this, bufferReadAt, buffer);
	if(status != kIOReturnSuccess) {
		LogError("\tError while reading first sector of content.");
		goto scanErr;
	}
	*/

	goto scanErr;

	// Release our resources.

	close(this);
	buffer->release();

	return partitions;

scanErr:

	// Release our resources.

	if(mediaIsOpen)
		close(this);
	if(partitions)
		partitions->release();
	if(buffer)
		buffer->release();

	return 0;
}

#ifndef __LP64__
bool IOLVMPartitionScheme::attachMediaObjectToDeviceTree(IOMedia *media)
{
	//
	// Attach the given media object to the device tree plane.
	//

	return super::attachMediaObjectToDeviceTree(media);
}

void IOLVMPartitionScheme::detachMediaObjectFromDeviceTree(IOMedia *media)
{
	//
	// Detach the given media object from the device tree plane.
	//

	super::detachMediaObjectFromDeviceTree(media);
}
#endif /* !__LP64__ */

OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  0);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  1);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  2);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  3);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  4);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  5);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  6);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  7);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  8);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme,  9);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme, 10);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme, 11);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme, 12);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme, 13);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme, 14);
OSMetaClassDefineReservedUnused(IOLVMPartitionScheme, 15);
