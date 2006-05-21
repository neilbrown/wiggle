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
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    Author: Neil Brown
 *    Email: <neilb@cse.unsw.edu.au>
 *    Paper: Neil Brown
 *           School of Computer Science and Engineering
 *           The University of New South Wales
 *           Sydney, 2052
 *           Australia
 */

/*
 * split patch or merge files.
 *
 */

#include	"wiggle.h"
#include	<malloc.h>
#include	<stdlib.h>

void skip_eol(char **cp, char *end)
{
	char *c = *cp;
	while (c < end && *c != '\n')
		c++;
	if (c < end) c++;
	*cp = c;
}

void copyline(struct stream *s, char **cp, char *end)
{
	char *from = *cp;
	char *to = s->body+s->len;

	while (from < end && *from != '\n')
		*to++ = *from++;
	if (from < end)
		*to++ = *from++;
	s->len = to-s->body;
	*cp = from;
}

int split_patch(struct stream f, struct stream *f1, struct stream *f2)
{
	struct stream r1, r2;
	int chunks=0;
	char *cp, *end;
	int state = 0;
	int acnt=0, bcnt=0;
	int a,b,c,d;
	int lineno = 0;

	f1->body = f2->body = NULL;

	r1.body = malloc(f.len);
	r2.body = malloc(f.len);
	if (!r1.body || !r2.body)
		die();

	r1.len = r2.len = 0;

	cp = f.body;
	end = f.body+f.len;
	while (cp < end) {
		/* state:
		 *   0   not in a patch
		 *   1   first half of context
		 *   2   second half of context
		 *   3   unified
		 */
		lineno++;
		switch(state) {
		case 0:
			if (sscanf(cp, "@@ -%d,%d +%d,%d @@", &a, &b, &c, &d)==4) {
				acnt = b;
				bcnt = d;
				state = 3;
			} else if (sscanf(cp, "*** %d,%d ****", &a, &b)==2) {
				acnt = b-a+1;
				state = 1;
			} else if (sscanf(cp, "--- %d,%d ----", &c, &d)==2) {
				bcnt = d-c+1;
				state = 2;
			}
			skip_eol(&cp, end);
			if (state==1 || state == 3) {
				char buf[20];
				buf[0] = 0;
				chunks++;
				sprintf(buf+1, "%5d %5d %5d\n", chunks, a, acnt);
				memcpy(r1.body+r1.len, buf, 19);
				r1.len += 19;
			}
			if (state==2 || state == 3) {
				char buf[20];
				buf[0] = 0;
				sprintf(buf+1, "%5d %5d %5d\n", chunks, c, bcnt);
				memcpy(r2.body+r2.len, buf, 19);
				r2.len += 19;
			}
			break;
		case 1:
			if ((*cp == ' ' || *cp=='!' || *cp == '-' || *cp == '+')
			    && cp[1] == ' ') {
				cp+=2;
				copyline(&r1, &cp, end);
				acnt--;
				if (acnt == 0)
					state = 0;
			} else {
				fprintf(stderr, "wiggle: bad context patch at line %d\n", lineno);
				return 0;
			}
			break;
		case 2:
			if ((*cp == ' ' || *cp=='!' || *cp == '-' || *cp == '+')
			    && cp[1] == ' ') {
				cp+= 2;
				copyline(&r2, &cp, end);
				bcnt--;
				if (bcnt == 0)
					state = 0;
			} else {
				fprintf(stderr, "wiggle: bad context patch/2 at line %d\n", lineno);
				return 0;
			}
			break;
		case 3:
			if (*cp == ' ') {
				char *cp2;
				cp++;
				cp2 = cp;
				copyline(&r1, &cp, end);
				copyline(&r2, &cp2, end);
				acnt--; bcnt--;
			} else if (*cp == '-') {
				cp++;
				copyline(&r1, &cp, end);
				acnt--;
			} else if (*cp == '+') {
				cp++;
				copyline(&r2, &cp, end);
				bcnt--;
			} else {
				fprintf(stderr, "wiggle: bad unified patch at line %d\n", lineno);
				return 0;
			}
			if (acnt <= 0 && bcnt <= 0)
				state = 0;
			break;
		}
	}
	if (r1.len > f.len || r2.len > f.len)
		abort();
	*f1 = r1;
	*f2 = r2;
	return chunks;
}

/*
 * extract parts of a "diff3 -m" or "wiggle -m" output
 */
int split_merge(struct stream f, struct stream *f1, struct stream *f2, struct stream *f3)
{
	int lineno;
	int state = 0;
	char *cp, *end;
	struct stream r1,r2,r3;
	f1->body = f2->body = f2->body = NULL;

	r1.body = malloc(f.len);
	r2.body = malloc(f.len);
	r3.body = malloc(f.len);
	if (!r1.body || !r2.body || !r3.body)
		die();

	r1.len = r2.len = r3.len = 0;

	cp = f.body;
	end = f.body+f.len;
	while (cp < end) {
		/* state:
		 *  0 not in conflict
		 *  1 in file 1 of conflict
		 *  2 in file 2 of conflict
		 *  3 in file 3 of conflict
		 */
		int len = end-cp;
		lineno++;
		switch(state) {
		case 0:
			if (len>8 &&
			    strncmp(cp, "<<<<<<<", 7)==0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 1;
				skip_eol(&cp, end);
			} else {
				char *cp2= cp;
				copyline(&r1, &cp2, end);
				cp2 = cp;
				copyline(&r2, &cp2, end);
				copyline(&r3, &cp, end);
			}
			break;
		case 1:
			if (len>8 &&
			    strncmp(cp, "|||||||", 7)==0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 2;
				skip_eol(&cp, end);
			} else
				copyline(&r1, &cp, end);
			break;
		case 2:
			if (len>8 &&
			    strncmp(cp, "=======", 7)==0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 3;
				skip_eol(&cp, end);
			} else
				copyline(&r2, &cp, end);
			break;
		case 3:
			if (len>8 &&
			    strncmp(cp, ">>>>>>>", 7)==0 &&
			    (cp[7] == ' ' || cp[7] == '\n')
				) {
				state = 0;
				skip_eol(&cp, end);
			} else
				copyline(&r3, &cp, end);
			break;
		}
	}
	*f1 = r1;
	*f2 = r2;
	*f3 = r3;
	return state == 0;
}
