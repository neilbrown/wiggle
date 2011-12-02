/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *    Author: Neil Brown
 *    Email: <neilb@suse.de>
 */

/*
 * read in files
 *
 * Files are read in whole and stored in a
 * struct stream {char*, len}
 *
 *
 * loading the file "-" reads from stdin which might require
 * reading into several buffers
 */

#include	"wiggle.h"
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<stdlib.h>

static void join_streams(struct stream list[], int cnt)
{
	/* join all the streams in the list (upto body=NULL)
	 * into one by re-allocing list[0].body and copying
	 */
	int len = 0;
	int i;
	char *c;

	for (i = 0; i < cnt ; i++)
		len += list[i].len;

	c = realloc(list[0].body, len+1);
	if (c == NULL)
		die();

	list[0].body = c;
	c  += list[0].len;
	list[0].len = len;
	for (i = 1; i < cnt; i++) {
		memcpy(c, list[i].body, list[i].len);
		c += list[i].len;
		list[i].len = 0;
	}
	c[0] = 0;
}

static struct stream load_regular(int fd)
{
	struct stat stb;
	struct stream s;
	fstat(fd, &stb);

	s.len = stb.st_size;
	s.body = malloc(s.len+1);
	if (s.body) {
		if (read(fd, s.body, s.len) != s.len) {
			free(s.body);
			s.body = NULL;
		}
	} else
		die();
	s.body[s.len] = 0;
	return s;
}

static struct stream load_other(int fd)
{

	struct stream list[10];
	int i = 0;

	while (1) {
		list[i].body = malloc(8192);
		if (!list[i].body)
			die();
		list[i].len = read(fd, list[i].body, 8192);
		if (list[i].len < 0)
			die();
		if (list[i].len == 0)
			break;
		i++;
		if (i == 10) {
			join_streams(list, i);
			i = 1;
		}
	}
	join_streams(list, i);
	return list[0];
}

struct stream load_file(char *name)
{
	struct stream s;
	struct stat stb;
	int fd;

	s.body = NULL;
	s.len = 0;
	if (strcmp(name, "-") == 0)
		fd = 0;
	else {
		fd = open(name, O_RDONLY);
		if (fd < 0)
			return s;
	}

	if (fstat(fd, &stb) == 0) {

		if (S_ISREG(stb.st_mode))
			s = load_regular(fd);
		else
			s = load_other(fd);
	}
	close(fd);
	return s;
}

