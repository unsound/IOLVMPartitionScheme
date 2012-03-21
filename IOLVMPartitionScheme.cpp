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

//#define DEBUG

static const struct raw_locn null_raw_locn = { 0, 0, 0, 0 };

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

static bool readLVM2Text(IOMedia *const media, IOLVMPartitionScheme *const obj,
		const UInt64 metadataOffset, const UInt64 metadataSize,
		const raw_locn *const locn)
{
	const UInt64 mediaBlockSize = media->getPreferredBlockSize();

	const UInt64 locnOffset = OSSwapLittleToHostInt64(locn->offset);
	const UInt64 locnSize = OSSwapLittleToHostInt64(locn->size);
	const UInt32 locnChecksum = OSSwapLittleToHostInt32(locn->checksum);
	const UInt32 locnFiller = OSSwapLittleToHostInt32(locn->filler);

	bool res = false;
	IOReturn status = kIOReturnError;

	IOBufferMemoryDescriptor *textBuffer = NULL;
	size_t textBufferInset;
	size_t textBufferSize;

	UInt64 readOffset;

	char *text;
	size_t textLen;

	LogDebug("metadataOffset = %" FMTllu, ARGllu(metadataOffset));
	LogDebug("metadataSize = %" FMTllu, ARGllu(metadataSize));

	LogDebug("mediaBlockSize = %" FMTllu, ARGllu(mediaBlockSize));
	LogDebug("locnOffset = %" FMTllu, ARGllu(locnOffset));
	LogDebug("locnSize = %" FMTllu, ARGllu(locnSize));
	LogDebug("locnChecksum = 0x%" FMTlX, ARGlX(locnChecksum));
	LogDebug("locnFiller = 0x%" FMTlX, ARGlX(locnFiller));

	if(locnOffset >= metadataSize) {
		LogError("locn offset out of range for metadata area (offset: "
			"%" FMTllu " max: %" FMTllu ").",
			ARGllu(locnOffset), ARGllu(metadataSize));
		goto errOut;
	}
	else if(locnSize > (metadataSize - locnOffset)) {
		LogError("locn size out of range for metadata area (size: "
			"%" FMTllu " max: %" FMTllu ").",
			ARGllu(locnSize), ARGllu(metadataSize - locnOffset));
		goto errOut;
	}
	else if(locnSize > SIZE_MAX) {
		LogError("locnSize out of range (%" FMTllu ").",
			ARGllu(locnSize));
		goto errOut;
	}
	else if(mediaBlockSize > SIZE_MAX) {
		LogError("mediaBlockSize out of range (%" FMTllu ").",
			ARGllu(mediaBlockSize));
		goto errOut;
	}

	textBufferInset = (size_t) (locnOffset % mediaBlockSize);
	LogDebug("textBufferInset = %" FMTzu, ARGzu(textBufferInset));

	textBufferSize =
		(size_t) IORound(textBufferInset + locnSize, mediaBlockSize);
	LogDebug("textBufferSize = %" FMTzu, ARGzu(textBufferSize));

	textBuffer = IOBufferMemoryDescriptor::withCapacity(
		/* capacity      */ textBufferSize,
		/* withDirection */ kIODirectionIn);
	if(textBuffer == NULL) {
		LogError("Error while allocating %" FMTzu " bytes of memory "
			"for 'textBuffer'.", ARGzu(textBuffer));
		goto errOut;
	}

	readOffset = metadataOffset + (locnOffset - textBufferInset);
	LogDebug("readOffset = %" FMTllu, ARGllu(readOffset));

	status = media->read(obj, readOffset, textBuffer);
	if(status != kIOReturnSuccess) {
		LogError("Error %d while reading LVM2 text.", status);
		goto errOut;
	}

	text = &((char*) textBuffer->getBytesNoCopy())[textBufferInset];
	textLen = (size_t) locnSize;

	//LogDebug("LVM2 text: %.*s", textLen, text);

	struct parsed_lvm2_text *parsedText;
	if(lvm2_parse_text(text, textLen, &parsedText)) {
		parsed_lvm2_text_destroy(&parsedText);
	}

	res = true;
cleanup:
	if(textBuffer)
		textBuffer->release();

	return res;
errOut:
	goto cleanup;
}

