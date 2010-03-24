/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2005 Neil Brown <neilb@cse.unsw.edu.au>
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
 *    Email: <neilb@suse.de>
 */

/*
 * vpatch - visual front end for wiggle
 *
 * "files" display, lists all files with statistics
 *    - can hide various lines including subdirectories
 *      and files without wiggles or conflicts
 * "diff" display shows merged file with different parts
 *      in different colours
 *    - untouched are pale  A_DIM
 *    - matched/remaining are regular A_NORMAL
 *    - matched/removed are red/underlined A_UNDERLINE
 *    - unmatched in file are A_STANDOUT
 *    - unmatched in patch are A_STANDOUT|A_UNDERLINE ???
 *    - inserted are inverse/green ?? A_REVERSE
 *
 *  The window can be split horiz or vert and two different
 *  views displayed.  They will have different parts missing
 *
 *  So a display of NORMAL, underline, standout|underline reverse
 *   should show a normal patch.
 *
 */

#include "wiggle.h"
#include <malloc.h>
#include <string.h>
#include <curses.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>

#define assert(x) do { if (!(x)) abort(); } while (0)

struct plist {
	char *file;
	unsigned int start, end;
	int parent;
	int next, prev, last;
	int open;
	int chunks, wiggles, conflicts;
	int calced;
};

