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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#include "lvm2_log.h"
#include "lvm2_text.h"

/* Defined in lvm2_osal_unix.c. */
long long lvm2_get_allocations(void);

#define emit(...) \
	do { \
		fprintf(stderr, __VA_ARGS__); \
		fprintf(stderr, "\n"); \
	} while(0)

static void print_lvm2_stripe(struct lvm2_stripe *stripe)
{
	emit("\t\t\t\tpv_name: %s", stripe->pv_name->content);
	emit("\t\t\t\textent_start: %" FMTllu, ARGllu(stripe->extent_start));
}

static void print_lvm2_segment(struct lvm2_segment *segment)
{
	emit("\t\t\tstart_extent: %" FMTllu, ARGllu(segment->start_extent));
	emit("\t\t\textent_count: %" FMTllu, ARGllu(segment->extent_count));
	emit("\t\t\ttype: %s", segment->type->content);
	emit("\t\t\tstripe_count: %" FMTllu, ARGllu(segment->stripe_count));

	if(segment->stripes_len) {
		size_t i;

		for(i = 0; i < segment->stripes_len; ++i) {
			emit("\t\t\tstripes[%" FMTzu "]:",
				ARGzu(i));
			print_lvm2_stripe(segment->stripes[i]);
		}
	}
}

static void print_lvm2_logical_volume(struct lvm2_logical_volume *lv)
{
	emit("\t\tname: %s", lv->name->content);
	emit("\t\tid: %s", lv->id->content);
	emit("\t\tstatus: 0x%X", lv->status);
	emit("\t\tflags: 0x%X", lv->flags);
	emit("\t\tsegment_count: %" FMTllu, ARGllu(lv->segment_count));

	if(lv->segments_len) {
		size_t i;

		for(i = 0; i < lv->segments_len; ++i) {
			emit("\t\tsegments[%" FMTzu "]:",
				ARGzu(i));
			print_lvm2_segment(lv->segments[i]);
		}
	}
}

static void print_lvm2_physical_volume(struct lvm2_physical_volume *pv)
{
	emit("\t\tname: %s", pv->name->content);
	emit("\t\tid: %s", pv->id->content);
	emit("\t\tdevice: %s", pv->device->content);
	emit("\t\tstatus: 0x%X", pv->status);
	if(pv->flags_defined) {
		emit("\t\tflags: 0x%X", pv->flags);
	}
	if(pv->dev_size_defined) {
		emit("\t\tdev_size: %" FMTllu, ARGllu(pv->dev_size));
	}
	emit("\t\tpe_start: %" FMTllu, ARGllu(pv->pe_start));
	emit("\t\tpe_count: %" FMTllu, ARGllu(pv->pe_count));
}

static void print_lvm2_volume_group(struct lvm2_volume_group *vg)
{

	emit("\tid: %s", vg->id ? vg->id->content : "NULL");
	emit("\tseqno: %" FMTllu, ARGllu(vg->seqno));
	emit("\tstatus: 0x%" FMTllX, ARGllX(vg->status));
	emit("\tflags: 0x%" FMTllX, ARGllX(vg->flags));
	emit("\textent_size: %" FMTllu, ARGllu(vg->extent_size));
	emit("\tmax_lv: %" FMTllu, ARGllu(vg->max_lv));
	emit("\tmax_pv: %" FMTllu, ARGllu(vg->max_pv));
	emit("\tmetadata_copies: %" FMTllu, ARGllu(vg->metadata_copies));

	if(vg->physical_volumes_len) {
		size_t i;

		for(i = 0; i < vg->physical_volumes_len; ++i) {
			emit("\tphysical_volumes[%" FMTzu "]:",
				ARGzu(i));
			print_lvm2_physical_volume(vg->physical_volumes[i]);
		}
	}
	if(vg->logical_volumes_len) {
		size_t i;

		for(i = 0; i < vg->logical_volumes_len; ++i) {
			emit("\tlogical_volumes[%" FMTzu "]:",
				ARGzu(i));
			print_lvm2_logical_volume(vg->logical_volumes[i]);
		}
	}
}

static void print_lvm2_layout(struct lvm2_layout *layout)
{
	emit("vg_name: %s", layout->vg_name->content);
	emit("vg:");
	print_lvm2_volume_group(layout->vg);
	emit("contents: %s", layout->contents->content);
	emit("version: %" FMTllu, ARGllu(layout->version));
	emit("description: %s", layout->description->content);
	emit("creation_host: %s", layout->creation_host->content);
	emit("creation_time: %" FMTllu, ARGllu(layout->creation_time));
}