OSSet* IOLVMPartitionScheme::scan(SInt32 *score)
{
	IOBufferMemoryDescriptor *buffer = NULL;
	IOBufferMemoryDescriptor *secondaryBuffer = NULL;
	UInt64 bufferReadAt = 0;
	vm_size_t bufferSize = 0;
	UInt32 secondaryBufferSize = 0;
	IOMedia *media = getProvider();
	UInt64 mediaBlockSize = media->getPreferredBlockSize();
	bool mediaIsOpen = false;
	OSSet *partitions = NULL;
	IOReturn status = kIOReturnError;
	UInt32 i;
	SInt32 firstLabel = -1;

	LogDebug("%s: Entering with score=%p.", __FUNCTION__, score);

	if(mediaBlockSize > SIZE_MAX) {
		LogError("Unrealistic media block size: %" FMTllu,
			ARGllu(mediaBlockSize));
		goto scanErr;
	}

	/* Media must be formatted. */

	if(media->isFormatted() == false)
		goto scanErr;

	LogDebug("\tMedia is formatted.");

	/* Allocate a suitably sized buffer. */

	bufferSize = (size_t) IORound(LVM_SECTOR_SIZE, mediaBlockSize);
	buffer = IOBufferMemoryDescriptor::withCapacity(
		/* capacity      */ bufferSize,
		/* withDirection */ kIODirectionIn);
	if(buffer == NULL) {
		LogError("Error while allocating 'buffer' (%" FMTzu " bytes).",
			ARGzu(bufferSize));
		goto scanErr;
	}

	LogDebug("\tAllocated 'buffer': %" FMTzu " bytes",
		ARGzu(bufferSize));

	secondaryBufferSize =
		(size_t) IORound(LVM_MDA_HEADER_SIZE, mediaBlockSize);
	secondaryBuffer = IOBufferMemoryDescriptor::withCapacity(
		/* capacity      */ secondaryBufferSize,
		/* withDirection */ kIODirectionIn);
	if(secondaryBuffer == NULL) {
		LogError("Error while allocating 'secondaryBuffer' "
			"(%" FMTzu "bytes).",
			ARGzu(secondaryBufferSize));
		goto scanErr;
	}

	LogDebug("\tAllocated 'secondaryBuffer': %" FMTzu " bytes",
		ARGzu(secondaryBufferSize));

	/* Allocate a set to hold the set of media objects representing
	 * partitions. */

	partitions = OSSet::withCapacity(8);
	if(partitions == NULL) {
		LogError("Error while allocating 'partitions'.");
		goto scanErr;
	}

	LogDebug("\tAllocated 'partitions'.");

	/* Open the media with read-only access. */

	mediaIsOpen = open(this, 0, kIOStorageAccessReader);
	if(mediaIsOpen == false) {
		LogError("Error while opening media.");
		goto scanErr;
	}

	LogDebug("\tMedia is open.");

	for(i = 0; i < LVM_LABEL_SCAN_SECTORS; ++i) {
		const struct label_header *labelHeader;
		UInt64 labelSector;
		UInt32 labelCrc;
		UInt32 calculatedCrc;

		bufferReadAt = i * LVM_SECTOR_SIZE;

		LogDebug("Searching for LVM label at sector %" FMTlu "...",
			ARGlu(i));

		status = media->read(this, bufferReadAt, buffer);
		if(status != kIOReturnSuccess) {
			LogError("Error while reading sector %" FMTlu ".",
				ARGlu(i));
			goto scanErr;
		}

		labelHeader = (struct label_header*) buffer->getBytesNoCopy();

		LogDebug("\tlabel_header = {");
		LogDebug("\t\t.id = '%.*s'", 8, labelHeader->id);
		LogDebug("\t\t.sector_xl = %" FMTllu,
			ARGllu(OSSwapLittleToHostInt64(
			labelHeader->sector_xl)));
		LogDebug("\t\t.crc_xl = 0x%08" FMTlX,
			ARGlX(OSSwapLittleToHostInt32(labelHeader->crc_xl)));
		LogDebug("\t\t.offset_xl = %" FMTlu,
			ARGlu(OSSwapLittleToHostInt32(labelHeader->offset_xl)));
		LogDebug("\t\t.type = '%.*s'", 8, labelHeader->type);
		LogDebug("\t}");

		if(memcmp(labelHeader->id, "LABELONE", 8)) {
			LogDebug("\t'id' magic does not match.");
			continue;
		}

		labelSector = OSSwapLittleToHostInt64(labelHeader->sector_xl);
		if(labelSector != i) {
			LogError("'sector_xl' does not match actual sector "
				"(%" FMTllu " != %" FMTlu ").",
				ARGllu(labelSector), ARGlu(i));
			continue;
		}

		labelCrc = OSSwapLittleToHostInt32(labelHeader->crc_xl);
		calculatedCrc = lvm2_calc_crc(LVM_INITIAL_CRC,
			&labelHeader->offset_xl, LVM_SECTOR_SIZE -
			offsetof(label_header, offset_xl));
		if(labelCrc != calculatedCrc) {
			LogError("Stored and calculated CRC32 checksums don't "
				"match (0x%08" FMTlX " != 0x%08" FMTlX ").",
				ARGlX(labelCrc), ARGlX(calculatedCrc));
			continue;
		}

		if(firstLabel == -1) {
			firstLabel = i;
		}
		else {
			LogError("Ignoring additional label at sector %" FMTlu,
				ARGlu(i));
		}
	}

	if(firstLabel < 0) {
		LogDebug("No LVM label found on volume.");
		goto scanErr;
	}

	{
		void *sectorBytes;
		const struct label_header *labelHeader;
		const struct pv_header *pvHeader;

		UInt64 labelSector;
		UInt32 labelCrc;
		UInt32 calculatedCrc;
		UInt32 contentOffset;

		UInt64 deviceSize;

		UInt32 disk_areas_idx;
		size_t dataAreasLength;
		size_t metadataAreasLength;

		bufferReadAt = firstLabel * LVM_SECTOR_SIZE;

		status = media->read(this, bufferReadAt, buffer);
		if(status != kIOReturnSuccess) {
			LogError("\tError while reading label sector "
				"%" FMTlu ".",
				ARGlu(i));
			goto scanErr;
		}

		sectorBytes = buffer->getBytesNoCopy();
		labelHeader = (struct label_header*) sectorBytes;

		/* Re-verify label fields. If the first three don't verify we
		 * have a very strange situation, indicated with the tag
		 * 'Unexpected:' before the error messages. */
		if(memcmp(labelHeader->id, "LABELONE", 8)) {
			LogError("Unexpected: 'id' magic does not match.");
			goto scanErr;
		}

		labelSector = OSSwapLittleToHostInt64(labelHeader->sector_xl);
		if(labelSector != (UInt32) firstLabel) {
			LogError("Unexpected: 'sector_xl' does not match "
				 "actual sector (%" FMTllu " != %" FMTlu ").",
				 ARGllu(labelSector), ARGlu(firstLabel));
			goto scanErr;
		}

		labelCrc = OSSwapLittleToHostInt32(labelHeader->crc_xl);
		calculatedCrc = lvm2_calc_crc(LVM_INITIAL_CRC,
			&labelHeader->offset_xl, LVM_SECTOR_SIZE -
			offsetof(label_header, offset_xl));
		if(labelCrc != calculatedCrc) {
			LogError("Unexpected: Stored and calculated CRC32 "
				 "checksums don't match (0x%08" FMTlX " "
				 "!= 0x%08" FMTlX ").",
				 ARGlx(labelCrc), ARGlX(calculatedCrc));
			goto scanErr;
		}

		contentOffset = OSSwapLittleToHostInt32(labelHeader->offset_xl);
		if(contentOffset < sizeof(struct label_header)) {
			LogError("Content overlaps header (content offset: "
				 "%" FMTlu ").", ARGlu(contentOffset));
			goto scanErr;
		}

		if(memcmp(labelHeader->type, LVM_LVM2_LABEL, 8)) {
			LogError("Unsupported label type: '%.*s'.",
				8, labelHeader->type);
			goto scanErr;
		}

		pvHeader = (struct pv_header*)
			&((char*) sectorBytes)[contentOffset];

		disk_areas_idx = 0;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0) {
			const uintptr_t ptrDiff = (uintptr_t)
				&pvHeader->disk_areas_xl[disk_areas_idx] -
				(uintptr_t) sectorBytes;

			if(ptrDiff > bufferSize) {
				LogError("Data areas overflow into the next "
					"sector (index %" FMTzu ").",
					ARGzu(disk_areas_idx));
				goto scanErr;
			}

			++disk_areas_idx;
		}
		dataAreasLength = disk_areas_idx;
		++disk_areas_idx;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0) {
			const uintptr_t ptrDiff = (uintptr_t)
				&pvHeader->disk_areas_xl[disk_areas_idx] -
				(uintptr_t) sectorBytes;

			if(ptrDiff > bufferSize) {
				LogError("Metadata areas overflow into the "
					"next sector (index %" FMTzu ").",
					ARGzu(disk_areas_idx));
				goto scanErr;
			}

			++disk_areas_idx;
		}
		metadataAreasLength = disk_areas_idx - (dataAreasLength + 1);

		if(dataAreasLength != metadataAreasLength) {
			LogError("Size mismatch between PV data and metadata "
				"areas (%" FMTzu " != %" FMTzu ").",
				ARGzu(dataAreasLength),
				ARGzu(metadataAreasLength));
			goto scanErr;
		}

