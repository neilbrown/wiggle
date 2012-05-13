/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003-2012 Neil Brown <neilb@suse.de>
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
 * Parse a patch file to find the names of the different
 * files to patch and record which parts of the patch
 * file applies to which target file.
 */

#include	"wiggle.h"
#include	<unistd.h>
#include	<fcntl.h>

/* determine how much we need to stripe of the front of
 * paths to find them from current directory.  This is
 * used to guess correct '-p' value.
 */
static int get_strip(char *file)
{
	int fd;
	int strip = 0;

	while (file && *file) {
		fd  = open(file, O_RDONLY);
		if (fd >= 0) {
			close(fd);
			return strip;
		}
		strip++;
		file = strchr(file, '/');
		if (file)
			while (*file == '/')
				file++;
	}
	return -1;

}

int set_prefix(struct plist *pl, int n, int strip)
{
	int i;
	for (i = 0; i < 4 && i < n  && strip < 0; i++)
		strip = get_strip(pl[i].file);

	if (strip < 0) {
		fprintf(stderr, "%s: Cannot find files to patch: please specify --strip\n",
			Cmd);
		return 0;
	}
	for (i = 0; i < n; i++) {
		char *p = pl[i].file;
		int j;
		for (j = 0; j < strip; j++) {
			if (p)
				p = strchr(p, '/');
			while (p && *p == '/')
				p++;
		}
		if (p == NULL) {
			fprintf(stderr, "%s: cannot strip %d segments from %s\n",
				Cmd, strip, pl[i].file);
			return 0;
		}
		pl[i].file = p;
	}
	return 1;
}

static int pl_cmp(const void *av, const void *bv)
{
	const struct plist *a = av;
	const struct plist *b = bv;
	return strcmp(a->file, b->file);
}

static int common_depth(char *a, char *b)
{
	/* find number of path segments that these two have
	 * in common
	 */
	int depth = 0;
	while (1) {
		char *c;
		int al, bl;
		c = strchr(a, '/');
		if (c)
			al = c-a;
		else
			al = strlen(a);
		c = strchr(b, '/');
		if (c)
			bl = c-b;
		else
			bl = strlen(b);
		if (al == 0 || al != bl || strncmp(a, b, al) != 0)
			return depth;
		a += al;
		while (*a == '/')
			a++;
		b += bl;
		while (*b == '/')
			b++;

		depth++;
	}
}

static struct plist *patch_add_file(struct plist *pl, int *np, char *file,
				    unsigned int start, unsigned int end)
{
	/* size of pl is 0, 16, n^2 */
	int n = *np;
	int asize;

	while (*file == '/')
		/* leading '/' are bad... */
		file++;

	if (n == 0)
		asize = 0;
	else if (n <= 16)
		asize = 16;
	else if ((n&(n-1)) == 0)
		asize = n;
	else
		asize = n+1; /* not accurate, but not too large */
	if (asize <= n) {
		/* need to extend array */
		struct plist *npl;
		if (asize < 16)
			asize = 16;
		else
			asize += asize;
		npl = realloc(pl, asize * sizeof(struct plist));
		if (!npl) {
			fprintf(stderr, "realloc failed - skipping %s\n", file);
			return pl;
		}
		pl = npl;
	}
	pl[n].file = file;
	pl[n].start = start;
	pl[n].end = end;
	pl[n].last = pl[n].next = pl[n].prev = pl[n].parent = -1;
	pl[n].chunks = pl[n].wiggles = 0; pl[n].conflicts = 100;
	pl[n].open = 1;
	pl[n].calced = 0;
	pl[n].is_merge = 0;
	*np = n+1;
	return pl;
}

static struct plist *add_dir(struct plist *pl, int *np, char *file, char *curr)
{
	/* any parent of file that is not a parent of curr
	 * needs to be added to pl
	 */
	int d = common_depth(file, curr);
	char *buf = curr;
	while (d) {
		char *c = strchr(file, '/');
		int l;
		if (c)
			l = c-file;
		else
			l = strlen(file);
		file += l;
		curr += l;
		while (*file == '/')
			file++;
		while (*curr == '/')
			curr++;
		d--;
	}
	while (*file) {
		if (curr > buf && curr[-1] != '/')
			*curr++ = '/';
		while (*file && *file != '/')
			*curr++ = *file++;
		while (*file == '/')
			file++;
		*curr = '\0';
		if (*file)
			pl = patch_add_file(pl, np, strdup(buf),
					    0, 0);
	}
	return pl;
}

struct plist *sort_patches(struct plist *pl, int *np)
{
	/* sort the patches, add directory names, and re-sort */
	char curr[1024];
	char *prev;
	int parents[100];
	int prevnode[100];
	int i, n;
	qsort(pl, *np, sizeof(struct plist), pl_cmp);
	curr[0] = 0;
	n = *np;
	for (i = 0; i < n; i++)
		pl = add_dir(pl, np, pl[i].file, curr);

	qsort(pl, *np, sizeof(struct plist), pl_cmp);

	/* array is now stable, so set up parent pointers */
	n = *np;
	curr[0] = 0;
	prevnode[0] = -1;
	prev = "";
	for (i = 0; i < n; i++) {
		int d = common_depth(prev, pl[i].file);
		if (d == 0)
			pl[i].parent = -1;
		else {
			pl[i].parent = parents[d-1];
			pl[pl[i].parent].last = i;
		}
		pl[i].prev = prevnode[d];
		if (pl[i].prev > -1)
			pl[pl[i].prev].next = i;
		prev = pl[i].file;
		parents[d] = i;
		prevnode[d] = i;
		prevnode[d+1] = -1;
	}
	return pl;
}

struct plist *parse_patch(FILE *f, FILE *of, int *np)
{
	/* read a multi-file patch from 'f' and record relevant
	 * details in a plist.
	 * if 'of' >= 0, fd might not be seekable so we write
	 * to 'of' and use lseek on 'of' to determine position
	 */
	struct plist *plist = NULL;

	*np = 0;
	while (!feof(f)) {
		/* first, find the start of a patch: "\n+++ "
		 * grab the file name and scan to the end of a line
		 */
		char *target = "\n+++ ";
		char *target2 = "\n--- ";
		char *pos = target;
		int c;
		char name[1024];
		unsigned start, end;

		while (*pos && (c = fgetc(f)) != EOF) {
			if (of)
				fputc(c, of);
			if (c == *pos)
				pos++;
			else
				pos = target;
		}
		if (c == EOF)
			break;
		assert(c == ' ');
		/* now read a file name */
		pos = name;
		while ((c = fgetc(f)) != EOF
		       && c != '\t' && c != '\n' && c != ' ' &&
		       pos - name < 1023) {
			*pos++ = c;
			if (of)
				fputc(c, of);
		}
		*pos = 0;
		if (c == EOF)
			break;
		if (of)
			fputc(c, of);
		while (c != '\n' && (c = fgetc(f)) != EOF)
			if (of)
				fputc(c, of);

		start = ftell(of ?: f);

		if (c == EOF)
			break;

		/* now skip to end - "\n--- " */
		pos = target2+1;

		while (*pos && (c = fgetc(f)) != EOF) {
			if (of)
				fputc(c, of);
			if (c == *pos)
				pos++;
			else
				pos = target2;
		}
		end = ftell(of ?: f);
		if (pos > target2)
			end -= (pos - target2) - 1;
		plist = patch_add_file(plist, np,
				       strdup(name), start, end);
	}
	return plist;
}
