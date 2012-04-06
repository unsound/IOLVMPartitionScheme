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
#include "lvm2_device.h"

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static long long allocations = 0;

int lvm2_malloc(size_t size, void **out_ptr)
{
	void *ptr;

	ptr = malloc(size);
	if(ptr) {
		*out_ptr = ptr;
		++allocations;
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
	--allocations;
}

long long lvm2_get_allocations(void);

long long lvm2_get_allocations(void) {
	return allocations;
}

/* Device layer implementation. */

struct lvm2_io_buffer {
	size_t size;
	void *data;
};

int lvm2_io_buffer_create(size_t size, struct lvm2_io_buffer **out_buf)
{
	int err;
	struct lvm2_io_buffer *buf = NULL;

	err = lvm2_malloc(sizeof(struct lvm2_io_buffer), (void**) &buf);
	if(!err) {
		void *data;

		err = lvm2_malloc(size, &data);
		if(!err) {
			buf->size = size;
			buf->data = data;

			*out_buf = buf;
		}

		if(err) {
			lvm2_free((void**) &buf, sizeof(struct lvm2_io_buffer));
		}
	}

	return err;
}

const void* lvm2_io_buffer_get_bytes(struct lvm2_io_buffer *buf)
{
	return buf->data;
}

void lvm2_io_buffer_destroy(struct lvm2_io_buffer **buf)
{
	lvm2_free(&(*buf)->data, (*buf)->size);
	lvm2_free((void**) buf, sizeof(struct lvm2_io_buffer));
}

static int lvm2_unix_get_block_size(int fd, u32 *out_block_size);

#if defined(__linux__) || defined(ANDROID) || defined(__ANDROID__)
#include <linux/fs.h>

static int lvm2_unix_get_block_size(int fd, u32 *out_block_size)
{
	int err;
	int res;
	int sector_size = 0;

#if defined(BLKPBSZGET)
	res = ioctl(fd, BLKPBSZGET, &sector_size);
#else
	res = ioctl(fd, BLKBSZGET, &sector_size);
#endif
	if(res) {
		err = errno ? errno : EIO;
	}
	else if(sector_size < 0 || sector_size > U32_MAX) {
		err = ERANGE;
	}
	else {
		*out_block_size = (u32) sector_size;
		err = 0;
	}

	return err;
}
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/disk.h>

static int lvm2_unix_get_block_size(int fd, u32 *out_block_size)
{
	int err;
	int res;
	u_int sector_size = 0;

	res = ioctl(fd, DIOCGSECTORSIZE, &sector_size);
	if(res) {
		err = errno ? errno : EIO;
	}
	else if(sector_size > U32_MAX) {
		err = ERANGE;
	}
	else {
		*out_block_size = (u32) sector_size;
		err = 0;
	}

	return err;
}
#elif defined(__APPLE__) || defined(__DARWIN__)
#include <sys/disk.h>

static int lvm2_unix_get_block_size(int fd, u32 *out_block_size)
{
	int err;
	int res;
	uint32_t sector_size = 0;

#if defined(DKIOCGETPHYSICALBLOCKSIZE)
	res = ioctl(fd, DKIOCGETPHYSICALBLOCKSIZE, &sector_size);
#else
	res = ioctl(fd, DKIOCGETBLOCKSIZE, &sector_size);
#endif
	if(res) {
		err = errno ? errno : EIO;
	}
	else if(sector_size > U32_MAX) {
		err = ERANGE;
	}
	else {
		*out_block_size = (u32) sector_size;
		err = 0;
	}

	return err;
}
#else
#error "Add OS-specific ioctl for getting a device's minimum block size here."
#endif

struct lvm2_device {
	int fd;
	u32 block_size;
};

int lvm2_unix_device_create(const char *const name,
		struct lvm2_device **const out_dev)
{
	int err;
	int fd;

	fd = open(name, O_RDONLY);
	if(fd == -1) {
		err = errno ? errno : ENOENT;
	}
	else {
		int res;
		struct stat stbuf;
		u32 block_size;

		memset(&stbuf, 0, sizeof(struct stat));
		res = fstat(fd, &stbuf);
		if(res == -1) {
			err = errno ? errno : EIO;
		}
		else if(stbuf.st_mode & S_IFREG) {
			block_size = 1;
		}
		else {
			block_size = 0;
			err = lvm2_unix_get_block_size(fd, &block_size);
			if(err) {
				LogError("Error while getting block size for "
					"device: %d (%s)", err, strerror(err));
			}
		}

		if(!err) {
			struct lvm2_device *dev;

			err = lvm2_malloc(sizeof(struct lvm2_device),
				(void**) &dev);
			if(!err) {
				memset(dev, 0, sizeof(struct lvm2_device));
				dev->fd = fd;
				dev->block_size = block_size;

				*out_dev = dev;
			}
		}
	}

	return err;
}

void lvm2_unix_device_destroy(struct lvm2_device **dev)
{
	if(close((*dev)->fd) == -1) {
		LogError("Ignoring error on close: %d (%s)",
			errno, strerror(errno));
	}

	lvm2_free((void**) dev, sizeof(struct lvm2_device));
}

int lvm2_device_read(struct lvm2_device *const dev, const u64 in_pos,
		const size_t in_count, struct lvm2_io_buffer *const in_buf)
{
	static const off_t max_off_t =
		((off_t) 1) << ((sizeof(off_t) * 8) - 1);

	int err;
	ssize_t res;

	u32 lead_in;
	u32 lead_out;

	u64 pos;
	size_t count;
	void *buf;

	if(in_pos > (u64) max_off_t)
		return ERANGE;
	if(in_count > in_buf->size || in_count > SSIZE_MAX)
		return ERANGE;

	lead_in = (u32) (in_pos % dev->block_size);
	lead_out = dev->block_size -
		(u32) ((lead_in + in_count) % dev->block_size);

	if(lead_in != 0 || lead_out != 0) {
		u64 aligned_pos;
		size_t aligned_count;
		void *aligned_buf = NULL;

		aligned_pos = in_pos - lead_in;
		aligned_count = lead_in + in_count + lead_out;

		LogError("Warning: Unaligned read. Aligning (%" FMTllu ", "
			"%" FMTzu ") -> (%" FMTllu ", %" FMTzu ")...",
			ARGllu(in_pos), ARGzu(in_count), ARGllu(aligned_pos),
			ARGzu(aligned_count));

		err = lvm2_malloc(aligned_count, &aligned_buf);
		if(err) {
			LogError("Temporary memory allocation (%" FMTzu " "
				"bytes) failed: %d (%s)",
				ARGzu(aligned_count), err, strerror(err));
			return err;
		}

		pos = aligned_pos;
		count = aligned_count;
		buf = aligned_buf;
	}
	else {
		pos = in_pos;
		count = in_count;
		buf = in_buf->data;
	}

	res = pread(dev->fd, buf, count, (off_t) pos);
	if(res == -1) {
		err = errno ? errno : EIO;
	}
	else if(res != (ssize_t) count) {
		/* We don't accept partial reads... it's either the full read or
		 * nothing. */
		err = EIO;
	}
	else {
		if(buf != in_buf->data) {
			memcpy(in_buf, &((char*) buf)[lead_in], in_count);
		}

		err = 0;
	}

	if(buf != in_buf->data) {
		lvm2_free(&buf, count);
	}

	return err;
}