#undef emit

static int read_text_main(const char *const device_name)
{
	int ret = (EXIT_FAILURE);
	int fd;

	fd = open(device_name, O_RDWR);
	if(fd == -1) {
		LogError("Error while opening \"%s\": %d (%s)",
			device_name, errno, strerror(errno));
	}
	else {
		off_t tmp_file_size;

		tmp_file_size = lseek(fd, (off_t) 0, SEEK_END);
		if(tmp_file_size == -1) {
			LogError("Error while seeking to end of file: "
				"%d (%s)",
				errno, strerror(errno));
		}
		else if(tmp_file_size < 0 ||
			(uintmax_t) tmp_file_size > SIZE_MAX)
		{
			LogError("File size exceeds addressable memory.");
		}
		else {
			size_t file_size = (size_t) tmp_file_size;
			char *file_data;

			file_data = malloc(file_size);
			if(!file_data) {
				LogError("Error while allocating "
					"%" FMTzu " bytes of memory for "
					"temporary buffer: %d (%s)",
					ARGzu(file_size), errno,
					strerror(errno));
			}
			else {
				ssize_t res;

				res = pread(fd, file_data, file_size,
					(off_t) 0);
				if(res == -1) {
					LogError("Error while reading "
						"%" FMTzu " bytes into "
						"temporary buffer: %d (%s)",
						ARGzu(file_size), errno,
						strerror(errno));
				}
				else {
					struct lvm2_dom_section *result;
					if(lvm2_parse_text(file_data, file_size,
						&result))
					{
						int err;
						struct lvm2_layout *layout;
						if(!(err = lvm2_layout_create(
							result, &layout)))
						{
							lvm2_dom_section_destroy(
								&result,
								LVM2_TRUE);

							fprintf(stderr, "lvm2_"
								"layout_create "
								"returned "
								"successfully."
								"\n");
							print_lvm2_layout(
								layout);

							lvm2_layout_destroy(
								&layout);
						}
						else {
							lvm2_dom_section_destroy(
								&result,
								LVM2_TRUE);

							fprintf(stderr, "lvm2_"
								"layout_create "
								"returned "
								"with error: %d"
								"\n", err);
						}
					}
					else {
						fprintf(stderr,
							"lvm2_parse_text "
							"returned with error.");
					}
				}

				fprintf(stderr, "Number of outstanding "
					"allocations: %lld\n",
					lvm2_get_allocations());

				free(file_data);
			}
		}

		close(fd);
	}

	return ret;
}

static lvm2_bool volume_callback(
		void *const private_data __attribute__((unused)),
		const u64 device_size, const char *const volume_name,
		const u64 volume_start, const u64 volume_length)
{
	static lvm2_bool device_size_printed = LVM2_FALSE;

	if(!device_size_printed) {
		fprintf(stdout, "Device size: %" FMTllu "\n",
			ARGllu(device_size));
	}

	fprintf(stdout, "%s: [%" FMTllu "-%" FMTllu "]\n",
		volume_name, ARGllu(volume_start),
		ARGllu(volume_start + volume_length));

	return LVM2_TRUE;
}

static int read_device_main(const char *const device_name)
{
	int ret = (EXIT_FAILURE);
	int err;
	struct lvm2_device *dev = NULL;

	err = lvm2_unix_device_create(device_name, &dev);
	if(err) {
		LogError("Error while opening \"%s\": %d (%s)",
			device_name, err, strerror(err));
	}
	else {
		err = lvm2_parse_device(dev, &volume_callback, NULL);
		if(err) {
			LogError("Error while parsing LVM2 volume: %d (%s)",
				err, strerror(err));
		}
		else {
			ret = (EXIT_SUCCESS);
		}

		lvm2_unix_device_destroy(&dev);
	}

	return ret;
}

int main(int argc, char **argv)
{
	if(!lvm2_check_layout()) {
		fprintf(stderr, "Build error: Incorrect struct definitions.\n");
		exit(EXIT_FAILURE);
		return (EXIT_FAILURE);
	}

	if(argc != 2) {
		fprintf(stderr, "usage: %s <file>\n",
			argc ? argv[0] : "<null>");
		exit(EXIT_FAILURE);
		return (EXIT_FAILURE);
	}

	if(1)
		return read_device_main(argv[1]);
	else
		return read_text_main(argv[1]);
}