#if defined(DEBUG)
		LogDebug("\tpvHeader = {");
		LogDebug("\t\tpv_uuid = '%.*s'", LVM_ID_LEN, pvHeader->pv_uuid);
		LogDebug("\t\tdevice_size_xl = %" FMTllu, ARGllu(
			OSSwapLittleToHostInt64(pvHeader->device_size_xl)));
		LogDebug("\t\tdisk_areas_xl = {");
		LogDebug("\t\t\tdata_areas = {");
		disk_areas_idx = 0;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0 &&
			disk_areas_idx < dataAreasLength)
		{
			LogDebug("\t\t\t\t{");
			LogDebug("\t\t\t\t\toffset = %" FMTllu,
				ARGllu(OSSwapLittleToHostInt64(pvHeader->
				disk_areas_xl[disk_areas_idx].offset)));
			LogDebug("\t\t\t\t\tsize = %" FMTllu,
				ARGllu(OSSwapLittleToHostInt64(pvHeader->
				disk_areas_xl[disk_areas_idx].size)));
			LogDebug("\t\t\t\t}");

			++disk_areas_idx;
		}
		LogDebug("\t\t\t}");
		++disk_areas_idx;
		LogDebug("\t\t\tmetadata_areas = {");
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0 &&
			disk_areas_idx < (dataAreasLength + 1 +
			metadataAreasLength))
		{
			LogDebug("\t\t\t\t{");
			LogDebug("\t\t\t\t\toffset = %" FMTllu,
				ARGllu(OSSwapLittleToHostInt64(pvHeader->
				disk_areas_xl[disk_areas_idx].offset)));
			LogDebug("\t\t\t\t\tsize = %" FMTllu,
				ARGllu(OSSwapLittleToHostInt64(pvHeader->
				disk_areas_xl[disk_areas_idx].size)));
			LogDebug("\t\t\t\t}");

			++disk_areas_idx;
		}
		LogDebug("\t\t\t}");
		LogDebug("\t\t}");
		LogDebug("\t}");