struct plist *patch_add_file(struct plist *pl, int *np, char *file,
	       unsigned int start, unsigned int end)
{
	/* size of pl is 0, 16, n^2 */
	int n = *np;
	int asize;

	while (*file == '/') file++; /* leading '/' are bad... */

/*	printf("adding %s at %d: %u %u\n", file, n, start, end); */
	if (n==0) asize = 0;
	else if (n<=16) asize = 16;
	else if ((n&(n-1))==0) asize = n;
	else asize = n+1; /* not accurate, but not too large */
	if (asize <= n) {
		/* need to extend array */
		struct plist *npl;
		if (asize < 16) asize = 16;
		else asize += asize;
		npl = realloc(pl, asize * sizeof(struct plist));
		if (!npl) {
			fprintf(stderr, "malloc failed - skipping %s\n", file);
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
	*np = n+1;
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

	while (!feof(f)) {
		/* first, find the start of a patch: "\n+++ "
		 * grab the file name and scan to the end of a line
		 */
		char *target="\n+++ ";
		char *target2="\n--- ";
		char *pos = target;
		int c;
		char name[1024];
		unsigned start, end;

		while (*pos && (c=fgetc(f)) != EOF ) {
			if (of) fputc(c, of);
			if (c == *pos)
				pos++;
			else pos = target;
		}
		if (c == EOF)
			break;
		assert(c == ' ');
		/* now read a file name */
		pos = name;
		while ((c=fgetc(f)) != EOF && c != '\t' && c != '\n' && c != ' ' &&
		       pos - name < 1023) {
			*pos++ = c;
			if (of) fputc(c, of);
		}
		*pos = 0;
		if (c == EOF)
			break;
		if (of) fputc(c, of);
		while (c != '\n' && (c=fgetc(f)) != EOF) {
			if (of) fputc(c, of);
		}
		start = of ? ftell(of) : ftell(f);

		if (c == EOF) break;

		/* now skip to end - "\n--- " */
		pos = target2+1;

		while (*pos && (c=fgetc(f)) != EOF) {
			if (of) fputc(c, of);
			if (c == *pos)
				pos++;
			else pos = target2;
		}
		end = of ? ftell(of) : ftell(f);
		if (pos > target2)
			end -= (pos - target2) - 1;
		plist = patch_add_file(plist, np,
				       strdup(name), start, end);
	}
	return plist;
}
#if 0
void die()
{
	fprintf(stderr,"vpatch: fatal error\n");
	abort();
	exit(3);
}
#endif

static struct stream load_segment(FILE *f,
				  unsigned int start, unsigned int end)
{
	struct stream s;
	s.len = end - start;
	s.body = malloc(s.len);
	if (s.body) {
		fseek(f, start, 0);
		if (fread(s.body, 1, s.len, f) != s.len) {
			free(s.body);
			s.body = NULL;
		}
	} else
		die();
	return s;
}


void catch(int sig)
{
	if (sig == SIGINT) {
		signal(sig, catch);
		return;
	}
	nocbreak();nl();endwin();
	printf("Died on signal %d\n", sig);
	exit(2);
}

int pl_cmp(const void *av, const void *bv)
{
	const struct plist *a = av;
	const struct plist *b = bv;
	return strcmp(a->file, b->file);
}

int common_depth(char *a, char *b)
{
	/* find number of patch segments that these two have
	 * in common
	 */
	int depth = 0;
	while(1) {
		char *c;
		int al, bl;
		c = strchr(a, '/');
		if (c) al = c-a; else al = strlen(a);
		c = strchr(b, '/');
		if (c) bl = c-b; else bl = strlen(b);
		if (al == 0 || al != bl || strncmp(a,b,al) != 0)
			return depth;
		a+= al;
		while (*a=='/') a++;
		b+= bl;
		while(*b=='/') b++;

		depth++;
	}
}

struct plist *add_dir(struct plist *pl, int *np, char *file, char *curr)
{
	/* any parent of file that is not a parent of curr
	 * needs to be added to pl
	 */
	int d = common_depth(file, curr);
	char *buf = curr;
	while (d) {
		char *c = strchr(file, '/');
		int l;
		if (c) l = c-file; else l = strlen(file);
		file += l;
		curr += l;
		while (*file == '/') file++;
		while (*curr == '/') curr++;
		d--;
	}
	while (*file) {
		if (curr > buf && curr[-1] != '/')
			*curr++ = '/';
		while (*file && *file != '/')
			*curr++ = *file++;
		while (*file == '/') file++;
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
	for (i=0; i<n; i++)
		pl = add_dir(pl, np, pl[i].file, curr);

	qsort(pl, *np, sizeof(struct plist), pl_cmp);

	/* array is now stable, so set up parent pointers */
	n = *np;
	curr[0] = 0;
	prevnode[0] = -1;
	prev = "";
	for (i=0; i<n; i++) {
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

int get_strip(char *file)
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
			while(*file == '/')
				file++;
	}
	return -1;

}
int set_prefix(struct plist *pl, int n, int strip)
{
	int i;
	for(i=0; i<4 && i<n  && strip < 0; i++)
		strip = get_strip(pl[i].file);

	if (strip < 0) {
		fprintf(stderr, "%s: Cannot file files to patch: please specify --strip\n",
			Cmd);
		return 0;
	}
	for (i=0; i<n; i++) {
		char *p = pl[i].file;
		int j;
		for (j=0; j<strip; j++) {
			if (p) p = strchr(p,'/');
			while (p && *p == '/') p++;
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


int get_prev(int pos, struct plist *pl, int n)
{
	if (pos == -1) return pos;
	if (pl[pos].prev == -1)
		return pl[pos].parent;
	pos = pl[pos].prev;
	while (pl[pos].open &&
	       pl[pos].last >= 0)
		pos = pl[pos].last;
	return pos;
}

int get_next(int pos, struct plist *pl, int n)
{
	if (pos == -1) return pos;
	if (pl[pos].open) {
		if (pos +1 < n)
			return pos+1;
		else
			return -1;
	}
	while (pos >= 0 && pl[pos].next == -1)
		pos = pl[pos].parent;
	if (pos >= 0)
		pos = pl[pos].next;
	return pos;
}

/* global attributes */
int a_delete, a_added, a_common, a_sep, a_void, a_unmatched, a_extra, a_already;

void draw_one(int row, struct plist *pl, FILE *f, int reverse)
{
	char hdr[10];
	hdr[0] = 0;

	if (pl == NULL) {
		move(row,0);
		clrtoeol();
		return;
	}
	if (pl->calced == 0 && pl->end) {
		/* better load the patch and count the chunks */
		struct stream s1, s2;
		struct stream s = load_segment(f, pl->start, pl->end);
		struct stream sf = load_file(pl->file);
		if (reverse)
			pl->chunks = split_patch(s, &s2, &s1);
		else
			pl->chunks = split_patch(s, &s1, &s2);

		if (sf.body == NULL) {
			pl->wiggles = pl->conflicts = -1;
		} else {
			struct file ff, fp1, fp2;
			struct csl *csl1, *csl2;
			struct ci ci;
			ff = split_stream(sf, ByWord, 0);
			fp1 = split_stream(s1, ByWord, 0);
			fp2 = split_stream(s2, ByWord, 0);
			csl1 = pdiff(ff, fp1, pl->chunks);
			csl2 = diff(fp1,fp2);
			ci = make_merger(ff, fp1, fp2, csl1, csl2, 0, 1);
			pl->wiggles = ci.wiggles;
			pl->conflicts = ci.conflicts;
			free(csl1);
			free(csl2);
			free(ff.list);
			free(fp1.list);
			free(fp2.list);
		}

		free(s1.body);
		free(s2.body);
		free(s.body);
		pl->calced = 1;
	}
	if (pl->end == 0) {
		strcpy(hdr, "         ");
	} else {
		if (pl->chunks > 99)
			strcpy(hdr, "XX");
		else sprintf(hdr, "%2d", pl->chunks);
		if (pl->wiggles > 99)
			strcpy(hdr+2, " XX");
		else sprintf(hdr+2, " %2d", pl->wiggles);
		if (pl->conflicts > 99)
			strcpy(hdr+5, " XX ");
		else sprintf(hdr+5, " %2d ", pl->conflicts);
	}
	if (pl->end)
		strcpy(hdr+9, "= ");
	else if (pl->open)
		strcpy(hdr+9, "+ ");
	else strcpy(hdr+9, "- ");

	mvaddstr(row, 0, hdr);
	mvaddstr(row, 11, pl->file);
	clrtoeol();
}

void addword(struct elmnt e)
{
	addnstr(e.start, e.len);
}
void addsep(struct elmnt e1, struct elmnt e2)
{
	int a,b,c,d,e,f;
	char buf[50];
	attron(a_sep);
	sscanf(e1.start+1, "%d %d %d", &a, &b, &c);
	sscanf(e2.start+1, "%d %d %d", &d, &e, &f);
	sprintf(buf, "@@ -%d,%d +%d,%d @@\n", b,c,e,f);
	addstr(buf);
	attroff(a_sep);
}
#define BEFORE	1
#define AFTER	2
#define	ORIG	4
#define	RESULT	8
#define CHANGED 32 /* The RESULT is different to ORIG */
#define CHANGES 64 /* AFTER is different to BEFORE */

struct pos {
	int a,b,c;
};
struct elmnt prev_elmnt(struct pos *pos, int mode,
		     struct file f1, struct file f2, struct csl *csl)
{
	while(1) {
		int a1, b1;
		if (pos->a > csl[pos->c].a) {
			assert(pos->b > csl[pos->c].b);
			pos->a--;
			pos->b--;
			return f1.list[pos->a];
		}
		b1=0;
		if (pos->c) b1 = csl[pos->c-1].b+csl[pos->c-1].len;
		if (pos->b > b1) {
			pos->b--;
			if (!(mode&AFTER))
				continue;
			return f2.list[pos->b];
		}
		a1=0;
		if (pos->c) a1 = csl[pos->c-1].a+csl[pos->c-1].len;
		if (pos->a > a1) {
			pos->a--;
			if (!(mode&BEFORE))
				continue;
			return f1.list[pos->a];
		}
		/* must be time to go to previous common segment */
		pos->c--;
		if (pos->c < 0) {
			struct elmnt e;
			e.start = NULL; e.len = 0;
			return e;
		}
	}
}

struct elmnt next_elmnt(struct pos *pos, int mode, int *type,
		     struct file f1, struct file f2, struct csl *csl)
{
	if (pos->c < 0) {
		struct elmnt e;
		e.start = NULL; e.len = 0;
		return e;
	}
	while(1) {
		int a1;
		*type = a_delete;
		if (pos->a < csl[pos->c].a) {
			if (mode & BEFORE)
				return f1.list[pos->a++];
			pos->a++;
			continue;
		}
		*type = a_added;
		if (pos->b < csl[pos->c].b) {
			if (mode & AFTER)
				return f2.list[pos->b++];
			pos->b++;
			continue;
		}
		*type = a_common;
		a1 = csl[pos->c].a + csl[pos->c].len;
		if (pos->a < a1) {
			pos->b++;
			return f1.list[pos->a++];
		}
		if (csl[pos->c].len == 0) {
			struct elmnt e;
			e.start = NULL; e.len = 0;
			pos->c = -1;
			return e;
		}
		pos->c++;
	}
}

void pos_sol(struct pos *pos, int mode, struct file f1, struct file f2, struct csl *csl)
{
	/* find the start-of-line before 'pos' considering 'mode' only.
	 */
	if (pos->c < 0) return;
	while(1) {
		struct pos tpos = *pos;
		struct elmnt e = prev_elmnt(&tpos, mode, f1, f2, csl);
		if (e.start == NULL) {
			return;
		}
		if (e.start[0] == '\n' || e.start[0] == 0)
			return;
		*pos = tpos;
	}
}

void prev_pos(struct pos *pos, int mode, struct file f1, struct file f2, struct csl *csl)
{
	/* find the start-of-line before 'pos' considering 'mode' only.
	 */
	int eol=0;
	if (pos->c < 0) return;
	while(1) {
		struct pos tpos = *pos;
		struct elmnt e = prev_elmnt(&tpos, mode, f1, f2, csl);
		if (e.start == NULL) {
			pos->c = -1;
			return;
		}
		if (e.start[0] == '\n' || e.start[0] == 0) {
			if (eol == 1)
				return;
			eol=1;
		}
		*pos = tpos;
		if (e.start[0] == 0) return;

	}
}

void next_pos(struct pos *pos, int mode, struct file f1, struct file f2, struct csl *csl)
{
	/* find the start-of-line after 'pos' considering 'mode' only.
	 */
	int type;
	if (pos->c < 0) return;
	while(1) {
		struct pos tpos = *pos;
		struct elmnt e = next_elmnt(&tpos, mode, &type, f1, f2, csl);
		if (e.start == NULL) {
			pos->c = -1;
			return;
		}
		*pos = tpos;
		if (e.start[0] == '\n') {
			return;
		}
		if (e.start[0] == 0)
			return;
	}
}

void draw_line(int i, struct pos pos, int mode,
	       struct file f1, struct file f2, struct csl *csl, int start, int len)
{
	int col = 0;
	int attr;
	struct elmnt e1;
	move(i, col);
	while (1) {
		e1 = next_elmnt(&pos, mode, &attr, f1, f2, csl);
		if (e1.start == NULL) {
			attr = a_void;
			break;
		}
		if (e1.start[0] == '\0') {
			int a,b,c;
			int d,e,f;
			struct elmnt e2 = f2.list[pos.b-1];
			char buf[50];
			(void)attrset(a_sep);
			sscanf(e1.start+1, "%d %d %d", &a, &b, &c);
			sscanf(e2.start+1, "%d %d %d", &d, &e, &f);
			sprintf(buf, "@@ -%d,%d +%d,%d @@\n", b,c, e,f);
			addstr(buf);
			break;
		} else {
			unsigned char *c;
			int l;
			(void)attrset(attr);
			if (e1.start[0] == '\n') {
				break;
			}
			c = (unsigned char *)e1.start;
			l = e1.len;
			while (l) {
				if (*c >= ' ' && *c != 0x7f) {
					/* normal width char */
					if (col >= start && col < len+start)
						mvaddch(i,col-start, *c);
					col++;
				} else if (*c == '\t') {
					do {
						if (col >= start && col < len+start)
							mvaddch(i, col-start, ' ');
						col++;
					} while ((col&7)!= 0);
				} else {
					if (col>=start && col < len+start)
						mvaddch(i, col-start, *c=='\n'?'@':'?');
					col++;
				}
				c++;
				l--;
			}
		}
	}
	bkgdset(attr);
	clrtoeol();
	bkgdset(A_NORMAL);
}

void diff_window(struct plist *p, FILE *f)
{
	/* display just the diff, either 'before' or 'after' or all.
	 *
	 * In any case highlighting is the same
	 *
	 * Current pos is given as a,b,csl where a and b are
	 * before or in the common segment, and one is  immediately
	 * after a newline.
	 * The current row is given by 'row'.
	 * Lines do not wrap, be horizontal scrolling is supported.
	 *
	 * We don't insist that the top line lies at or above the top
	 * of the screen, as that allows alignment of different views
	 * more easily.
	 */
	struct stream s;
	struct stream  s1, s2;
	struct file f1, f2;
	struct csl *csl;
	char buf[100];
	int ch;
	int row, rows, cols;
	int start = 0;
	int i;
	int c;
	int refresh = 2;
	int mode = BEFORE|AFTER;

	struct pos pos, tpos;

	s = load_segment(f, p->start, p->end);
	ch = split_patch(s, &s1, &s2);


	f1 = split_stream(s1, ByWord, 0);
	f2 = split_stream(s2, ByWord, 0);

	csl = diff(f1, f2);

	pos.a=0; pos.b=0; pos.c=0;
	row = 1;

	while(1) {
		if (refresh == 2) {
			clear();
			sprintf(buf, "File: %s\n", p->file);
			attrset(A_BOLD); mvaddstr(0,0,buf); clrtoeol(); attrset(A_NORMAL);
			refresh = 1;
		}
		if (row < 1 || row >= rows)
			refresh = 1;
		if (refresh) {
			refresh = 0;
			getmaxyx(stdscr,rows,cols);
			if (row < -3)
				row = (rows+1)/2;
			if (row < 1)
				row = 1;
			if (row >= rows+3)
				row = (rows+1)/2;
			if (row >= rows)
				row = rows-1;
			tpos = pos;
			pos_sol(&tpos, mode, f1, f2, csl);
			draw_line(row, tpos, mode, f1, f2, csl, start, cols);
			for (i=row-1; i>=1; i--) {
				prev_pos(&tpos, mode, f1,f2,csl);
				draw_line(i, tpos, mode, f1, f2,csl, start,cols);
			}
			tpos = pos;
			for (i=row+1; i<rows; i++) {
				next_pos(&tpos, mode, f1, f2, csl);
				draw_line(i, tpos, mode, f1, f2, csl, start,cols);
			}
		}
		move(row,0);
		c = getch();
		switch(c) {
		case 27: /* escape */
			break;
		case 'q':
			return;
		case 'L'-64:
			refresh = 2;
			break;

		case 'j':
		case 'n':
		case 'N':
		case 'N'-64:
		case KEY_DOWN:
			tpos = pos;
			next_pos(&tpos, mode, f1, f2, csl);
			if (tpos.c >= 0) {
				pos = tpos;
				row++;
			}
			break;
		case 'k':
		case 'p':
		case 'P':
		case 'P'-64:
		case KEY_UP:
			tpos = pos;
			prev_pos(&tpos, mode, f1, f2, csl);
			if (tpos.c >= 0) {
				pos = tpos;
				row--;
			}
			break;

		case 'a':
			mode = AFTER;
			refresh = 1;
			break;
		case 'b':
			mode = BEFORE;
			refresh = 1;
			break;
		case 'x':
			mode = AFTER|BEFORE;
			refresh = 1;
			break;

		case 'h':
		case KEY_LEFT:
			if (start > 0) start--;
			refresh = 1;
			break;
		case 'l':
		case KEY_RIGHT:
			if (start < cols) start++;
			refresh = 1;
			break;
		}
	}
}

struct mpos {
	struct mp {
		int m; /* merger index */
		int s; /* stream 0,1,2 for a,b,c */
		int o; /* offset in that stream */
	}       p, /* the current point */
		lo, /* eol for start of the current group */
		hi; /* eol for end of the current group */
	int side; /* -1 if on the '-' of a diff,
		   * +1 if on the '+',
		   * 0 if on an unchanged (lo/hi not meaningful)
		   */
};
int same_mpos(struct mpos a, struct mpos b)
{
	return a.p.m == b.p.m &&
		a.p.s == b.p.s &&
		a.p.o == b.p.o &&
		a.side == b.side;
}

int invalid(int s, enum mergetype type)
{
	switch(type) {
	case End: return 0;
	case Unmatched: return s>0;
	case Unchanged: return s>0;
	case Extraneous: return s<2;
	case Changed: return s==1;
	case Conflict: return 0;
	case AlreadyApplied: return 0;
	}
	return 0;
}

struct elmnt next_melmnt(struct mpos *pos,
			 struct file fm, struct file fb, struct file fa,
			 struct merge *m)
{
	int l;
	pos->p.o++;
	while(1) {
		if (pos->p.m < 0)
			l = 0;
		else switch(pos->p.s) {
		case 0: l = m[pos->p.m].al; break;
		case 1: l = m[pos->p.m].bl; break;
		case 2: l = m[pos->p.m].cl; break;
		}
		if (pos->p.o >= l) {
			pos->p.o = 0;
			do {
				pos->p.s++;
				if (pos->p.s > 2) {
					pos->p.s = 0;
					pos->p.m++;
				}
			} while (invalid(pos->p.s, m[pos->p.m].type));
		} else
			break;
	}
	if (pos->p.m == -1 || m[pos->p.m].type == End) {
		struct elmnt e;
		e.start = NULL; e.len = 0;
		return e;
	}
	switch(pos->p.s) {
	default: /* keep compiler happy */
	case 0: return fm.list[m[pos->p.m].a + pos->p.o];
	case 1: return fb.list[m[pos->p.m].b + pos->p.o];
	case 2: return fa.list[m[pos->p.m].c + pos->p.o];
	}
}

struct elmnt prev_melmnt(struct mpos *pos,
			 struct file fm, struct file fb, struct file fa,
			 struct merge *m)
{
	pos->p.o--;
	while (pos->p.m >=0 && pos->p.o < 0) {
		do {
			pos->p.s--;
			if (pos->p.s < 0) {
				pos->p.s = 2;
				pos->p.m--;
			}
		} while (pos->p.m >= 0 && invalid(pos->p.s, m[pos->p.m].type));
		if (pos->p.m>=0) {
			switch(pos->p.s) {
			case 0: pos->p.o = m[pos->p.m].al-1; break;
			case 1: pos->p.o = m[pos->p.m].bl-1; break;
			case 2: pos->p.o = m[pos->p.m].cl-1; break;
			}
		}
	}
	if (pos->p.m < 0) {
		struct elmnt e;
		e.start = NULL; e.len = 0;
		return e;
	}
	switch(pos->p.s) {
	default: /* keep compiler happy */
	case 0: return fm.list[m[pos->p.m].a + pos->p.o];
	case 1: return fb.list[m[pos->p.m].b + pos->p.o];
	case 2: return fa.list[m[pos->p.m].c + pos->p.o];
	}
}

int visible(int mode, enum mergetype type, int stream)
{
#if 0
	switch(side){
	case 0: /* left - orig plus merge */
		switch(type) {
		case End: return A_NORMAL;
		case Unmatched: return stream == 0 ? a_unmatched : -1;
		case Unchanged: return stream == 0 ? a_common : -1;
		case Extraneous: return -1;
		case Changed: return stream == 0? a_delete:stream==1?-1:a_added;
		case Conflict: return stream == 0 ? a_unmatched : -1;
		case AlreadyApplied: return stream == 0 ? a_unmatched : -1;
		}
		break;
	case 1: /* right - old plus new */
		switch(type) {
		case End: return A_NORMAL;
		case Unmatched: return -1;
		case Unchanged: return stream == 0 ? a_common : -1;
		case Extraneous: return stream==2 ? a_extra : -1;
		case Changed: return stream == 0? a_delete:stream==1?-1:a_added;
		case Conflict: return stream==0?-1:stream==1?(a_delete|A_UNDERLINE):a_added;
		case AlreadyApplied: return stream==0?-1:stream==1?a_delete:a_added;
		}
	}
	return 1;
#endif
	if (mode == 0) return -1;
	/* mode can be any combination of ORIG RESULT BEFORE AFTER */
	switch(type) {
	case End: /* The END is always visible */
		return A_NORMAL;
	case Unmatched: /* Visible in ORIG and RESULT */
		if (mode & (ORIG|RESULT))
			return a_unmatched;
		break;
	case Unchanged: /* visible everywhere, but only show stream 0 */
		if (stream == 0) return a_common;
		break;
	case Extraneous: /* stream 2 is visible in BEFORE and AFTER */
		if ((mode & (BEFORE|AFTER))
		    && stream == 2)
			return a_extra;
		break;
	case Changed: /* stream zero visible ORIG and BEFORE, stream 2 elsewhere */
		if (stream == 0 &&
		    (mode & (ORIG|BEFORE)))
			return a_delete;
		if (stream == 2 &&
		    (mode & (RESULT|AFTER)))
			return a_added;
		break;
	case Conflict:
		switch(stream) {
		case 0:
			if (mode & (ORIG|RESULT))
				return a_unmatched | A_REVERSE;
			break;
		case 1:
			if (mode & BEFORE)
				return a_extra | A_UNDERLINE;
			break;
		case 2:
			if (mode & (AFTER|RESULT))
				return a_added | A_UNDERLINE;
			break;
		}
		break;
	case AlreadyApplied:
		switch(stream) {
		case 0:
			if (mode & (ORIG|RESULT))
				return a_already;
			break;
		case 1:
			if (mode & BEFORE)
				return a_delete | A_UNDERLINE;
			break;
		case 2:
			if (mode & AFTER)
				return a_added | A_UNDERLINE;
			break;
		}
		break;
	}
	return -1;
}


int check_line(struct mpos pos, struct file fm, struct file fb, struct file fa,
	       struct merge *m, int mode);

void next_mline(struct mpos *pos, struct file fm, struct file fb, struct file fa,
	      struct merge *m, int mode)
{
	int mask;
	do {
		struct mp prv;
		int mode2;

		prv = pos->p;
#if 0
		if (pos->p.m < 0 || m[pos->p.m].type == End) {
			if (pos->side == -1) {
				pos->side = 1;
				pos->hi = prv;
				pos->p = pos->lo;
				continue;
			} else
				return;
		}
#endif
		while (1) {
			struct elmnt e = next_melmnt(pos, fm,fb,fa,m);
			if (e.start == NULL)
				break;
			if (ends_mline(e) &&
			    visible(mode, m[pos->p.m].type, pos->p.s) >= 0)
				break;
		}
		mode2 = check_line(*pos, fm,fb,fa,m,mode);

		if (pos->side == 1 && !(mode2 & CHANGES))
			/* left a diff-set */
			pos->side = 0;
		else if (pos->side == -1 && !(mode2 & CHANGES)) {
			/*flip from '-' to '+' - need to ensure still visible */
			pos->side = 1;
			pos->hi = prv;
			pos->p = pos->lo;
		} else if (pos->side == 0 && (mode2 & CHANGES)) {
			/* entered a diff-set */
			pos->side = -1;
			pos->lo = pos->p;
		}
		mask = ORIG|RESULT|BEFORE|AFTER|CHANGES|CHANGED;
		if (pos->side == 1)
			mask &= ~(ORIG|BEFORE);
		if (pos->side == -1)
			mask &= ~(RESULT|AFTER);
	} while (visible(mode&mask, m[pos->p.m].type, pos->p.s) < 0);

}

void prev_mline(struct mpos *pos, struct file fm, struct file fb, struct file fa,
		struct merge *m, int mode)
{
	int mask;
	do {
		struct mp prv;
		int mode2;

		prv = pos->p;
		if (pos->p.m < 0)
			return;
		while(1) {
			struct elmnt e = prev_melmnt(pos, fm,fb,fa,m);
			if (e.start == NULL)
				break;
			if (ends_mline(e) &&
			    visible(mode, m[pos->p.m].type, pos->p.s) >= 0)
				break;
		}
		mode2 = check_line(*pos, fm,fb,fa,m,mode);

		if (pos->side == -1 && !(mode2 & CHANGES))
			/* we have stepped out of a diff-set */
			pos->side = 0;
		else if (pos->side == 1 && !(mode2 & CHANGES)) {
			/* flipped from '+' to '-' */
			pos->side = -1;
			pos->lo = prv;
			pos->p = pos->hi;
		} else if (pos->side == 0 && (mode2 & CHANGES)) {
			/* entered a diffset */
			pos->side = 1;
			pos->hi = pos->p;
		}
		mask = ORIG|RESULT|BEFORE|AFTER|CHANGES|CHANGED;
		if (pos->side == 1)
			mask &= ~(ORIG|BEFORE);
		if (pos->side == -1)
			mask &= ~(RESULT|AFTER);
	} while (visible(mode&mask, m[pos->p.m].type, pos->p.s) < 0);
}


void blank(int row, int start, int cols, int attr)
{
	(void)attrset(attr);
	move(row,start);
	while (cols-- > 0)
		addch(' ');
}

/* Checkline determines how many screen lines are needed to display
 * a file line.
 * Options are:
 *     - one line that is before/original
 *     - one line that is after/result
 *     - one line that is unchanged/unmatched/extraneous
 *     - two lines, but only one on the left.
 *     - two lines, one before and one after.
 * CLARIFY/CORRECT this.
 */
int check_line(struct mpos pos, struct file fm, struct file fb, struct file fa,
	       struct merge *m, int mode)
{
	int rv = 0;
	struct elmnt e;

	if (visible(ORIG, m[pos.p.m].type, pos.p.s) >= 0)
		rv |= ORIG;
	if (visible(RESULT, m[pos.p.m].type, pos.p.s) >= 0)
		rv |= RESULT;
	if (visible(BEFORE, m[pos.p.m].type, pos.p.s) >= 0)
		rv |= BEFORE;
	if (visible(AFTER, m[pos.p.m].type, pos.p.s) >= 0)
		rv |= AFTER;

	do {
		if (m[pos.p.m].type == Changed)
			rv |= CHANGED | CHANGES;
		else if ((m[pos.p.m].type == AlreadyApplied ||
			  m[pos.p.m].type == Conflict))
			rv |= CHANGES;
		e = prev_melmnt(&pos, fm,fb,fa,m);
	} while (e.start != NULL &&
		 (!ends_mline(e) || visible(mode, m[pos.p.m].type, pos.p.s)==-1));

	return rv & (mode | CHANGED | CHANGES);
}

int mcontains(struct mpos pos,
	      struct file fm, struct file fb, struct file fa, struct merge *m,
	      int mode, char *search)
{
	/* See if and of the files, between start of this line and here,
	 * contain the search string
	 */
	struct elmnt e;
	int len = strlen(search);
	do {
		e = prev_melmnt(&pos, fm,fb,fa,m);
		if (e.start) {
			int i;
			for (i=0; i<e.len; i++)
				if (strncmp(e.start+i, search, len)==0)
					return 1;
		}
	} while (e.start != NULL &&
		 (!ends_mline(e) || visible(mode, m[pos.p.m].type, pos.p.s)==-1));
	return 0;
}

void draw_mside(int mode, int row, int offset, int start, int cols,
		struct file fm, struct file fb, struct file fa, struct merge *m,
		struct mpos pos,
		int target, int *colp)
{
	/* find previous visible newline, or start of file */
	struct elmnt e;
	int col = 0;
	do {
		e = prev_melmnt(&pos, fm,fb,fa,m);
	} while (e.start != NULL &&
		 (!ends_mline(e) || visible(mode, m[pos.p.m].type, pos.p.s)==-1));

	while (1) {
		unsigned char *c;
		int l;
		e = next_melmnt(&pos, fm,fb,fa,m);
		if (e.start == NULL ||
		    (ends_mline(e) && visible(mode, m[pos.p.m].type, pos.p.s) != -1)) {
			if (colp) *colp = col;
			if (col < start) col = start;
			if (e.start && e.start[0] == 0) {
				(void)attrset(visible(mode, m[pos.p.m].type, pos.p.s));
				mvaddstr(row, col-start+offset, "SEP");
				col += 3;
			}
			blank(row, col-start+offset, start+cols-col, e.start?visible(mode, m[pos.p.m].type, pos.p.s):A_NORMAL );
			return;
		}
		if (visible(mode, m[pos.p.m].type, pos.p.s) == -1) {
			continue;
		}
		if (e.start[0] == 0)
			continue;
		(void)attrset(visible(mode, m[pos.p.m].type, pos.p.s));
		c = (unsigned char *)e.start;
		l = e.len;
		while(l) {
			if (*c >= ' ' && *c != 0x7f) {
				if (col >= start && col < start+cols)
					mvaddch(row, col-start+offset, *c);
				col++;
			} else if (*c == '\t') {
				do {
					if (col >= start && col < start+cols)
						mvaddch(row, col-start+offset, ' ');
					col++;
				} while ((col&7)!= 0);
			} else {
				if (col >= start && col < start+cols)
					mvaddch(row, col-start+offset, '?');
				col++;
			}
			c++;
			if (colp && target <= col) {
				*colp = col;
				colp = NULL;
			}
			l--;
		}
	}
}

void draw_mline(int row, struct mpos pos,
		struct file fm, struct file fb, struct file fa,
		struct merge *m,
		int start, int cols, int mode,
		int target, int *colp)
{
	/*
	 * Draw the left and right images of this line
	 * One side might be a_blank depending on the
	 * visibility of this newline
	 * If Changed is found, return ORIG|RESULT | BEFORE|AFTER,
	 * If AlreadyApplied or Conflict, return BEFORE|AFTER
	 */
	int lcols, rcols;
	lcols = (cols-1)/2-1;
	rcols = cols - lcols - 3;

	attrset(A_STANDOUT);
	mvaddch(row, lcols+1, '|');

	attrset(A_NORMAL);
	if (!(mode & CHANGES)) {
		mvaddch(row, 0, ' ');
		mvaddch(row, lcols+2, ' ');
	} else if (mode & (ORIG|BEFORE)) {
		mvaddch(row, 0, '-');
		mvaddch(row, lcols+2, '-');
	} else {
		mvaddch(row, 0, '+');
		mvaddch(row, lcols+2, '+');
	}

	if (visible(mode&(ORIG|RESULT), m[pos.p.m].type, pos.p.s) >= 0)
		/* visible on left */
		draw_mside(mode&(ORIG|RESULT), row, 1, start, lcols,
			   fm,fb,fa,m, pos, target, colp);
	else {
		blank(row, 0, lcols+1, a_void);
		if (colp) *colp = 0;
	}

	if (visible(mode&(BEFORE|AFTER), m[pos.p.m].type, pos.p.s) >= 0)
		/* visible on right */
		draw_mside(mode&(BEFORE|AFTER), row, lcols+3, start, rcols,
			   fm,fb,fa,m, pos, 0, NULL);
	else
		blank(row, lcols+2, rcols+1, a_void);
}

extern void cleanlist(struct file a, struct file b, struct csl *list);

void merge_window(struct plist *p, FILE *f, int reverse)
{
	/* display the merge in two side-by-side
	 * panes.
	 * left side shows diff between original and new
	 * right side shows the requested patch
	 *
	 * Unmatched: c_unmatched - left only
	 * Unchanged: c_normal - left and right
	 * Extraneous: c_extra - right only
	 * Changed-a: c_changed - left and right
	 * Changed-c: c_new - left and right
	 * AlreadyApplied-b: c_old - right only
	 * AlreadyApplied-c: c_applied - left and right
	 * Conflict-a: ?? left only
	 * Conflict-b: ?? left and right
	 * Conflict-c: ??
	 *
	 * A Conflict is displayed as the original in the
	 * left side, and the highlighted diff in the right.
	 *
	 * Newlines are the key to display.
	 * 'pos' is always a newline (or eof).
	 * For each side that this newline is visible on, we
	 * rewind the previous newline visible on this side, and
	 * the display the stuff in between
	 *
	 * A 'position' is an offset in the merger, a stream
	 * choice (a,b,c - some aren't relevant) and an offset in
	 * that stream
	 */

	struct stream sm, sb, sa, sp; /* main, before, after, patch */
	struct file fm, fb, fa;
	struct csl *csl1, *csl2;
	struct ci ci;
	char buf[100];
	int ch;
	int refresh = 2;
	int rows,cols;
	int i, c;
	int mode = ORIG|RESULT | BEFORE|AFTER;

	int row,start = 0;
	int trow;
	int col=0, target=0;
	struct mpos pos;
	struct mpos tpos, toppos, botpos;
	int toprow,botrow;
	int mode2;
	int meta = 0, tmeta;
	int num= -1, tnum;
	char search[80];
	int searchlen = 0;
	int search_notfound = 0;
	int searchdir;
	struct search_anchor {
		struct search_anchor *next;
		struct mpos pos;
		int notfound;
		int row, col, searchlen;
	} *anchor = NULL;

	sp = load_segment(f, p->start, p->end);
	if (reverse)
		ch = split_patch(sp, &sa, &sb);
	else
		ch = split_patch(sp, &sb, &sa);

	sm = load_file(p->file);
	fm = split_stream(sm, ByWord, 0);
	fb = split_stream(sb, ByWord, 0);
	fa = split_stream(sa, ByWord, 0);

	csl1 = pdiff(fm, fb, ch);
	csl2 = diff(fb, fa);
#if 0
	cleanlist(fm, fb, csl1);
	cleanlist(fb, fa, csl2);
#endif

	ci = make_merger(fm, fb, fa, csl1, csl2, 0, 1);

	row = 1;
	pos.p.m = 0; /* merge node */
	pos.p.s = 0; /* stream number */
	pos.p.o = -1; /* offset */
	next_mline(&pos, fm,fb,fa,ci.merger, mode);
	mode2 = check_line(pos, fm,fb,fa, ci.merger, mode);
	if (!(mode2 & CHANGES))
		pos.side = 0;
	else {
		if (mode2 & (ORIG|BEFORE))
			pos.side = -1;
		else
			pos.side = 1;
		pos.lo = pos.p;
		tpos = pos;
		do {
			pos.hi = tpos.p;
			next_mline(&tpos, fm,fb,fa,ci.merger, mode);
			mode2 = check_line(tpos, fm,fb,fa, ci.merger, mode);
		} while ((mode2 & CHANGES) && ci.merger[tpos.p.m].type != End);
	}

	while(1) {
		if (refresh == 2) {
			clear();
			sprintf(buf, "File: %s%s\n", p->file,reverse?" - reversed":"");
			attrset(A_BOLD); mvaddstr(0,0,buf);
			clrtoeol();
			attrset(A_NORMAL);
			refresh = 1;
		}
		if (row < 1 || row >= rows)
			refresh = 1;


		if (refresh) {
			getmaxyx(stdscr, rows, cols);
			rows--; /* keep last row clear */
			if (row < -3)
				row = (rows-1)/2+1;
			if (row < 1)
				row = 1;
			if (row > rows+3)
				row = (rows-1)/2+1;
			if (row >= rows)
				row = rows-1;
		}

		/* Always refresh the line */
		while (start > target) { start -= 8; refresh = 1;}
		if (start < 0) start = 0;
	retry:
		mode2 = check_line(pos, fm,fb,fa,ci.merger,mode);
		if ((pos.side <= 0) && (mode2 & (ORIG|BEFORE)))
			draw_mline(row,pos,fm,fb,fa,ci.merger,start,cols, mode2&(ORIG|BEFORE|CHANGED|CHANGES), target, &col);
		else if (pos.side > 0 && (mode2 & (RESULT|AFTER))) {
			if (mode2 & CHANGED)
				draw_mline(row,pos,fm,fb,fa,ci.merger,start,cols, mode2&(RESULT|AFTER|CHANGED|CHANGES), target, &col);
			else if (mode2 & CHANGES)
				draw_mline(row,pos,fm,fb,fa,ci.merger,start,cols, mode2&(AFTER|CHANGED|CHANGES), target, &col);
		}
		if (col > (cols-3)/2+start) {
			start += 8;
			refresh = 1;
			goto retry;
		}
		if (col < start) {
			start -= 8;
			refresh = 1;
			if (start < 0) start = 0;
			goto retry;
		}
		if (refresh) {
			refresh = 0;
			tpos = pos;

			for (i=row-1; i>=1 && tpos.p.m >= 0; ) {
				prev_mline(&tpos, fm,fb,fa,ci.merger, mode);
				mode2 = check_line(tpos, fm,fb,fa, ci.merger, mode);

				if (tpos.side == 1 &&
				    (mode2 & (RESULT|AFTER)) &&
				    (mode2 & (CHANGED)))
					draw_mline(i--,tpos,fm,fb,fa,ci.merger,start,cols, mode2&(RESULT|AFTER|CHANGED|CHANGES), 0, NULL);
				else if (tpos.side == 1 &&
					 (mode2 & (RESULT|AFTER)) &&
				    (mode2 & (CHANGES)))
					draw_mline(i--,tpos,fm,fb,fa,ci.merger,start,cols, mode2&(AFTER|CHANGED|CHANGES), 0, NULL);
				else if ((tpos.side == 0 || tpos.side == -1) && (mode2 & (ORIG|BEFORE)))
					draw_mline(i--,tpos,fm,fb,fa,ci.merger,start,cols, mode2&(ORIG|BEFORE|CHANGED|CHANGES), 0, NULL);
			}
			toppos = tpos; toprow = i;
			while (i >= 1)
				blank(i--, 0, cols, a_void);
			tpos = pos;
			for (i=row; i<rows && ci.merger[tpos.p.m].type != End; ) {
				mode2 = check_line(tpos, fm,fb,fa,ci.merger,mode);
				if ((tpos.side <= 0) && (mode2 & (ORIG|BEFORE)))
					draw_mline(i++,tpos,fm,fb,fa,ci.merger,start,cols, mode2&(ORIG|BEFORE|CHANGED|CHANGES), 0, NULL);
				else if (tpos.side > 0 && (mode2 & (RESULT|AFTER))) {
					if (mode2 & CHANGED)
						draw_mline(i++,tpos,fm,fb,fa,ci.merger,start,cols, mode2&(RESULT|AFTER|CHANGED|CHANGES), 0, NULL);
					else if (mode2 & CHANGES)
						draw_mline(i++,tpos,fm,fb,fa,ci.merger,start,cols, mode2&(AFTER|CHANGED|CHANGES), 0, NULL);
				}
				next_mline(&tpos, fm,fb,fa,ci.merger, mode);
			}
			botpos = tpos; botrow = i;
			while (i<rows)
				blank(i++, 0, cols, a_void);
		} else {
		}
#define META(c) ((c)|0x1000)
#define	SEARCH(c) ((c)|0x2000)
		move(rows,0);
		attrset(A_NORMAL);
		if (num>=0) { char buf[10]; sprintf(buf, "%d ", num); addstr(buf);}
		if (meta & META(0)) addstr("ESC...");
		if (meta & SEARCH(0)) {
			if (searchdir) addstr("Backwards ");
			addstr("Search: ");
			addstr(search);
			if (search_notfound)
				addstr(" - Not Found.");
			search_notfound = 0;
		}
		clrtoeol();
		move(row,col-start);
		c = getch();
		tmeta = meta; meta = 0;
		tnum = num; num = -1;
		switch(c | tmeta) {
		case 27: /* escape */
		case META(27):
			meta = META(0);
			break;
		case META('<'): /* start of file */
		start:
			tpos = pos; row++;
			do {
				pos = tpos; row--;
				prev_mline(&tpos, fm,fb,fa,ci.merger,mode);
			} while (tpos.p.m >= 0);
			if (row <= 0)
				row = 0;
			break;
		case META('>'): /* end of file */
		case 'G':
			if (tnum >=0) goto start;
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm,fb,fa,ci.merger,mode);
			} while (ci.merger[tpos.p.m].type != End);
			if (row >= rows)
				row = rows;
			break;
		case '0' ... '9':
			if (tnum < 0) tnum = 0;
			num = tnum*10 + (c-'0');
			break;
		case 'q':
			return;

		case '/':
		case 'S'-64:
			/* incr search forward */
			meta = SEARCH(0);
			searchlen = 0;
			search[searchlen] = 0;
			searchdir = 0;
			break;
		case '\\':
		case 'R'-64:
			/* incr search backwards */
			meta = SEARCH(0);
			searchlen = 0;
			search[searchlen] = 0;
			searchdir = 1;
			break;
		case SEARCH('G'-64):
		case SEARCH('S'-64):
			/* search again */
			meta = SEARCH(0);
			tpos = pos; trow = row;
			if (searchdir) {
				trow--;
				prev_mline(&tpos, fm,fb,fa,ci.merger,mode);
			} else {
				trow++;
				next_mline(&tpos, fm,fb,fa,ci.merger,mode);
			}
			goto search_again;

		case SEARCH('H'-64):
			meta = SEARCH(0);
			if (anchor) {
				struct search_anchor *a;
				a = anchor;
				anchor = a->next;
				free(a);
			}
			if (anchor) {
				struct search_anchor *a;
				a = anchor;
				anchor = a->next;
				pos = a->pos;
				row = a->row;
				col = a->col;
				search_notfound = a->notfound;
				searchlen = a->searchlen;
				search[searchlen] = 0;
				free(a);
				refresh = 1;
			}
			break;
		case SEARCH(' ') ... SEARCH('~'):
		case SEARCH('\t'):
			meta = SEARCH(0);
			if (searchlen < sizeof(search)-1)
				search[searchlen++] = c & (0x7f);
			search[searchlen] = 0;
			tpos = pos; trow = row;
		search_again:
			search_notfound = 1;
			do {
				if (mcontains(tpos, fm,fb,fa,ci.merger,mode,search)) {
					pos = tpos;
					row = trow;
					search_notfound = 0;
					break;
				}
				if (searchdir) {
					trow--;
					prev_mline(&tpos, fm,fb,fa,ci.merger,mode);
				} else {
					trow++;
					next_mline(&tpos, fm,fb,fa,ci.merger,mode);
				}
			} while (tpos.p.m >= 0 && ci.merger[tpos.p.m].type != End);

			break;
		case 'L'-64:
			refresh = 2;
			if (toprow >= 1) row -= (toprow+1);
			break;

		case 'V'-64: /* page down */
			pos = botpos;
			if (botrow < rows)
				row = botrow;
			else
				row = 2;
			refresh = 1;
			break;
		case META('v'): /* page up */
			pos = toppos;
			if (toprow >= 1)
				row = toprow+1;
			else
				row = rows-2;
			refresh = 1;
			break;

		case 'j':
		case 'n':
		case 'N'-64:
		case KEY_DOWN:
			if (tnum < 0) tnum = 1;
			for (; tnum > 0 ; tnum--) {
				tpos = pos;
				next_mline(&tpos, fm,fb,fa,ci.merger, mode);
				if (ci.merger[tpos.p.m].type != End) {
					pos = tpos;
					row++;
				}
			}
			break;
		case 'N':
			/* Next diff */
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm,fb,fa,ci.merger, mode);
			} while (pos.side != 0 && ci.merger[tpos.p.m].type != End);
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm,fb,fa,ci.merger, mode);
			} while (pos.side == 0 && ci.merger[tpos.p.m].type != End);

			break;
		case 'P':
			/* Previous diff */
			tpos = pos; row++;
			do {
				pos = tpos; row--;
				prev_mline(&tpos, fm,fb,fa,ci.merger, mode);
			} while (tpos.side == 0 && tpos.p.m >= 0);
			tpos = pos; row++;
			do {
				pos = tpos; row--;
				prev_mline(&tpos, fm,fb,fa,ci.merger, mode);
			} while (tpos.side != 0 && tpos.p.m >= 0);

			break;

		case 'k':
		case 'p':
		case 'P'-64:
		case KEY_UP:
			if (tnum < 0) tnum = 1;
			for (; tnum > 0 ; tnum--) {
				tpos = pos;
				prev_mline(&tpos, fm,fb,fa,ci.merger, mode);
				if (tpos.p.m >= 0) {
					pos = tpos;
					row--;
				}
			}
			break;

		case KEY_LEFT:
		case 'h':
			/* left */
			target = col - 1;
			if (target < 0) target = 0;
			break;
		case KEY_RIGHT:
		case 'l':
			/* right */
			target = col + 1;
			break;

		case '^':
		case 'A'-64:
			/* Start of line */
			target = 0;
			break;
		case '$':
		case 'E'-64:
			/* End of line */
			target = 1000;
			break;

		case 'a': /* 'after' view in patch window */
			if (mode & AFTER)
				mode &= ~BEFORE;
			else
				mode |= AFTER;
			refresh = 1;
			break;
		case 'b': /* 'before' view in patch window */
			if (mode & BEFORE)
				mode &= ~AFTER;
			else
				mode |= BEFORE;
			refresh = 1;
			break;
		case 'o': /* 'original' view in the merge window */
			if (mode & ORIG)
				mode &= ~RESULT;
			else
				mode |= ORIG;
			refresh = 1;
			break;
		case 'r': /* the 'result' view in the merge window */
			if (mode & RESULT)
				mode &= ~ORIG;
			else
				mode |= RESULT;
			refresh = 1;
			break;
#if 0
		case 'h':
		case KEY_LEFT:
			if (start > 0) start--;
			refresh = 1;
			break;
		case 'l':
		case KEY_RIGHT:
			if (start < cols) start++;
			refresh = 1;
			break;
#endif
		}

		if (meta == SEARCH(0)) {
			if (anchor == NULL ||
			    !same_mpos(anchor->pos, pos) ||
			    anchor->searchlen != searchlen ||
			    anchor->col != col) {
				struct search_anchor *a = malloc(sizeof(*a));
				a->pos = pos;
				a->row = row;
				a->col = col;
				a->searchlen = searchlen;
				a->notfound = search_notfound;
				a->next = anchor;
				anchor = a;
			}
		} else {
			while (anchor) {
				struct search_anchor *a = anchor;
				anchor = a->next;
				free(a);
			}
		}
	}
}

