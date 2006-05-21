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

#include <stdio.h>
#include <memory.h>
#include	<getopt.h>

struct stream {
	char *body;
	int len;
};


struct elmnt {
	int hash;
	char *start;
	int len;
};

static  inline int match(struct elmnt *a, struct elmnt *b)
{
	return
		a->hash == b->hash &&
		a->len == b->len &&
		strncmp(a->start, b->start, a->len)==0;
}

static inline int ends_line(struct elmnt e)
{
	return e.len &&  e.start[e.len-1] == '\n';
}

struct csl {
	int a,b;
	int len;
};

struct file {
	struct elmnt *list;
	int elcnt;
};

extern struct stream load_file(char *name);
extern int split_patch(struct stream, struct stream*, struct stream*);
extern int split_merge(struct stream, struct stream*, struct stream*, struct stream*);
extern struct file split_stream(struct stream s, int type, int reverse);
extern struct csl *pdiff(struct file a, struct file b, int chunks);
extern struct csl *diff(struct file a, struct file b);
extern struct csl *diff_partial(struct file a, struct file b, 
				int alo, int ahi, int blo, int bhi);
extern struct csl *worddiff(struct stream f1, struct stream f2,
			    struct file *fl1p, struct file *fl2p);

struct ci { int conflicts; int ignored; };
extern struct ci print_merge(FILE *out, struct file *a, struct file *b, struct file *c,
		       struct csl *c1, struct csl *c2,
		       int words);
extern void printword(FILE *f, struct elmnt e);

extern void die(void);

extern char Version[];
extern char short_options[];
extern struct option long_options[];
extern char Usage[];
extern char Help[];
extern char HelpExtract[];
extern char HelpDiff[];
extern char HelpMerge[];


#define	ByLine	0
#define	ByWord	1
#define	ApproxWord 2
