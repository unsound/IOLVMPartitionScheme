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

int main(int argc, char **argv) {
	int ret = (EXIT_FAILURE);
	int fd;

	if(argc != 2) {
		fprintf(stderr, "usage: %s <file>\n",
			argc ? argv[0] : "<null>");
		exit(EXIT_FAILURE);
		return (EXIT_FAILURE);
	}

	fd = open(argv[1], O_RDWR);
	if(fd == -1) {
		LogError("Error while opening \"%s\": %d (%s)",
			argv[1], errno, strerror(errno));
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
						lvm2_dom_section_destroy(
							&result, LVM2_TRUE);
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
