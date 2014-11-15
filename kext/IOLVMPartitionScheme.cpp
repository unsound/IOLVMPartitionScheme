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
#include <sys/errno.h>

#define LOG_CLASSNAME "IOLVMPartitionScheme"

#include "IOLVMPartitionScheme.h"
#include "lvm2_osal.h"
#include "lvm2_text.h"

/* Forward declarations. */

static IOMedia* instantiateMediaObject(IOLVMPartitionScheme *obj,
	int partitionNumber, UInt64 formattedLVMSize, const char *partitionName,
	UInt64 partitionBase, UInt64 partitionSize, bool partitionIsWritable,
	const char *partitionHint);

#define super IOPartitionScheme
OSDefineMetaClassAndStructors(IOLVMPartitionScheme, IOPartitionScheme);

bool IOLVMPartitionScheme::init(OSDictionary *properties)
{
	bool status;

	LogDebug("%s: Entering with: properties=%p.",
		__FUNCTION__, properties);

	/* Verify that the compiler didn't mess with our struct definitions. */
	if(!lvm2_check_layout()) {
		LogError("Invalid layout of on-disk struct definitions.");
		status = false;
	}
	else if(super::init(properties) == false) {
		/* Call the init method of super to get a go-ahead. */
		status = false;
	}
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
IOReturn IOLVMPartitionScheme::requestProbe(
		IOOptionBits options __attribute__((unused)))
{
	OSSet *partitions = NULL;
	OSSet *partitionsNew;
	SInt32 score = 0;

	LogDebug("%s: Entering with options=0x%08" FMTlX ".",
		__FUNCTION__, ARGlX(options));

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

struct LVMDeviceReadContext {
	IOLVMPartitionScheme *obj;
	OSSet *partitions;
	int partitionNumber;
};

static lvm2_bool volumeCallback(void *const privateData, const u64 deviceSize,
		const char *const volumeName, const u64 volumeStart,
		const u64 volumeLength, const lvm2_bool isIncomplete)
{
	struct LVMDeviceReadContext *const ctx =
		(struct LVMDeviceReadContext*) privateData;
	IOMedia *const media = ctx->obj->getProvider();
	IOMedia *newMedia;

	const char *partitionHint;
	bool partitionIsWritable;

	LogDebug("%s: Entering with privateData=%p deviceSize=%" FMTllu " "
		"volumeName=%s volumeStart=%" FMTllu " "
		"volumeLength=%" FMTllu "...",
		__FUNCTION__, privateData, ARGllu(deviceSize), volumeName,
		ARGllu(volumeStart), ARGllu(volumeLength));

	if(!isIncomplete) {
		/* We use "Linux" as partition hint as this will surely work
		 * best given that an LVM volume containing a file system will
		 * most likely always contain a Linux file system. We do not
		 * know however if there is a file system on the LVM volume (it
		 * could be for instance swap space or raw space reserved for
		 * database storage). So one could argue that no partition hint
		 * at all would be more appropriate, but at this stage I think
		 * "Linux" is more helpful. */
		partitionIsWritable = media->isWritable();
		partitionHint = "Linux";
	}
	else {
		/* For incomplete volumes, we expose the raw PVs as read-only
		 * devices (for recovery purposes). */
		partitionIsWritable = false;
		partitionHint = "LVM_incomplete_logical_volume";
	}

	/* TODO: Check that partition is inside device bounds, doesn't overlap
	 * other partitions, ... */
	newMedia = instantiateMediaObject(ctx->obj, ctx->partitionNumber++,
		deviceSize, volumeName, volumeStart, volumeLength,
		partitionIsWritable, partitionHint);
	if(newMedia) {
		LogDebug("Instantiated media object.");
		ctx->partitions->setObject(newMedia);
		newMedia->release();
		return LVM2_TRUE;
	}
	else {
		LogError("Error while creating IOMedia object.");
		return LVM2_FALSE;
	}
}

OSSet* IOLVMPartitionScheme::scan(SInt32 *score __attribute__((unused)))
{
	IOMedia *const media = getProvider();

	int err = 0;
	OSSet *partitions = NULL;
	struct lvm2_device *dev = NULL;
	struct LVMDeviceReadContext ctx;

	LogDebug("%s: Entering with score=%p.", __FUNCTION__, score);

	/* Media must be formatted. */

	if(media->isFormatted() == false)
		goto err_out;

	LogDebug("\tMedia is formatted.");

	/* Allocate a set to hold the set of media objects representing
	 * partitions. */

	partitions = OSSet::withCapacity(8);
	if(partitions == NULL) {
		LogError("Error while allocating 'partitions'.");
		goto err_out;
	}

	LogDebug("\tAllocated 'partitions'.");

	err = lvm2_iokit_device_create(this, media, &dev);
	if(err) {
		LogError("Error while opening device: %d", err);
		goto err_out;
	}

	ctx.obj = this;
	ctx.partitions = partitions;
	ctx.partitionNumber = 0;

	err = lvm2_parse_device(dev, volumeCallback, &ctx);
	if(err) {
		LogDebug("Error while parsing LVM2 structures: %d", err);
		goto err_out;
	}

cleanup:
	if(dev)
		lvm2_iokit_device_destroy(&dev);

	return partitions;

err_out:
	if(partitions) {
		partitions->release();
		partitions = NULL;
	}

	goto cleanup;
}

/**
 * Instantiate a new media object to represent an LVM partition.
 */
static IOMedia* instantiateMediaObject(IOLVMPartitionScheme *const obj,
		const int partitionNumber,
		const UInt64 formattedLVMSize __attribute__((unused)),
		const char *const partitionName,
		const UInt64 partitionBase,
		UInt64 partitionSize,
		const bool partitionIsWritable,
		const char *const partitionHint)
{
	IOMedia *const media = obj->getProvider();
	const UInt64 mediaBlockSize = media->getPreferredBlockSize();

	IOMedia *newMedia;

	LogDebug("Entering with obj=%p partitionNumber=%d "
		"formattedLVMsize=%" FMTllu " paritionName=%p (%s) "
		"partitionBase=%" FMTllu " partitionSize=%" FMTllu,
		obj, partitionNumber, ARGllu(formattedLVMSize), partitionName,
		partitionName ? partitionName : "<null>", ARGllu(partitionBase),
		ARGllu(partitionSize));

	/* Clip the size of the new partition if it extends past the
	 * end-of-media. */

	if(partitionBase + partitionSize > media->getSize()) {
		LogError("Warning: Specified partition extends past end of "
			"media ((%" FMTllu " + %" FMTllu ") > %" FMTllu "). "
			"Clipping...",
			ARGllu(partitionBase), ARGllu(partitionSize),
			ARGllu(media->getSize()));
		partitionSize = media->getSize() - partitionBase;
	}

	/* Create the new media object. */

	newMedia = new IOMedia;
	if(newMedia) {
		if(newMedia->init(
			/* base               */ partitionBase,
			/* size               */ partitionSize,
			/* preferredBlockSize */ mediaBlockSize,
			/* attributes         */ media->getAttributes(),
			/* isWhole            */ false,
			/* isWritable         */ partitionIsWritable,
			/* contentHint        */ partitionHint))
		{
			/* Set a name for this partition. */

			char name[24];
			snprintf(name, sizeof(name), "Untitled %d",
				(int) partitionNumber);
			newMedia->setName(partitionName[0] ? partitionName :
				name);

			/* Set a location value (the partition number) for this
			 * partition. */

			char location[12];
			snprintf(location, sizeof(location), "%d",
				(int) partitionNumber);
			newMedia->setLocation(location);

			/* Set the "Partition ID" key for this partition. */

			newMedia->setProperty(kIOMediaPartitionIDKey,
				(UInt32) partitionNumber, 32);
		}
		else {
			newMedia->release();
			newMedia = NULL;
		}
	}

	return newMedia;
}

#ifndef __LP64__
bool IOLVMPartitionScheme::attachMediaObjectToDeviceTree(IOMedia *media)
{
	/* Attach the given media object to the device tree plane. */

	return super::attachMediaObjectToDeviceTree(media);
}

void IOLVMPartitionScheme::detachMediaObjectFromDeviceTree(IOMedia *media)
{
	/* Detach the given media object from the device tree plane. */

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
