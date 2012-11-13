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

#include	<stdio.h>
#include	<string.h>
#include	<memory.h>
#include	<getopt.h>
#include	<stdlib.h>

static inline void assert(int a)
{
	if (!a)
		abort();
}

struct stream {
	char *body;
	int len;
};

/* an 'elmnt' is a word or a line from the file.
 * 'start' points into a 'body' in a stream.
 * When a stream is made of 'diff' hunks, there is a special
 * elmnt at the start of each hunk which starts with '\0' and
 * records the line offsets of the hunk.  These are 20 bytes long.
 * "\0\d{5} \d{5} \d{5}\n\0"
 * The 3 numbers are: chunk number, starting line, number if lines.
 * An element with len==0 marks EOF.
 */
struct elmnt {
	char *start;
	int hash;
	int len;
};

static  inline int match(struct elmnt *a, struct elmnt *b)
{
	return
		a->hash == b->hash &&
		a->len == b->len &&
		strncmp(a->start, b->start, a->len) == 0;
}

/* end-of-line is important for narrowing conflicts.
 * In line mode, every element is a line and 'ends a line'
 * In word mode, the newline element and the diff-hunk element
 * end a line.
 */
static inline int ends_line(struct elmnt e)
{
	if (e.len == 20 && e.start[0] == 0)
		return 1;
	return e.len &&  e.start[e.len-1] == '\n';
}

static inline int ends_mline(struct elmnt e)
{
	return e.len &&  (e.start[0] == '\n' || e.start[0] == 0);
}

struct csl {
	int a, b;
	int len;
};

struct file {
	struct elmnt *list;
	int elcnt;
};

/* The result of a merger is a series of text sections.
 * Each section may occur in one or more of the three stream,
 * and may be different in different stream (e.g. for changed text)
 * or the same.
 * When a conflict occurs we need to treat some surrounding
 * sections as being involved in that conflict.  For
 * line-based merging, all surrounding sections until an Unchanged
 * section are part of the conflict - the Unchanged isn't.
 * For word-based merging, we need to find Unchanged sections
 * that include a newline.  Further, text within the unchanged
 * section upto the newline (in whichever direction) is treated
 * as part of the whole conflict.
 * Actually... it is possibly for a 'Changed' section to bound
 * a conflict as it indicates a successful match of A and B.
 * For line-wise merges, any Changed or Unchanged section bounds a conflict
 * For word-wise merges, any Changed or Unchanged section that matches
 * a newline, or immediately follows a newline (in all files) can bound
 * a conflict.
 */
struct merge {
	enum mergetype {
		End, Unmatched, Unchanged, Extraneous,
		Changed, Conflict, AlreadyApplied,
	} type;
	int a, b, c; /* start of ranges */
	int al, bl, cl; /* length of ranges */
	int c1, c2; /* this or next common-sequence */
	int in_conflict;
	int conflict_ignored;
	int lo, hi; /* region of a Changed or Unchanged that is not involved
		    * in a conflict.
		    * These are distances from start of the "before" section,
		    * not indexes into any file.
		    */

};

/* plist stores a list of patched files in an array
 * Each entry identifies a file, the range of the
 * original patch which applies to this file, some
 * statistics concerning how many conflicts etc, and
 * some linkage information so the list can be viewed
 * as a directory-tree.
 */
struct plist {
	char *file;
	unsigned int start, end;
	int parent;
	int next, prev, last;
	int open;
	int chunks, wiggles, conflicts;
	int calced;
	int is_merge;
	char *before, *after;
};

extern struct plist *sort_patches(struct plist *pl, int *np);
extern void plist_free(struct plist *pl, int num);
extern struct plist *parse_patch(FILE *f, FILE *of, int *np);
extern struct stream load_segment(FILE *f, unsigned int start,
				  unsigned int end);
extern int set_prefix(struct plist *pl, int n, int strip);
extern struct stream load_file(char *name);
extern int split_patch(struct stream, struct stream*, struct stream*);
extern int split_merge(struct stream, struct stream*, struct stream*,
		       struct stream*);
extern struct file split_stream(struct stream s, int type);
extern struct csl *pdiff(struct file a, struct file b, int chunks);
extern struct csl *diff(struct file a, struct file b);
extern struct csl *diff_partial(struct file a, struct file b,
				int alo, int ahi, int blo, int bhi);
extern struct csl *worddiff(struct stream f1, struct stream f2,
			    struct file *fl1p, struct file *fl2p);

struct ci {
	int conflicts, wiggles, ignored;
	struct merge *merger;
};
extern struct ci print_merge2(FILE *out,
			      struct file *a, struct file *b, struct file *c,
			      struct csl *c1, struct csl *c2,
			      int words, int ignore_already, int show_wiggles);
extern void printword(FILE *f, struct elmnt e);

extern int isolate_conflicts(struct file af, struct file bf, struct file cf,
			     struct csl *csl1, struct csl *csl2, int words,
			     struct merge *m, int show_wiggles);
extern struct ci make_merger(struct file a, struct file b, struct file c,
			     struct csl *c1, struct csl *c2, int words,
			     int ignore_already, int show_wiggles);

extern void die(void);
extern void *xmalloc(int len);
extern int do_trace;

extern int vpatch(int argc, char *argv[], int patch, int strip,
		  int reverse, int replace);

extern char *Cmd;
extern char Version[];
extern char short_options[];
extern struct option long_options[];
extern char Usage[];
extern char Help[];
extern char HelpExtract[];
extern char HelpDiff[];
extern char HelpMerge[];
extern char HelpBrowse[];

extern void cleanlist(struct file a, struct file b, struct csl *list);

enum {
	ByLine,
	ByWord,
};
