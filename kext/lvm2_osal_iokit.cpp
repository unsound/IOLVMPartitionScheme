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
#include "lvm2_types.h"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOStorage.h>

#include <machine/limits.h>
#include <sys/errno.h>

__private_extern__ int lvm2_malloc(size_t size, void **out_ptr)
{
	void *ptr;

	ptr = IOMalloc(size);
	if(ptr) {
		*out_ptr = ptr;
		return 0;
	}
	else {
		return ENOMEM;
	}
}

__private_extern__ void lvm2_free(void **ptr, size_t size)
{
	IOFree(*ptr, size);
	*ptr = NULL;
}

/* Device layer implementation. */

struct lvm2_io_buffer {
	IOBufferMemoryDescriptor *buffer;
};

__private_extern__ int lvm2_io_buffer_create(size_t size,
		struct lvm2_io_buffer **out_buf)
{
	int err;
	struct lvm2_io_buffer *buf = NULL;

	LogDebug("%s: Entering with out_buf=%p", __FUNCTION__, out_buf);

	LogDebug("Allocating struct lvm2_io_buffer (%" FMTzu " bytes)....",
		ARGzu(sizeof(struct lvm2_io_buffer)));
	err = lvm2_malloc(sizeof(struct lvm2_io_buffer), (void**) &buf);
	if(!err) {
		IOBufferMemoryDescriptor *buffer;

		LogDebug("Allocating IOBufferMemoryDescriptor (%" FMTzu " "
			"bytes)....", ARGzu(size));
		buffer = IOBufferMemoryDescriptor::withCapacity(
			/* capacity      */ size,
			/* withDirection */ kIODirectionIn);
		if(buffer == NULL) {
			LogError("Error while allocating "
				"IOBufferMemoryDescriptor with buffer size: "
				"%" FMTzu "bytes",
				ARGzu(size));
			err = ENOMEM;
		}
		else {
			LogDebug("Successsfully created lvm2_io_buffer.");
			buf->buffer = buffer;
			*out_buf = buf;
		}

		if(err) {
			lvm2_free((void**) &buf, sizeof(struct lvm2_io_buffer));
		}
	}

	LogDebug("%s: Leaving with %d.", __FUNCTION__, err);

	return err;
}

__private_extern__ const void* lvm2_io_buffer_get_bytes(
		struct lvm2_io_buffer *buf)
{
	void *result;

	LogDebug("%s: Entering with buf=%p", __FUNCTION__, buf);

	result = buf->buffer->getBytesNoCopy();

	LogDebug("%s: Leaving.", __FUNCTION__);

	return result;
}

__private_extern__ void lvm2_io_buffer_destroy(struct lvm2_io_buffer **buf)
{
	LogDebug("%s: Entering with buf=%p", __FUNCTION__, buf);

	(*buf)->buffer->release();
	lvm2_free((void**) buf, sizeof(struct lvm2_io_buffer));

	LogDebug("%s: Leaving.", __FUNCTION__);
}

struct lvm2_device {
	IOStorage *storage;
	IOMedia *media;
	u32 block_size;
};

__private_extern__ int lvm2_iokit_device_create(IOStorage *const storage,
		IOMedia *const media, struct lvm2_device **const out_dev)
{
	int err;
	UInt64 mediaBlockSize;
	bool mediaIsOpen = false;

	LogDebug("%s: Entering with storage=%p media=%p out_dev=%p",
		__FUNCTION__, storage, media, out_dev);

	mediaBlockSize = media->getPreferredBlockSize();
	if(mediaBlockSize > U32_MAX) {
		LogError("Unrealistic media block size: %" FMTllu,
			ARGllu(mediaBlockSize));
		err = EINVAL;
	}
	else {
		/* Open the media with read-only access. */

		mediaIsOpen = storage->open(storage, 0, kIOStorageAccessReader);
		if(mediaIsOpen == false) {
			LogError("Error while opening media.");
			err = EACCES;
		}
		else {
			struct lvm2_device *dev;

			err = lvm2_malloc(sizeof(struct lvm2_device),
				(void**) &dev);
			if(!err) {
				memset(dev, 0, sizeof(struct lvm2_device));
				dev->storage = storage;
				dev->media = media;
				dev->block_size = (u32) mediaBlockSize;

				*out_dev = dev;
			}

			if(err) {
				storage->close(storage);
			}
		}
	}

	LogDebug("%s: Leaving with %d.", __FUNCTION__, err);

	return err;
}