void main_window(struct plist *pl, int n, FILE *f, int reverse)
{
	/* The main window lists all files together with summary information:
	 * number of chunks, number of wiggles, number of conflicts.
	 * The list is scrollable
	 * When a entry is 'selected', we switch to the 'file' window
	 * The list can be condensed by removing files with no conflict
	 * or no wiggles, or removing subdirectories
	 *
	 * We record which file in the list is 'current', and which
	 * screen line it is on.  We try to keep things stable while
	 * moving.
	 *
	 * Counts are printed before the name using at most 2 digits.
	 * Numbers greater than 99 are XX
	 * Ch Wi Co File
	 * 27 5   1 drivers/md/md.c
	 *
	 * A directory show the sum in all children.
	 *
	 * Commands:
	 *  select:  enter, space, mouseclick
	 *      on file, go to file window
	 *      on directory, toggle open
	 *  up:  k, p, control-p uparrow
	 *      Move to previous open object
	 *  down: j, n, control-n, downarrow
	 *      Move to next open object
	 *
	 */
	int pos=0; /* position in file */
	int row=1; /* position on screen */
	int rows; /* size of screen in rows */
	int cols;
	int tpos, i;
	int refresh = 2;
	int c=0;

	while(1) {
		if (refresh == 2) {
			clear(); attrset(0);
			attron(A_BOLD);
			mvaddstr(0,0,"Ch Wi Co Patched Files");
			move(2,0);
			attroff(A_BOLD);
			refresh = 1;
		}
		if (row <1  || row >= rows)
			refresh = 1;
		if (refresh) {
			refresh = 0;
			getmaxyx(stdscr, rows, cols);
			if (row >= rows +3)
				row = (rows+1)/2;
			if (row >= rows)
				row = rows-1;
			tpos = pos;
			for (i=row; i>1; i--) {
				tpos = get_prev(tpos, pl, n);
				if (tpos == -1) {
					row = row - i + 1;
					break;
				}
			}
			/* Ok, row and pos could be trustworthy now */
			tpos = pos;
			for (i=row; i>=1; i--) {
				draw_one(i, &pl[tpos], f, reverse);
				tpos = get_prev(tpos, pl, n);
			}
			tpos = pos;
			for (i=row+1; i<rows; i++) {
				tpos = get_next(tpos, pl, n);
				if (tpos >= 0)
					draw_one(i, &pl[tpos], f, reverse);
				else
					draw_one(i, NULL, f, reverse);
			}
		}
		{char bb[20]; sprintf(bb,"%d", c); mvaddstr(0, 70, bb); clrtoeol();}
		move(row, 9);
		c = getch();
		switch(c) {
		case 'j':
		case 'n':
		case 'N':
		case 'N'-64:
		case KEY_DOWN:
			tpos = get_next(pos, pl, n);
			if (tpos >= 0) {
				pos = tpos;
				row++;
			}
			break;
		case 'k':
		case 'p':
		case 'P':
		case 'P'-64:
		case KEY_UP:
			tpos = get_prev(pos, pl, n);
			if (tpos >= 0) {
				pos = tpos;
				row--;
			}
			break;

		case ' ':
		case 13:
			if (pl[pos].end == 0) {
				pl[pos].open = ! pl[pos].open;
				refresh = 1;
			} else {
				/* diff_window(&pl[pos], f); */
				merge_window(&pl[pos],f,reverse);
				refresh = 2;
			}
			break;
		case 27: /* escape */
			mvaddstr(0,70,"ESC..."); clrtoeol();
			c = getch();
			switch(c) {
			}
			break;
		case 'q':
			return;

		case KEY_RESIZE:
			refresh = 2;
			break;
		}
	}
}