#endif /* defined(DEBUG) */

		deviceSize = OSSwapLittleToHostInt64(pvHeader->device_size_xl);
		disk_areas_idx = 0;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0) {
			const disk_locn *locn;
			const disk_locn *meta_locn;
			UInt64 meta_offset;
			UInt64 meta_size;

			const struct mda_header *mdaHeader;
			UInt32 mda_checksum;
			UInt32 mda_version;
			UInt64 mda_start;
			UInt64 mda_size;

			UInt32 mda_calculated_checksum;

			if(disk_areas_idx >= dataAreasLength) {
				LogError("Overflow when iterating through disk "
					"areas (%" FMTzu " >= %" FMTzu ").",
					ARGzu(disk_areas_idx),
					ARGzu(dataAreasLength));
				goto scanErr;
			}

			locn = &pvHeader->disk_areas_xl[disk_areas_idx];
			meta_locn = &pvHeader->disk_areas_xl[dataAreasLength +
				1 + disk_areas_idx];

			meta_offset =
				OSSwapLittleToHostInt64(meta_locn->offset);
			meta_size =
				OSSwapLittleToHostInt64(meta_locn->size);

			status = media->read(this, meta_offset,
				secondaryBuffer);
			if(status != kIOReturnSuccess) {
				LogError("Error while reading first metadata "
					"sector of PV number %" FMTlu " "
					"(offset %" FMTzu " bytes).",
					ARGlu(disk_areas_idx),
					ARGzu(meta_offset));
				goto scanErr;
			}

			mdaHeader = (struct mda_header*)
				secondaryBuffer->getBytesNoCopy();

#if defined(DEBUG)
			LogDebug("mdaHeader[%" FMTlu "] = {",
				ARGlu(disk_areas_idx));
			LogDebug("\tchecksum_xl = 0x%08" FMTlX,
				ARGlX(OSSwapLittleToHostInt32(
				mdaHeader->checksum_xl)));
			LogDebug("\tmagic = '%.*s'", 16, mdaHeader->magic);
			LogDebug("\tversion = %" FMTlu,
				ARGlu(OSSwapLittleToHostInt32(
				mdaHeader->version)));
			LogDebug("\tstart = %" FMTllu,
				ARGllu(OSSwapLittleToHostInt64(
				mdaHeader->start)));
			LogDebug("\tsize = %" FMTllu,
				ARGllu(OSSwapLittleToHostInt64(
				mdaHeader->size)));
			LogDebug("\traw_locns = {");
			{
				size_t raw_locns_idx = 0;

				while(memcmp(&null_raw_locn,
					&mdaHeader->raw_locns[raw_locns_idx],
					sizeof(struct raw_locn)) != 0)
				{
					const raw_locn *const cur_locn =
						&mdaHeader->
						raw_locns[raw_locns_idx];

					LogDebug("\t\t[%" FMTllu "] = {",
						ARGllu(raw_locns_idx));
					LogDebug("\t\t\toffset = %" FMTllu,
						ARGllu(OSSwapLittleToHostInt64(
						cur_locn->offset)));
					LogDebug("\t\t\tsize = %" FMTllu,
						ARGllu(OSSwapLittleToHostInt64(
						cur_locn->size)));
					LogDebug("\t\t\tchecksum = 0x%08" FMTlX,
						ARGlX(OSSwapLittleToHostInt32(
						cur_locn->checksum)));
					LogDebug("\t\t\tfiller = %" FMTlu,
						ARGlu(OSSwapLittleToHostInt32(
						cur_locn->filler)));
					LogDebug("\t\t}");

					++raw_locns_idx;
				}
			}
			LogDebug("\t}");
			LogDebug("}");
#endif /* defined(DEBUG) */

			mda_checksum =
				OSSwapLittleToHostInt32(mdaHeader->checksum_xl);
			mda_version =
				OSSwapLittleToHostInt32(mdaHeader->version);
			mda_start = OSSwapLittleToHostInt64(mdaHeader->start);
			mda_size = OSSwapLittleToHostInt64(mdaHeader->size);

			mda_calculated_checksum = lvm2_calc_crc(LVM_INITIAL_CRC,
				mdaHeader->magic, LVM_MDA_HEADER_SIZE -
				offsetof(struct mda_header, magic));
			if(mda_calculated_checksum != mda_checksum) {
				LogError("mda_header checksum mismatch "
					"(calculated: 0x%" FMTlX " expected: "
					"0x%" FMTlX ").",
					ARGlX(mda_calculated_checksum),
					ARGlX(mda_checksum));
				continue;
			}

			if(mda_version != 1) {
				LogError("Unsupported mda_version: %" FMTlu,
					ARGlu(mda_version));
				continue;
			}

			if(mda_start != meta_offset) {
				LogError("mda_start does not match metadata "
					"offset (%" FMTllu " != %" FMTllu ").",
					ARGllu(mda_start), ARGllu(meta_offset));
				continue;
			}

			if(mda_size != meta_size) {
				LogError("mda_size does not match metadata "
					"size (%" FMTllu " != %" FMTllu ").",
					ARGllu(mda_size), ARGllu(meta_size));
				continue;
			}

			if(memcmp(&mdaHeader->raw_locns[0], &null_raw_locn,
				sizeof(struct raw_locn)) == 0)
			{
				LogError("Missing first raw_locn.");
				continue;
			}
			else if(memcmp(&mdaHeader->raw_locns[1], &null_raw_locn,
				sizeof(struct raw_locn)) != 0)
			{
				LogError("Found more than one raw_locn "
					"(currently unsupported).");
				continue;
			}

			readLVM2Text(media, this, meta_offset, meta_size,
				&mdaHeader->raw_locns[0]);

#if 0
			if(disk_areas_idx > INT_MAX) {
				LogError("Index out of range: %" ARGlu,
					FMTlu(disk_areas_idx));
				break;
			}

			IOMedia *newMedia = instantiateMediaObject(
				(int) disk_areas_idx,
				deviceSize,
				locn);
			if(newMedia) {
				partitions->setObject(newMedia);
				newMedia->release();
			}
			else {
				LogError("Error while creating IOMedia "
					"object.");
			}
#endif

			++disk_areas_idx;
		}
	}

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
	if(secondaryBuffer)
		secondaryBuffer->release();
	if(buffer)
		buffer->release();

	return 0;
}

