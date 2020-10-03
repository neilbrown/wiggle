/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2010-2013 Neil Brown <neilb@suse.de>
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

#include	"wiggle.h"
#include	<unistd.h>
#include	<stdlib.h>
#include	<sys/stat.h>

char *Cmd = "wiggle";

int do_trace = 0;

void *xmalloc(int size)
{
	void *rv = malloc(size);
	if (size && !rv) {
		char *msg = "Failed to allocate memory - aborting\n";
		write(2, msg, strlen(msg));
		exit(3);
	}
	return rv;
}

void printword(FILE *f, struct elmnt e)
{
	if (e.start[0])
		fprintf(f, "%.*s", e.plen + e.prefix,
			e.start - e.prefix);
	else {
		int a, b, c;
		sscanf(e.start+1, "%d %d %d", &a, &b, &c);
		fprintf(f, "*** %d,%d **** %d%s", b, c, a, e.start+18);
	}
}

void die(char *reason)
{
	fprintf(stderr, "%s: fatal error: %s failure\n", Cmd, reason);
	exit(3);
}

void check_dir(char *name, int fd)
{
	struct stat st;
	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "%s: fatal: %s is strange\n", Cmd, name);
		exit(3);
	}
	if (S_ISDIR(st.st_mode)) {
		fprintf(stderr, "%s: %s is a directory\n", Cmd, name);
		exit(3);
	}
}