int vpatch(int argc, char *argv[], int strip, int reverse, int replace)
{
	/* NOT argv[0] is first arg... */
	int n = 0;
	FILE *f = NULL;
	FILE *in = stdin;
	struct plist *pl;

	if (argc >=1) {
		in = fopen(argv[0], "r");
		if (!in) {
			printf("No %s\n", argv[0]);
			exit(1);
		}
	} else {
		in = stdin;
	}
	if (lseek(fileno(in),0L, 1) == -1) {
		f = tmpfile();
		if (!f) {
			fprintf(stderr, "Cannot create a temp file\n");
			exit(1);
		}
	}

	pl = parse_patch(in, f, &n);

	if (set_prefix(pl, n, strip) == 0) {
		fprintf(stderr, "%s: aborting\n", Cmd);
		exit(2);
	}
	pl = sort_patches(pl, &n);

	if (f) {
		if (fileno(in) == 0) {
			close(0);
			dup(2);
		} else
			fclose(in);
		in = f;
	}
#if 0
	int i;
	for (i=0; i<n ; i++) {
		printf("%3d: %3d %2d/%2d %s\n", i, pl[i].parent, pl[i].prev, pl[i].next, pl[i].file);
	}
	exit(0);
#endif
	signal(SIGINT, catch);
	signal(SIGQUIT, catch);
	signal(SIGTERM, catch);
	signal(SIGBUS, catch);
	signal(SIGSEGV, catch);

	initscr(); cbreak(); noecho();
	start_color();
	use_default_colors();
	if (!has_colors()) {
		a_delete = A_UNDERLINE;
		a_added = A_BOLD;
		a_common = A_NORMAL;
		a_sep = A_STANDOUT;
		a_already = A_STANDOUT;
	} else {
		init_pair(1, COLOR_WHITE, COLOR_RED);
		a_delete = COLOR_PAIR(1);
		init_pair(2, COLOR_WHITE, COLOR_BLUE);
		a_added = COLOR_PAIR(2);
		a_common = A_NORMAL;
		init_pair(3, COLOR_WHITE, COLOR_GREEN);
		a_sep = COLOR_PAIR(3);
		init_pair(4, COLOR_WHITE, COLOR_YELLOW);
		a_void = COLOR_PAIR(4);
		init_pair(5, COLOR_BLUE, -1);
		a_unmatched = COLOR_PAIR(5);
		init_pair(6, COLOR_CYAN, -1);
		a_extra = COLOR_PAIR(6);

		init_pair(7, COLOR_BLACK, COLOR_CYAN);
		a_already = COLOR_PAIR(7);
	}
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	mousemask(ALL_MOUSE_EVENTS, NULL);

	main_window(pl, n, in, reverse);

	nocbreak();nl();endwin();
	return 0;
	exit(0);
}
#if 0
 WiggleVerbose=1 ~/work/wiggle/wiggle -mR fs/nfsd/nfs4callback.c .patches/removed/144NfsdV4-033 |less
neilb@notabene:/home/src/mm$

~/work/wiggle/wiggle -BR .patches/removed/144NfsdV4-033


#endif
