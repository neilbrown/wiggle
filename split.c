/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003-2013 Neil Brown <neilb@suse.de>
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
 *    Email: <neilb@suse.de>
 */

/*
 * Split a stream into words or lines
 *
 * A word is one of:
 *    string of [A-Za-z0-9_]
 *    or string of [ \t]
 *    or single char (i.e. punctuation and newlines).
 *
 * A line is any string that ends with \n
 *
 * As a special case to allow proper aligning of multiple chunks
 * in a patch, a word starting \0 will include 20+ chars with a newline
 * second from the end.
 *
 * We make two passes through the stream.
 * Firstly we count the number of item so an array can be allocated,
 * then we store start and length of each item in the array
 *
 */

#include	"wiggle.h"
#include	<stdlib.h>
#include	<ctype.h>
#include	<stdlib.h>

#include "ccan/hash/hash.h"

static int split_internal(char *start, char *end, int type,
			  struct elmnt *list)
{
	int cnt = 0;

	while (start < end) {
		char *cp = start;
		char *cp2;
		int prefix = 0;

		if ((type & ByWord) && (type & IgnoreBlanks))
			while (cp < end &&
			       (*cp == ' ' || *cp == '\t')) {
				prefix++;
				cp++;
			}
		start = cp;

		if (*cp == '\0' && cp+19 < end) {
			/* special word */
			cp += 19;
			cp += strlen(cp) + 1;
		} else
			switch (type & ByMask) {
			case ByLine:
				while (cp < end && *cp != '\n')
					cp++;
				if (cp < end)
					cp++;
				break;
			case ByWord:
				if (*cp == ' ' || *cp == '\t') {
					do
						cp++;
					while (cp < end
					       && (*cp == ' '
						   || *cp == '\t'));
				} else if ((type & WholeWord) ||
					   isalnum(*cp) || *cp == '_') {
					do
						cp++;
					while (cp < end
					       && (((type & WholeWord)
						    && *cp != ' ' && *cp != '\t'
						    && *cp != '\n')
						   || isalnum(*cp)
						   || *cp == '_'));
				} else
					cp++;
				break;
			}
		cp2 = cp;
		if ((type & ByWord) && (type & IgnoreBlanks) &&
		    *start && *start != '\n')
			while (cp2 < end &&
			       (*cp2 == ' ' || *cp2 == '\t' || *cp2 == '\n')) {
				cp2++;
				if (cp2[-1] == '\n')
					break;
			}
		if (list) {
			list->start = start;
			list->len = cp-start;
			list->plen = cp2-start;
			list->prefix = prefix;
			if (*start)
				list->hash = hash(start, list->len, 0);
			else
				list->hash = atoi(start+1);
			list++;
		}
		cnt++;
		start = cp2;
	}
	return cnt;
}

struct file split_stream(struct stream s, int type)
{
	int cnt;
	struct file f;
	char *c, *end;

	if (!s.body) {
		f.list = NULL;
		f.elcnt = 0;
		return f;
	}
	end = s.body+s.len;
	c = s.body;

	cnt = split_internal(c, end, type, NULL);
	f.list = xmalloc(cnt*sizeof(struct elmnt));

	f.elcnt = split_internal(c, end, type, f.list);
	return f;
}
