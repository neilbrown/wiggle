/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2013 Neil Brown <neilb@suse.de>
 * Copyright (C) 2014-2020 Neil Brown <neil@brown.name>
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
 *    along with this program.
 *
 *    Author: Neil Brown
 *    Email: <neil@brown.name>
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
		wiggle_die("memory allocation");

	list[0].body = c;
	c  += list[0].len;
	list[0].len = len;
	for (i = 1; i < cnt; i++) {
		memcpy(c, list[i].body, list[i].len);
		c += list[i].len;
		list[i].len = 0;
		free(list[i].body);
	}
	c[0] = 0;
}

static struct stream wiggle_load_regular(int fd)
{
	struct stat stb;
	struct stream s;
	fstat(fd, &stb);

	s.len = stb.st_size;
	s.body = wiggle_xmalloc(s.len+1);
	if (read(fd, s.body, s.len) != s.len)
		wiggle_die("file read");

	s.body[s.len] = 0;
	return s;
}

static struct stream wiggle_load_other(int fd)
{

	struct stream list[10];
	int i = 0;

	while (1) {
		list[i].body = wiggle_xmalloc(8192);
		list[i].len = read(fd, list[i].body, 8192);
		if (list[i].len < 0)
			wiggle_die("file read");
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

struct stream wiggle_load_segment(FILE *f,
			   unsigned int start, unsigned int end)
{
	struct stream s;
	s.len = end - start;
	s.body = wiggle_xmalloc(s.len+1);
	fseek(f, start, 0);
	if (fread(s.body, 1, s.len, f) != (size_t)s.len)
		wiggle_die("file read");
	/* ensure string is 'nul' terminated - for sscanf */
	s.body[s.len] = 0;
	return s;
}

struct stream wiggle_load_file(char *name)
{
	struct stream s;
	struct stat stb;
	int fd;
	int start, end;
	int prefix_len = 0;

	s.body = NULL;
	s.len = 0;
	if (sscanf(name, "_wiggle_:%d:%d:%n", &start, &end,
		   &prefix_len) >= 2 && prefix_len > 0) {
		FILE *f = fopen(name + prefix_len, "r");
		if (f) {
			s = wiggle_load_segment(f, start, end);
			fclose(f);
		} else {
			s.body = NULL;
			s.len = 0;
		}
	} else {
		if (strcmp(name, "-") == 0)
			fd = 0;
		else {
			fd = open(name, O_RDONLY);
			if (fd < 0)
				return s;
		}
		wiggle_check_dir(name, fd);
		if (fstat(fd, &stb) == 0) {

			if (S_ISREG(stb.st_mode))
				s = wiggle_load_regular(fd);
			else
				s = wiggle_load_other(fd);
		}
		close(fd);
	}
	return s;
}