IOMedia* IOLVMPartitionScheme::instantiateMediaObject(
		const int partitionNumber,
		const UInt64 formattedLVMSize,
		const disk_locn *const partition)
{
	//
	// Instantiate a new media object to represent the given partition.
	//

	IOMedia *media = getProvider();
	UInt64 mediaBlockSize = media->getPreferredBlockSize();
	UInt64 partitionBase = 0;
	const char *partitionHint;
	bool partitionIsWritable = media->isWritable();
	const char *partitionName;
	UInt64 partitionSize = 0;

	partitionHint = "LVM_member";
	partitionName = "";

	// Compute the relative byte position and size of the new partition.

	partitionBase = OSSwapLittleToHostInt64(partition->offset);
	partitionSize = OSSwapLittleToHostInt64(partition->size);

	if(partitionSize == 0) {
		partitionSize = formattedLVMSize - partitionBase;
	}

	// Clip the size of the new partition if it extends past the
	// end-of-media.

	if(partitionBase + partitionSize > media->getSize()) {
		partitionSize = media->getSize() - partitionBase;
	}

#if 0
	// Determine whether the new partition is read-only.
	//
	// Note that we treat the misspelt Apple_patition_map entries as
	// equivalent to Apple_partition_map entries due to the messed up CDs
	// noted in 2513960.

	if(!strncmp(partition->dpme_type, "Apple_partition_map",
			sizeof(partition->dpme_type)) ||
		!strncmp(partition->dpme_type, "Apple_Partition_Map",
			sizeof(partition->dpme_type)) ||
		!strncmp(partition->dpme_type, "Apple_patition_map",
			sizeof(partition->dpme_type)) ||
		(OSSwapBigToHostInt32(partition->dpme_flags) &
		(DPME_FLAGS_WRITABLE | DPME_FLAGS_VALID)) == DPME_FLAGS_VALID)
	{
		partitionIsWritable = false;
	}
#endif

	// Create the new media object.

	IOMedia *newMedia = instantiateDesiredMediaObject();

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
			// Set a name for this partition.

			char name[24];
			snprintf(name, sizeof(name), "Untitled %d",
				(int) partitionNumber);
			newMedia->setName(partitionName[0] ? partitionName :
				name);

			// Set a location value (the partition number) for this
			// partition.

			char location[12];
			snprintf(location, sizeof(location), "%d",
				(int) partitionNumber);
			newMedia->setLocation(location);

			// Set the "Partition ID" key for this partition.

			newMedia->setProperty(kIOMediaPartitionIDKey,
				(UInt32) partitionNumber, 32);
		}
		else {
			newMedia->release();
			newMedia = 0;
		}
	}

	return newMedia;
}

IOMedia* IOLVMPartitionScheme::instantiateDesiredMediaObject(
		/*UInt64 formattedLVMSize,
		disk_locn *partition*/)
{
	//
	// Allocate a new media object (called from instantiateMediaObject).
	//

	return new IOMedia;
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