__private_extern__ void lvm2_iokit_device_destroy(struct lvm2_device **dev)
{
	LogDebug("%s: Entering with dev=%p.", __FUNCTION__, dev);

	(*dev)->storage->close((*dev)->storage);

	lvm2_free((void**) dev, sizeof(struct lvm2_device));

	LogDebug("%s: Leaving.", __FUNCTION__);
}

__private_extern__ int lvm2_device_read(struct lvm2_device *const dev,
		const u64 in_pos, const size_t in_count,
		struct lvm2_io_buffer *const in_buf)
{
	int err;
	IOReturn status;

	u32 lead_in;
	u32 lead_out;

	u64 pos;
	size_t count;
	IOBufferMemoryDescriptor *buf;

	LogDebug("%s: Entering with dev=%p in_pos=%" FMTllu " "
		"in_count=%" FMTzu " in_buf=%p",
		__FUNCTION__, dev, ARGllu(in_pos), ARGzu(in_count), in_buf);

	if(in_count > SSIZE_MAX) {
		LogDebug("%s: 'in_count' overflows. Leaving with %d.", __FUNCTION__, ERANGE);
		return ERANGE;
	}

	lead_in = (u32) (in_pos % dev->block_size);
	lead_out = (dev->block_size - (u32) ((lead_in + in_count) %
		dev->block_size)) % dev->block_size;

	LogDebug("lead_in=%" FMTllu, ARGllu(lead_in));
	LogDebug("lead_out=%" FMTllu, ARGllu(lead_out));
	LogDebug("in_buf->buffer->getLength()=%" FMTllu, ARGllu(in_buf->buffer->getLength()));

	if(lead_in != 0 || lead_out != 0 ||
		in_count != in_buf->buffer->getLength())
	{
		u64 aligned_pos;
		size_t aligned_count;
		IOBufferMemoryDescriptor *aligned_buf = NULL;

		aligned_pos = in_pos - lead_in;
		aligned_count = lead_in + in_count + lead_out;

		LogError("Warning: Unaligned read. Aligning (%" FMTllu ", "
			"%" FMTzu ") -> (%" FMTllu ", %" FMTzu ")...",
			ARGllu(in_pos), ARGzu(in_count), ARGllu(aligned_pos),
			ARGzu(aligned_count));

		aligned_buf = IOBufferMemoryDescriptor::withCapacity(
			/* capacity      */ aligned_count,
			/* withDirection */ kIODirectionIn);
		if(aligned_buf == NULL) {
			LogError("Temporary memory allocation (%" FMTzu " "
				"bytes) failed.",
				ARGzu(aligned_count));
			LogDebug("%s: Leaving with %d.", __FUNCTION__, ENOMEM);
			return ENOMEM;
		}

		pos = aligned_pos;
		count = aligned_count;
		buf = aligned_buf;
	}
	else {
		pos = in_pos;
		count = in_count;
		buf = in_buf->buffer;
	}

	status = dev->media->read(dev->storage, pos, buf);
	if(status != kIOReturnSuccess) {
		err = EIO;
	}
	else {
		if(buf != in_buf->buffer) {
			IOByteCount res;
			res = in_buf->buffer->writeBytes(0,
				buf->getBytesNoCopy(), in_count);
			if(res != in_count) {
				LogError("Failed to write data back into the "
					"input buffer. Wrote "
					"%" FMTzu "/%" FMTzu " bytes.",
					ARGzu(res), ARGzu(in_count));
			}
		}

		err = 0;
	}

	if(buf != in_buf->buffer) {
		buf->release();
	}

	LogDebug("%s: Leaving with %d", __FUNCTION__, err);

	return err;
}

__private_extern__ u32 lvm2_device_get_alignment(struct lvm2_device *dev)
{
	return dev->block_size;
}
