/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2005 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2010-2011 Neil Brown <neilb@suse.de>
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
 * vpatch - visual front end for wiggle - aka Browse mode.
 *
 * "files" display, lists all files with statistics
 *    - can hide various lines including subdirectories
 *      and files without wiggles or conflicts
 * "merge" display shows various views of  merged file with different
 *  parts in different colours.
 *
 *  The window can be split horizontally to show the original and result
 *  beside the diff, and each different branch can be shown alone.
 *
 */

#include "wiggle.h"
#include <curses.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>

static void term_init(int raw);

static int intr_kills = 0;

/* global attributes */
unsigned int a_delete, a_added, a_common, a_sep, a_void,
	a_unmatched, a_extra, a_already;
unsigned int a_has_conflicts, a_has_wiggles, a_no_wiggles, a_saved;

/******************************************************************
 * Help window
 * We display help in an insert, leaving 5 columns left and right,
 * and 2 rows top and bottom, but at most 58x15 plus border
 * In help mode:
 *   SPC or RTN moves down or to next page
 *   BKSPC goes backwards
 *   'q' returns to origin screen
 *   '?' show help on help
 *   left and right scroll help view
 *
 * A help text is an array of lines of text
 */

char *help_help[] = {
	"   You are viewing the help page for the help viewer.",
	"You normally get here by typing '?'",
	"",
	"The following keystrokes work in the help viewer:",
	"  ?     display this help message",
	"  q     return to previous view",
	"  SPC   move forward through help document",
	"  RTN   same as SPC",
	"  BKSP  move backward through help document",
	"  RIGHT scroll help window so text on the right appears",
	"  LEFT  scroll help window so text on the left appears",
	NULL
};

char *help_missing[] = {
	"The file that this patch applies to appears",
	"to be missing.",
	"Please type 'q' to continue",
	NULL
};

char *help_corrupt[] = {
	"This patch appears to be corrupt",
	"Please type 'q' to continue",
	NULL
};

/* We can give one or two pages to display in the help window.
 * The first is specific to the current context.  The second
 * is optional and  may provide help in a more broad context.
 */
static int help_window(char *page1[], char *page2[], int query)
{
	int rows, cols;
	int top, left;
	int r, c;
	int ch;
	char **page = page1;
	int line = 0;
	int shift = 0;

	getmaxyx(stdscr, rows, cols);

	if (cols < 70) {
		left = 6;
		cols = cols-12;
	} else {
		left = (cols-58)/2 - 1;
		cols = 58;
	}

	if (rows < 21) {
		top = 3;
		rows = rows - 6;
	} else {
		top = (rows-15)/2 - 1;
		rows = 15;
	}

	/* Draw a border around the 'help' area */
	(void)attrset(A_STANDOUT);
	for (c = left; c < left+cols; c++) {
		mvaddch(top-1, c, '-');
		mvaddch(top+rows, c, '-');
	}
	for (r = top; r < top + rows ; r++) {
		mvaddch(r, left-1, '|');
		mvaddch(r, left+cols, '|');
	}
	mvaddch(top-1, left-1, '/');
	mvaddch(top-1, left+cols,  '\\');
	mvaddch(top+rows, left-1, '\\');
	mvaddch(top+rows, left+cols, '/');
	if (query) {
		mvaddstr(top-1, left + cols/2 - 4, "Question");
		mvaddstr(top+rows, left + cols/2 - 9,
			 "Answer Y, N, or Q.");
	} else {
		mvaddstr(top-1, left + cols/2 - 9,
			 "HELP - 'q' to exit");
		mvaddstr(top+rows, left+cols/2 - 17,
			 "Press SPACE for more, '?' for help");
	}
	(void)attrset(A_NORMAL);

	while (1) {
		char **lnp = page + line;

		/* Draw as much of the page at the current offset
		 * as fits.
		 */
		for (r = 0; r < rows; r++) {
			char *ln = *lnp;
			int sh = shift;
			if (ln)
				lnp++;
			else
				ln = "";

			while (*ln && sh > 0) {
				ln++;
				sh--;
			}
			for (c = 0; c < cols; c++) {
				int chr = *ln;
				if (chr)
					ln++;
				else
					chr = ' ';
				mvaddch(top+r, left+c, chr);
			}
		}
		move(top+rows-1, left);
		ch = getch();

		switch (ch) {
		case 'C' - 64:
		case 'Q':
		case 'q':
			return -1;
			break;
		case 'Y':
		case 'y':
			if (query)
				return 1;
			break;
		case 'N':
		case 'n':
			if (query)
				return 0;
			break;

		case '?':
			if (page1 != help_help)
				help_window(help_help, NULL, 0);
			break;
		case ' ':
		case '\r': /* page-down */
			for (r = 0; r < rows-2; r++)
				if (page[line])
					line++;
			if (!page[line] && !query) {
				line = 0;
				if (page == page1)
					page = page2;
				else
					page = NULL;
				if (page == NULL)
					return -1;
			}
			break;

		case '\b': /* page up */
			if (line > 0) {
				line -= (rows-2);
				if (line < 0)
					line = 0;
			} else {
				if (page == page2)
					page = page1;
				else
					page = page2;
				if (page == NULL)
					page = page1;
				line = 0;
			}
			break;

		case KEY_LEFT:
			if (shift > 0)
				shift--;
			break;
		case KEY_RIGHT:
			shift++;
			break;

		case KEY_UP:
			if (line > 0)
				line--;
			break;
		case KEY_DOWN:
			if (page[line])
				line++;
			break;
		}
	}
}

static char *typenames[] = {
	[End] = "End",
	[Unmatched] = "Unmatched",
	[Unchanged] = "Unchanged",
	[Extraneous] = "Extraneous",
	[Changed] = "Changed",
	[Conflict] = "Conflict",
	[AlreadyApplied] = "AlreadyApplied",
};

/* When we merge the original and the diff together we need
 * to keep track of where everything came from.
 * When we display the different views, we need to be able to
 * select certain portions of the whole document.
 * These flags are used to identify what is present, and to
 * request different parts be extracted.  They also help
 * guide choice of colour.
 */
#define BEFORE	1
#define AFTER	2
#define	ORIG	4
#define	RESULT	8
#define CHANGES 16 /* A change is visible here,
		    * so 2 streams need to be shown */
#define WIGGLED 32 /* a conflict that was successfully resolved */
#define CONFLICTED 64 /* a conflict that was not successfully resolved */

/* Displaying a Merge.
 * The first step is to linearise the merge.  The merge in inherently
 * parallel with before/after streams.  However much of the whole document
 * is linear as normally much of the original in unchanged.
 * All parallelism comes from the patch.  This normally produces two
 * parallel stream, but in the case of a conflict can produce three.
 * For browsing the merge we only ever show two alternates in-line.
 * When there are three we use two panes with 1 or 2 alternates in each.
 * So to linearise the two streams we find lines that are completely
 * unchanged (same for all 3 streams, or missing in 2nd and 3rd) which bound
 * a region where there are changes.  We include everything between
 * these twice, in two separate passes.  The exact interpretation of the
 * passes is handled at a higher level but will be one of:
 *  original and result
 *  before and after
 *  original and after (for a conflict)
 * This is all encoded in the 'struct merge'.  An array of these describes
 * the whole document.
 *
 * At any position in the merge we can be in one of 3 states:
 *  0: unchanged section
 *  1: first pass
 *  2: second pass
 *
 * So to walk a merge in display order we need a position in the merge,
 * a current state, and when in a changed section, we need to know the
 * bounds of that changed section.
 * This is all encoded in 'struct mpos'.
 *
 * Each location may or may not be visible depending on certain
 * display options.
 *
 * Also, some locations might be 'invalid' in that they don't need to be displayed.
 * For example when the patch leaves a section of the original unchanged,
 * we only need to see the original - the before/after sections are treated
 * as invalid and are not displayed.
 * The visibility of newlines is crucial and guides the display.  One line
 * of displayed text is all the visible sections between two visible newlines.
 *
 * Counting lines is a bit tricky.  We only worry about line numbers in the
 * original (stream 0) as these could compare with line numbers mentioned in
 * patch chunks.
 * We count 2 for every line: 1 for everything before the newline and 1 for the newline.
 * That way we don't get a full counted line until we see the first char after the
 * newline, so '+' lines are counted with the previous line.
 *
 */
struct mp {
	int m; /* merger index */
	int s; /* stream 0,1,2 for a,b,c */
	int o; /* offset in that stream */
	int lineno; /* Counts newlines in stream 0
		     * set lsb when see newline.
		     * add one when not newline and lsb set
		     */
};
struct mpos {
	struct mp p, /* the current point (end of a line) */
		lo, /* eol for start of the current group */
		hi; /* eol for end of the current group */
	int state; /*
		    * 0 if on an unchanged (lo/hi not meaningful)
		    * 1 if on the '-' of a diff,
		    * 2 if on the '+' of a diff
		    */
};

struct cursor {
	struct mp pos;	/* where in the document we are (an element)  */
	int offset;	/* which char in that element */
	int target;	/* display column - or -1 if we are looking for 'pos' */
	int col;	/* where we found pos or target */
	int width;	/* Size of char, for moving to the right */
	int alt;	/* Cursor is in alternate window */
};

/* used for checking location during search */
static int same_mp(struct mp a, struct mp b)
{
	return a.m == b.m &&
		a.s == b.s &&
		a.o == b.o;
}
static int same_mpos(struct mpos a, struct mpos b)
{
	return same_mp(a.p, b.p) &&
		(a.state == b.state || a.state == 0 || b.state == 0);
}

/* Check if a particular stream is meaningful in a particular merge
 * section.  e.g. in an Unchanged section, only stream 0, the
 * original, is meaningful.  This is used to avoid walking down
 * pointless paths.
 */
static int stream_valid(int s, enum mergetype type)
{
	switch (type) {
	case End:
		return 1;
	case Unmatched:
		return s == 0;
	case Unchanged:
		return s == 0;
	case Extraneous:
		return s == 2;
	case Changed:
		return s != 1;
	case Conflict:
		return 1;
	case AlreadyApplied:
		return 1;
	}
	return 0;
}

/*
 * Advance the 'pos' in the current mergepos returning the next
 * element (word).
 * This walks the merges in sequence, and the streams within
 * each merge.
 */
static struct elmnt next_melmnt(struct mp *pos,
				struct file fm, struct file fb, struct file fa,
				struct merge *m)
{
	pos->o++;
	while (pos->m < 0 || m[pos->m].type != End) {
		int l = 0; /* Length remaining in current merge section */
		if (pos->m >= 0)
			switch (pos->s) {
			case 0:
				l = m[pos->m].al;
				break;
			case 1:
				l = m[pos->m].bl;
				break;
			case 2:
				l = m[pos->m].cl;
				break;
			}
		if (pos->o >= l) {
			/* Offset has reached length, choose new stream or
			 * new merge */
			pos->o = 0;
			do {
				pos->s++;
				if (pos->s > 2) {
					pos->s = 0;
					pos->m++;
				}
			} while (!stream_valid(pos->s, m[pos->m].oldtype));
		} else
			break;
	}
	if (pos->m == -1 || m[pos->m].type == End) {
		struct elmnt e;
		e.start = NULL; e.hash = 0; e.len = 0;
		return e;
	}
	switch (pos->s) {
	default: /* keep compiler happy */
	case 0:
		if (pos->lineno & 1)
			pos->lineno++;
		if (ends_line(fm.list[m[pos->m].a + pos->o]))
			pos->lineno++;
		return fm.list[m[pos->m].a + pos->o];
	case 1: return fb.list[m[pos->m].b + pos->o];
	case 2: return fa.list[m[pos->m].c + pos->o];
	}
}

/* step current position.p backwards */
static struct elmnt prev_melmnt(struct mp *pos,
				struct file fm, struct file fb, struct file fa,
				struct merge *m)
{
	if (pos->s == 0) {
		if (m[pos->m].a + pos->o < fm.elcnt &&
		    ends_line(fm.list[m[pos->m].a + pos->o]))
			pos->lineno--;
		if (pos->lineno & 1)
			pos->lineno--;
	}

	pos->o--;
	while (pos->m >= 0 && pos->o < 0) {
		do {
			pos->s--;
			if (pos->s < 0) {
				pos->s = 2;
				pos->m--;
			}
		} while (pos->m >= 0 &&
			 !stream_valid(pos->s, m[pos->m].oldtype));
		if (pos->m >= 0) {
			switch (pos->s) {
			case 0:
				pos->o = m[pos->m].al-1;
				break;
			case 1:
				pos->o = m[pos->m].bl-1;
				break;
			case 2:
				pos->o = m[pos->m].cl-1;
				break;
			}
		}
	}
	if (pos->m < 0 || m[pos->m].type == End) {
		struct elmnt e;
		e.start = NULL; e.hash = 0; e.len = 0;
		return e;
	}
	switch (pos->s) {
	default: /* keep compiler happy */
	case 0: return fm.list[m[pos->m].a + pos->o];
	case 1: return fb.list[m[pos->m].b + pos->o];
	case 2: return fa.list[m[pos->m].c + pos->o];
	}
}

/* 'visible' not only checks if this stream in this merge should be
 * visible in this mode, but also chooses which colour/highlight to use
 * to display it.
 */
static int visible(int mode, struct merge *m, struct mpos *pos)
{
	enum mergetype type;
	int stream = pos->p.s;

	if (mode == 0)
		return -1;
	if (pos->p.m < 0)
		type = End;
	else if (mode & RESULT)
		type = m[pos->p.m].type;
	else
		type = m[pos->p.m].oldtype;
	/* mode can be any combination of ORIG RESULT BEFORE AFTER */
	switch (type) {
	case End: /* The END is always visible */
		return A_NORMAL;
	case Unmatched: /* Visible in ORIG and RESULT */
		if (mode & (ORIG|RESULT))
			return a_unmatched;
		break;
	case Unchanged: /* visible everywhere, but only show stream 0 */
		if (stream == 0)
			return a_common;
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
		switch (stream) {
		case 0:
			if (mode & ORIG)
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
		switch (stream) {
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

/* checkline creates a summary of the sort of changes that
 * are in a line, returning an "or" of
 *  CHANGES
 *  WIGGLED
 *  CONFLICTED
 */
static int check_line(struct mpos pos, struct file fm, struct file fb,
		      struct file fa,
		      struct merge *m, int mode)
{
	int rv = 0;
	struct elmnt e;
	int unmatched = 0;

	if (pos.p.m < 0)
		return 0;
	do {
		int type = m[pos.p.m].oldtype;
		if (mode & RESULT)
			type = m[pos.p.m].type;
		if (type == Changed)
			rv |= CHANGES;
		else if (type == Conflict) {
			rv |= CONFLICTED | CHANGES;
		} else if (type == AlreadyApplied) {
			rv |= CONFLICTED;
			if (mode & (BEFORE|AFTER))
				rv |= CHANGES;
		} else if (type == Extraneous) {
			if (fb.list[m[pos.p.m].b].start[0] == '\0')
				/* hunk headers don't count as wiggles
				 * and nothing before a hunk header
				 * can possibly be part of this 'line' */
				break;
			else
				rv |= WIGGLED;
		} else if (type == Unmatched)
			unmatched = 1;

		if (m[pos.p.m].in_conflict > 1)
			rv |= CONFLICTED | CHANGES;
		if (m[pos.p.m].in_conflict == 1 &&
		    (pos.p.o < m[pos.p.m].lo ||
		     pos.p.o > m[pos.p.m].hi))
			rv |= CONFLICTED | CHANGES;
		e = prev_melmnt(&pos.p, fm, fb, fa, m);
	} while (e.start != NULL &&
		 (!ends_line(e)
		  || visible(mode, m, &pos) == -1));

	if (unmatched && (rv & CHANGES))
		rv |= WIGGLED;
	return rv;
}

/* Find the next line in the merge which is visible.
 * If we hit the end of a conflicted set during pass-1
 * we rewind for pass-2.
 * 'mode' tells which bits we want to see, possible one of
 * the 4 parts (before/after/orig/result) or one of the pairs
 * before+after or orig+result.
 */
static void next_mline(struct mpos *pos, struct file fm, struct file fb,
		       struct file fa,
		       struct merge *m, int mode)
{
	int mask;
	do {
		struct mp prv;
		int mode2;

		prv = pos->p;
		while (1) {
			struct elmnt e = next_melmnt(&pos->p, fm, fb, fa, m);
			if (e.start == NULL)
				break;
			if (ends_line(e) &&
			    visible(mode, m, pos) >= 0)
				break;
		}
		mode2 = check_line(*pos, fm, fb, fa, m, mode);

		if ((mode2 & CHANGES) && pos->state == 0) {
			/* Just entered a diff-set */
			pos->lo = pos->p;
			pos->state = 1;
		} else if (!(mode2 & CHANGES) && pos->state) {
			/* Come to the end of a diff-set */
			switch (pos->state) {
			case 1:
				/* Need to record the end */
				pos->hi = prv;
				/* time for another pass */
				pos->p = pos->lo;
				pos->state++;
				break;
			case 2:
				/* finished final pass */
				pos->state = 0;
				break;
			}
		}
		mask = ORIG|RESULT|BEFORE|AFTER;
		switch (pos->state) {
		case 1:
			mask &= ~(RESULT|AFTER);
			break;
		case 2:
			mask &= ~(ORIG|BEFORE);
			break;
		}
	} while (visible(mode&mask, m, pos) < 0);

}

/* Move to previous line - simply the reverse of next_mline */
static void prev_mline(struct mpos *pos, struct file fm, struct file fb,
		       struct file fa,
		       struct merge *m, int mode)
{
	int mask;
	do {
		struct mp prv;
		int mode2;

		prv = pos->p;
		if (pos->p.m < 0)
			return;
		while (1) {
			struct elmnt e = prev_melmnt(&pos->p, fm, fb, fa, m);
			if (e.start == NULL)
				break;
			if (ends_line(e) &&
			    visible(mode, m, pos) >= 0)
				break;
		}
		mode2 = check_line(*pos, fm, fb, fa, m, mode);

		if ((mode2 & CHANGES) && pos->state == 0) {
			/* Just entered a diff-set */
			pos->hi = pos->p;
			pos->state = 2;
		} else if (!(mode2 & CHANGES) && pos->state) {
			/* Come to the end (start) of a diff-set */
			switch (pos->state) {
			case 1:
				/* finished final pass */
				pos->state = 0;
				break;
			case 2:
				/* Need to record the start */
				pos->lo = prv;
				/* time for another pass */
				pos->p = pos->hi;
				pos->state--;
				break;
			}
		}
		mask = ORIG|RESULT|BEFORE|AFTER;
		switch (pos->state) {
		case 1:
			mask &= ~(RESULT|AFTER);
			break;
		case 2:
			mask &= ~(ORIG|BEFORE);
			break;
		}
	} while (visible(mode&mask, m, pos) < 0);
}

/* blank a whole row of display */
static void blank(int row, int start, int cols, unsigned int attr)
{
	(void)attrset(attr);
	move(row, start);
	while (cols-- > 0)
		addch(' ');
}

/* search of a string on one display line.  If found, update the
 * cursor.
 */

static int mcontains(struct mpos pos,
		     struct file fm, struct file fb, struct file fa,
		     struct merge *m,
		     int mode, char *search, struct cursor *curs,
		     int dir, int ignore_case)
{
	/* See if any of the files, between start of this line and here,
	 * contain the search string.
	 * However this is modified by dir:
	 *  -2: find last match *before* curs
	 *  -1: find last match at-or-before curs
	 *   1: find first match at-or-after curs
	 *   2: find first match *after* curs
	 *
	 * We only test for equality with curs, so if it is on a different
	 * line it will not be found and everything is before/after.
	 * As we search from end-of-line to start we find the last
	 * match first.
	 * For a forward search, we stop when we find curs.
	 * For a backward search, we forget anything found when we find curs.
	 */
	struct elmnt e;
	int found = 0;
	struct mp mp;
	int o;
	int len = strlen(search);

	do {
		e = prev_melmnt(&pos.p, fm, fb, fa, m);
		if (e.start && e.start[0]) {
			int i;
			int curs_i;
			if (same_mp(pos.p, curs->pos))
				curs_i = curs->offset;
			else
				curs_i = -1;
			for (i = e.len-1; i >= 0; i--) {
				if (i == curs_i && dir == -1)
					/* next match is the one we want */
					found = 0;
				if (i == curs_i && dir == 2)
					/* future matches not accepted */
					goto break_while;
				if ((!found || dir > 0) &&
				    (ignore_case ? strncasecmp : strncmp)
				    (e.start+i, search, len) == 0) {
					mp = pos.p;
					o = i;
					found = 1;
				}
				if (i == curs_i && dir == -2)
					/* next match is the one we want */
					found = 0;
				if (i == curs_i && dir == 1)
					/* future matches not accepted */
					goto break_while;
			}
		}
	} while (e.start != NULL &&
		 (!ends_line(e)
		  || visible(mode, m, &pos) == -1));
break_while:
	if (found) {
		curs->pos = mp;
		curs->offset = o;
	}
	return found;
}

/* Drawing the display window.
 * There are 7 different ways we can display the data, each
 * of which can be configured by a keystroke:
 *  o   original - just show the original file with no changes, but still
 *                 with highlights of what is changed or unmatched
 *  r   result   - show just the result of the merge.  Conflicts just show
 *                 the original, not the before/after options
 *  b   before   - show the 'before' stream of the patch
 *  a   after    - show the 'after' stream of the patch
 *  d   diff     - show just the patch, both before and after
 *  m   merge    - show the full merge with -+ sections for changes.
 *                 If point is in a wiggled or conflicted section the
 *                 window is split horizontally and the diff is shown
 *                 in the bottom window
 *  | sidebyside - two panes, left and right.  Left holds the merge,
 *                 right holds the diff.  In the case of a conflict,
 *                 left holds orig/after, right holds before/after
 *
 * The horizontal split for 'merge' mode is managed as follows.
 * - The window is split when we first visit a line that contains
 *   a wiggle or a conflict, and the second pane is removed when
 *   we next visit a line that contains no changes (is fully Unchanged).
 * - to display the second pane, we find a visible end-of-line in the
 *   (BEFORE|AFTER) mode at-or-before the current end-of-line and
 *   the we centre that line.
 * - We need to rewind to an unchanged section, and wind forward again
 *   to make sure that 'lo' and 'hi' are set properly.
 * - every time we move, we redraw the second pane (see how that goes).
 */

/* draw_mside draws one text line or, in the case of sidebyside, one side
 * of a textline.
 * The 'mode' tells us what to draw via the 'visible()' function.
 * It is one of ORIG RESULT BEFORE AFTER or ORIG|RESULT or BEFORE|AFTER
 * It may also have WIGGLED or CONFLICTED ored in to trigger extra highlights.
 * The desired cursor position is given in 'target' the actual end
 * cursor position (allowing e.g. for tabs) is returned in *colp.
 */
static void draw_mside(int mode, int row, int offset, int start, int cols,
		       struct file fm, struct file fb, struct file fa,
		       struct merge *m,
		       struct mpos pos,
		       struct cursor *curs)
{
	struct elmnt e;
	int col = 0;
	char tag;
	unsigned int tag_attr;
	int changed = 0;

	switch (pos.state) {
	default: /* keep compiler happy */
	case 0: /* unchanged line */
		tag = ' ';
		tag_attr = A_NORMAL;
		break;
	case 1: /* 'before' text */
		tag = '-';
		tag_attr = a_delete;
		if ((mode & ORIG) && (mode & CONFLICTED)) {
			tag = '|';
			tag_attr = a_delete | A_REVERSE;
		}
		mode &= (ORIG|BEFORE);
		break;
	case 2: /* the 'after' part */
		tag = '+';
		tag_attr = a_added;
		mode &= (AFTER|RESULT);
		break;
	}

	if (visible(mode, m, &pos) < 0) {
		/* Not visible, just draw a blank */
		blank(row, offset, cols, a_void);
		if (curs) {
			curs->width = -1;
			curs->col = 0;
			curs->pos = pos.p;
			curs->offset = 0;
		}
		return;
	}

	(void)attrset(tag_attr);
	mvaddch(row, offset, tag);
	offset++;
	cols--;
	(void)attrset(A_NORMAL);

	if (check_line(pos, fm, fb, fa, m, mode))
		changed = 1;

	/* find previous visible newline, or start of file */
	do
		e = prev_melmnt(&pos.p, fm, fb, fa, m);
	while (e.start != NULL &&
	       (!ends_line(e) ||
		visible(mode, m, &pos) == -1));

	while (1) {
		unsigned char *c;
		unsigned int attr;
		int highlight_space;
		int l;
		e = next_melmnt(&pos.p, fm, fb, fa, m);
		if (!e.start)
			break;

		if (visible(mode, m, &pos) == -1)
			continue;
		if (e.start[0] == 0)
			break;
		c = (unsigned char *)e.start - e.prefix;
		highlight_space = 0;
		attr = visible(mode, m, &pos);
		if ((attr == a_unmatched || attr == a_extra) &&
		    changed)
			/* Only highlight spaces if there is a tab nearby */
			for (l = 0; l < e.plen + e.prefix; l++)
				if (c[l] == '\t')
					highlight_space = 1;
		if (!highlight_space && (c[0] == ' ' || c[0] == '\t')) {
			/* always highlight space/tab at end-of-line */
			struct mp nxt = pos.p;
			struct elmnt nxte = next_melmnt(&nxt, fm, fb, fa, m);
			if (nxte.start[0] == '\n')
				highlight_space = 1;
		}
		for (l = 0; l < e.plen + e.prefix; l++) {
			int scol = col;
			if (*c == '\n')
				break;
			(void)attrset(attr);
			if (*c >= ' ' && *c != 0x7f) {
				if (highlight_space)
					(void)attrset(attr|A_REVERSE);
				if (col >= start && col < start+cols)
					mvaddch(row, col-start+offset, *c);
				col++;
			} else if (*c == '\t') {
				if (highlight_space)
					(void)attrset(attr|A_UNDERLINE);
				do {
					if (col >= start && col < start+cols) {
						mvaddch(row, col-start+offset, ' ');
					} col++;
				} while ((col&7) != 0);
			} else {
				if (col >= start && col < start+cols)
					mvaddch(row, col-start+offset, '?');
				col++;
			}
			if (curs) {
				if (curs->target >= 0) {
					if (curs->target < col) {
						/* Found target column */
						curs->pos = pos.p;
						curs->offset = l;
						curs->col = scol;
						if (scol >= start + cols)
							/* Didn't appear on screen */
							curs->width = 0;
						else
							curs->width = col - scol;
						curs = NULL;
					}
				} else if (l == curs->offset &&
					   same_mp(pos.p, curs->pos)) {
					/* Found the pos */
					curs->target = scol;
					curs->col = scol;
					if (scol >= start + cols)
						/* Didn't appear on screen */
						curs->width = 0;
					else
						curs->width = col - scol;
					curs = NULL;
				}
			}
			c++;
		}
		if ((ends_line(e)
		     && visible(mode, m, &pos) != -1))
			break;
	}

	/* We have reached the end of visible line, or end of file */
	if (curs) {
		curs->col = col;
		if (col >= start + cols)
			curs->width = 0;
		else
			curs->width = -1; /* end of line */
		if (curs->target >= 0) {
			curs->pos = pos.p;
			curs->offset = 0;
		} else if (same_mp(pos.p, curs->pos))
			curs->target = col;
	}
	if (col < start)
		col = start;
	if (e.start && e.start[0] == 0) {
		char b[100];
		struct elmnt e1;
		if (pos.p.s == 2 && m[pos.p.m].type == Extraneous) {
			int A, B, C, D, E, F;
			e1 = fb.list[m[pos.p.m].b + pos.p.o];
			sscanf(e1.start+1, "%d %d %d", &A, &B, &C);
			sscanf(e.start+1, "%d %d %d", &D, &E, &F);
			snprintf(b, sizeof(b), "@@ -%d,%d +%d,%d @@%s", B, C, E, F, e1.start+18);
			(void)attrset(a_sep);
		} else {
			(void)attrset(visible(mode, m, &pos));
			sprintf(b, "<%.17s>", e.start+1);
		}
		mvaddstr(row, col-start+offset, b);
		col += strlen(b);
	}
	blank(row, col-start+offset, start+cols-col,
	      e.start
	      ? (unsigned)visible(mode, m, &pos)
	      : A_NORMAL);
}

/* Draw either 1 or 2 sides depending on the mode. */

static void draw_mline(int mode, int row, int start, int cols,
		       struct file fm, struct file fb, struct file fa,
		       struct merge *m,
		       struct mpos pos,
		       struct cursor *curs)
{
	/*
	 * Draw the left and right images of this line
	 * One side might be a_blank depending on the
	 * visibility of this newline
	 */
	int lcols, rcols;

	mode |= check_line(pos, fm, fb, fa, m, mode);

	if ((mode & (BEFORE|AFTER)) &&
	    (mode & (ORIG|RESULT))) {

		lcols = (cols-1)/2;
		rcols = cols - lcols - 1;

		(void)attrset(A_STANDOUT);
		mvaddch(row, lcols, '|');

		draw_mside(mode&~(BEFORE|AFTER), row, 0, start, lcols,
			   fm, fb, fa, m, pos, curs && !curs->alt ? curs : NULL);

		draw_mside(mode&~(ORIG|RESULT), row, lcols+1, start, rcols,
			   fm, fb, fa, m, pos, curs && curs->alt ? curs : NULL);
	} else
		draw_mside(mode, row, 0, start, cols,
			   fm, fb, fa, m, pos, curs);
}

static char *merge_help[] = {
	"This view shows the merge of the patch with the",
	"original file.  It is like a full-context diff showing",
	"removed lines with a '-' prefix and added lines with a",
	"'+' prefix.",
	"In cases where a patch chunk could not be successfully",
	"applied, the original text is prefixed with a '|', and",
	"the text that the patch wanted to add is prefixed with",
	"a '+'.",
	"When the cursor is over such a conflict, or over a chunk",
	"which required wiggling to apply (i.e. there was unmatched",
	"text in the original, or extraneous unchanged text in",
	"the patch), the terminal is split and the bottom pane is",
	"use to display the part of the patch that applied to",
	"this section of the original.  This allows you to confirm",
	"that a wiggled patch applied correctly, and to see",
	"why there was a conflict",
	NULL
};
static char *diff_help[] = {
	"This is the 'diff' or 'patch' view.  It shows",
	"only the patch that is being applied without the",
	"original to which it is being applied.",
	"Underlined text indicates parts of the patch which",
	"resulted in a conflict when applied to the",
	"original.",
	NULL
};
static char *orig_help[] = {
	"This is the 'original' view which simply shows",
	"the original file before applying the patch.",
	"Sections of code that would be changed by the patch",
	"are highlighted in red.",
	NULL
};
static char *result_help[] = {
	"This is the 'result' view which shows just the",
	"result of applying the patch.  When a conflict",
	"occurred this view does not show the full conflict",
	"but only the 'after' part of the patch.  To see",
	"the full conflict, use the 'merge' or 'sidebyside'",
	"views.",
	NULL
};
static char *before_help[] = {
	"This view shows the 'before' section of a patch.",
	"It allows the expected match text to be seen uncluttered",
	"by text that is meant to replaced it.",
	"Red text is text that will be removed by the patch",
	NULL
};
static char *after_help[] = {
	"This view shows the 'after' section of a patch.",
	"It allows the intended result to be seen uncluttered",
	"by text that was meant to be matched and replaced.",
	"Green text is text that was added by the patch - it",
	"was not present in the 'before' part of the patch",
	NULL
};
static char *sidebyside_help[] = {
	"This is the Side By Side view of a patched file.",
	"The left side shows the original and the result.",
	"The right side shows the patch which was applied",
	"and lines up with the original/result as much as",
	"possible.",
	"",
	"Where one side has no line which matches the",
	"other side it is displayed as a solid colour in the",
	"yellow family (depending on your terminal window).",
	NULL
};
static char *merge_window_help[] = {
	"  Highlight Colours and Keystroke commands",
	"",
	"In all different views of a merge, highlight colours",
	"are used to show which parts of lines were added,",
	"removed, already changed, unchanged or in conflict.",
	"Colours and their use are:",
	" normal              unchanged text",
	" red                 text that was removed or changed",
	" green               text that was added or the result",
	"                     of a change",
	" yellow background   used in side-by-side for a line",
	"                     which has no match on the other",
	"                     side",
	" blue                text in the original which did not",
	"                     match anything in the patch",
	" cyan                text in the patch which did not",
	"                     match anything in the original",
	" cyan background     already changed text: the result",
	"                     of the patch matches the original",
	" underline           remove or added text can also be",
	"                     underlined indicating that it",
	"                     was involved in a conflict",
	"",
	"While viewing a merge various keystroke commands can",
	"be used to move around and change the view.  Basic",
	"movement commands from both 'vi' and 'emacs' are",
	"available:",
	"",
	" p control-p k UP    Move to previous line",
	" n control-n j DOWN  Move to next line",
	" l LEFT              Move one char to right",
	" h RIGHT             Move one char to left",
	" / control-s         Enter incremental search mode",
	" control-r           Enter reverse-search mode",
	" control-g           Search again",
	" ?                   Display help message",
	" ESC-<  0-G          Go to start of file",
	" ESC->  G            Go to end of file",
	" q                   Return to list of files or exit",
	" S                   Arrange for merge to be saved on exit",
	" control-C           Disable auto-save-on-exit",
	" control-L           recenter current line",
	" control-V SPACE     page down",
	" ESC-v	  BACKSPC     page up",
	" N                   go to next patch chunk",
	" P                   go to previous patch chunk",
	" C                   go to next conflicted chunk",
	" C-X-o   O           move cursor to alternate pane",
	" ^ control-A         go to start of line",
	" $ control-E         go to end of line",
	"",
	" a                   display 'after' view",
	" b                   display 'before' view",
	" o                   display 'original' view",
	" r                   display 'result' view",
	" d                   display 'diff' or 'patch' view",
	" m                   display 'merge' view",
	" |                   display side-by-side view",
	"",
	" I                   toggle whether spaces are ignored",
	"                     when matching text.",
	" x                   toggle ignoring of current Changed,",
	"                     Conflict, or Unmatched item",
	" c                   toggle accepting of result of conflict",
	" X                   toggle ignored of all Change, Conflict",
	"                     and Unmatched items in current line",
	NULL
};
static char *save_query[] = {
	"",
	"You have modified the merge.",
	"Would you like to save it?",
	" Y = save the modified merge",
	" N = discard modifications, don't save",
	" Q = return to viewing modified merge",
	NULL
};

static char *toggle_ignore[] = {
	"",
	"You have modified the merge.",
	"Toggling ignoring of spaces will discard changes.",
	"Do you want to proceed?",
	" Y = discard changes and toggle ignoring of spaces",
	" N = keep changes, don't toggle",
	NULL
};

static int merge_window(struct plist *p, FILE *f, int reverse, int replace,
			int selftest, int ignore_blanks)
{
	/* Display the merge window in one of the selectable modes,
	 * starting with the 'merge' mode.
	 *
	 * Newlines are the key to display.
	 * 'pos' is always a visible newline (or eof).
	 * In sidebyside mode it might only be visible on one side,
	 * in which case the other side will be blank.
	 * Where the newline is visible, we rewind the previous visible
	 * newline visible and display the stuff in between
	 *
	 * A 'position' is a struct mpos
	 */

	struct stream sm, sb, sa, sp; /* main, before, after, patch */
	struct file fm, fb, fa;
	struct csl *csl1, *csl2;
	struct ci ci;
	int ch; /* count of chunks */
	/* Always refresh the current line.
	 * If refresh == 1, refresh all lines.  If == 2, clear first
	 */
	int refresh = 2;
	int rows = 0, cols = 0;
	int splitrow = -1; /* screen row for split - diff appears below */
	int lastrow = 0; /* end of screen, or just above 'splitrow' */
	int i, c, cswitch;
	MEVENT mevent;
	int mode = ORIG|RESULT;
	int mmode = mode; /* Mode for moving - used when in 'other' pane */
	char *modename = "merge";
	char **modehelp = merge_help;
	char *mesg = NULL;

	int row, start = 0;
	int trow; /* screen-row while searching.  If we cannot find,
		   * we forget this number */
	struct cursor curs;
	struct mpos pos;  /* current point */
	struct mpos tpos, /* temp point while drawing lines above and below pos */
		toppos,   /* pos at top of screen - for page-up */
		botpos;   /* pos at bottom of screen - for page-down */
	struct mpos vpos, tvpos, vispos;
	int botrow = 0;
	int meta = 0,     /* mode for multi-key commands- SEARCH or META */
		tmeta;
	int num = -1,     /* numeric arg being typed. */
		tnum;
	int changes = 0; /* If any edits have been made to the merge */
	int answer;	/* answer to 'save changes?' question */
	int do_mark;
	struct elmnt e;
	char search[80];  /* string we are searching for */
	unsigned int searchlen = 0;
	int search_notfound = 0;
	int searchdir = 0;
	/* ignore_case:
	 *  0 == no
	 *  1 == no because there are upper-case chars
	 *  2 == yes as there are no upper-case chars
	 *  3 == yes
	 */
	int ignore_case = 2;
	/* We record all the places we find so 'backspace'
	 * can easily return to the previous one
	 */
	struct search_anchor {
		struct search_anchor *next;
		struct mpos pos;
		struct cursor curs;
		int notfound;
		int row, start;
		unsigned int searchlen;
	} *anchor = NULL;

	if (selftest) {
		intr_kills = 1;
		selftest = 1;
	}

	if (f == NULL) {
		if (!p->is_merge) {
			/* three separate files */
			sm = load_file(p->file);
			sb = load_file(p->before);
			sa = load_file(p->after);
		} else {
			/* One merge file */
			sp = load_file(p->file);
			if (reverse)
				split_merge(sp, &sm, &sa, &sb);
			else
				split_merge(sp, &sm, &sb, &sa);
			free(sp.body);
		}
		ch = 0;
	} else {
		sp = load_segment(f, p->start, p->end);
		if (p->is_merge) {
			if (reverse)
				split_merge(sp, &sm, &sa, &sb);
			else
				split_merge(sp, &sm, &sb, &sa);
			ch = 0;
		} else {
			if (reverse)
				ch = split_patch(sp, &sa, &sb);
			else
				ch = split_patch(sp, &sb, &sa);

			sm = load_file(p->file);
		}
		free(sp.body);
	}
	if (!sm.body || !sb.body || !sa.body) {
		free(sm.body);
		free(sb.body);
		free(sa.body);
		term_init(1);
		if (!sm.body)
			help_window(help_missing, NULL, 0);
		else
			help_window(help_corrupt, NULL, 0);
		endwin();
		return 0;
	}
	/* FIXME check for errors in the stream */
	fm = split_stream(sm, ByWord | ignore_blanks);
	fb = split_stream(sb, ByWord | ignore_blanks);
	fa = split_stream(sa, ByWord | ignore_blanks);

	if (ch)
		csl1 = pdiff(fm, fb, ch);
	else
		csl1 = diff(fm, fb);
	csl2 = diff_patch(fb, fa);

	ci = make_merger(fm, fb, fa, csl1, csl2, 0, 1, 0);
	for (i = 0; ci.merger[i].type != End; i++)
		ci.merger[i].oldtype = ci.merger[i].type;

	term_init(!selftest);

	row = 1;
	pos.p.m = 0; /* merge node */
	pos.p.s = 0; /* stream number */
	pos.p.o = -1; /* offset */
	pos.p.lineno = 1;
	pos.state = 0;
	next_mline(&pos, fm, fb, fa, ci.merger, mode);
	memset(&curs, 0, sizeof(curs));
	vpos = pos;
	while (1) {
		unsigned int next;
		if (refresh >= 2) {
			clear();
			refresh = 1;
		}
		if (row < 1 || row >= lastrow)
			refresh = 1;
		if (curs.alt)
			refresh = 1;

		if (mode == (ORIG|RESULT)) {
			int cmode = check_line(pos, fm, fb, fa, ci.merger, mode);
			if (cmode & (WIGGLED | CONFLICTED)) {
				if (splitrow < 0) {
					splitrow = (rows+1)/2;
					lastrow = splitrow - 1;
					refresh = 1;
				}
			} else if (!curs.alt && splitrow >= 0) {
				splitrow = -1;
				lastrow = rows-1;
				refresh = 1;
			}
		} else if (splitrow >= 0) {
			splitrow = -1;
			lastrow = rows-1;
			refresh = 1;
		}

		if (refresh) {
			getmaxyx(stdscr, rows, cols);
			rows--; /* keep last row clear */
			if (splitrow >= 0) {
				splitrow = (rows+1)/2;
				lastrow = splitrow - 1;
			} else
				lastrow =  rows - 1;

			if (row < -3)
				row = lastrow/2+1;
			if (row < 1)
				row = 1;
			if (row > lastrow+3)
				row = lastrow/2+1;
			if (row >= lastrow)
				row = lastrow-1;
		}
		if (getenv("WIGGLE_VTRACE")) {
			char b[100];
			char *e, e2[7];
			int i;
			switch (vpos.p.s) {
			default: /* keep compiler happy */
			case 0:
				e = fm.list[ci.merger[vpos.p.m].a + vpos.p.o].start;
				break;
			case 1:
				e = fb.list[ci.merger[vpos.p.m].b + vpos.p.o].start;
				break;
			case 2:
				e = fa.list[ci.merger[vpos.p.m].c + vpos.p.o].start;
				break;
			}
			for (i = 0; i < 6; i++) {
				e2[i] = e[i];
				if (e2[i] < 32 || e2[i] >= 127)
					e2[i] = '?';
			}
			sprintf(b, "st=%d str=%d o=%d m=%d mt=%s(%d,%d,%d) ic=%d  <%.3s>", vpos.state,
				vpos.p.s, vpos.p.o,
				vpos.p.m, typenames[ci.merger[vpos.p.m].type],
				ci.merger[vpos.p.m].al,
				ci.merger[vpos.p.m].bl,
				ci.merger[vpos.p.m].cl,
				ci.merger[vpos.p.m].in_conflict,
				e2
				);
			(void)attrset(A_NORMAL);
			mvaddstr(0, 50, b);
			clrtoeol();
		}

		/* Always refresh the line */
		while (start > curs.target) {
			start -= 8;
			refresh = 1;
		}
		if (start < 0)
			start = 0;
		vispos = pos; /* visible position - if cursor is in
			       * alternate pane, pos might not be visible
			       * in main pane. */
		if (check_line(vispos, fm, fb, fa, ci.merger, mode)
		    & CHANGES) {
			if (vispos.state == 0)
				vispos.state = 1;
		} else {
			vispos.state = 0;
		}

		if (visible(mode, ci.merger, &vispos) < 0)
			prev_mline(&vispos, fm, fb, fa, ci.merger, mode);
		if (!curs.alt)
			pos= vispos;
	retry:
		draw_mline(mode, row, start, cols, fm, fb, fa, ci.merger,
			   vispos, (splitrow >= 0 && curs.alt) ? NULL : &curs);
		if (curs.width == 0 && start < curs.col) {
			/* width == 0 implies it appear after end-of-screen */
			start += 8;
			refresh = 1;
			goto retry;
		}
		if (curs.col < start) {
			start -= 8;
			refresh = 1;
			if (start < 0)
				start = 0;
			goto retry;
		}
		if (refresh) {
			refresh = 0;

			tpos = vispos;

			for (i = row-1; i >= 1 && tpos.p.m >= 0; ) {
				prev_mline(&tpos, fm, fb, fa, ci.merger, mode);
				draw_mline(mode, i--, start, cols,
					   fm, fb, fa, ci.merger,
					   tpos, NULL);

			}
			if (i) {
				row -= (i+1);
				refresh = 1;
				goto retry;
			}
			toppos = tpos;
			while (i >= 1)
				blank(i--, 0, cols, a_void);
			tpos = vispos;
			for (i = row; i <= lastrow && ci.merger[tpos.p.m].type != End; ) {
				draw_mline(mode, i++, start, cols,
					   fm, fb, fa, ci.merger,
					   tpos, NULL);
				next_mline(&tpos, fm, fb, fa, ci.merger, mode);
			}
			botpos = tpos; botrow = i;
			while (i <= lastrow)
				blank(i++, 0, cols, a_void);
		}

		if (splitrow >= 0) {
			struct mpos spos = pos;
			int smode = BEFORE|AFTER;
			int srow = (rows + splitrow)/2;
			if (check_line(spos, fm, fb, fa, ci.merger, smode)
			    & CHANGES) {
				if (spos.state == 0)
					spos.state = 1;
			} else {
				spos.state = 0;
			}
			if (visible(smode, ci.merger, &spos) < 0)
				prev_mline(&spos, fm, fb, fa, ci.merger, smode);
			/* Now hi/lo might be wrong, so lets fix it. */
			tpos = spos;
			if (spos.state)
				/* 'hi' might be wrong so we mustn't depend
				 * on it while walking back.  So set state
				 * to 1 to avoid ever testing it.
				 */
				spos.state = 1;
			while (spos.p.m >= 0 && spos.state != 0)
				prev_mline(&spos, fm, fb, fa, ci.merger, smode);
			while (!same_mpos(spos, tpos))
				next_mline(&spos, fm, fb, fa, ci.merger, smode);

			(void)attrset(a_sep);
			for (i = 0; i < cols; i++)
				mvaddstr(splitrow, i, "-");

			tpos = spos;
			for (i = srow-1; i > splitrow; i--) {
				prev_mline(&tpos, fm, fb, fa, ci.merger, smode);
				draw_mline(smode, i, start, cols, fm, fb, fa, ci.merger,
					   tpos, NULL);
			}
			while (i > splitrow)
				blank(i--, 0, cols, a_void);
			tpos = spos;
			for (i = srow;
			     i < rows && ci.merger[tpos.p.m].type != End;
			     i++) {
				draw_mline(smode, i, start, cols, fm, fb, fa, ci.merger,
					   tpos,
					   (i == srow && curs.alt) ? &curs : NULL);
				next_mline(&tpos, fm, fb, fa, ci.merger, smode);
			}
			while (i < rows)
				blank(i++, 0, cols, a_void);
		}
		/* Now that curs is accurate, report the type */
		{
			int l = 0;
			char buf[100];
			snprintf(buf, 100, "File: %s%s Mode: %s",
				p->file, reverse ? " - reversed" : "", modename);
			(void)attrset(A_BOLD);
			mvaddstr(0, 0, buf);
			(void)attrset(A_NORMAL);
			if (ignore_blanks)
				addstr(" (ignoring blanks)");
			clrtoeol();
			(void)attrset(A_BOLD);
			if (ci.merger[curs.pos.m].type != ci.merger[curs.pos.m].oldtype)
				l = snprintf(buf, sizeof(buf), "%s->", typenames[ci.merger[curs.pos.m].oldtype]);
			snprintf(buf+l, sizeof(buf)-l, "%s ln:%d",
				 typenames[ci.merger[curs.pos.m].type],
				 (pos.p.lineno-1)/2);
			mvaddstr(0, cols - strlen(buf) - 1, buf);
		}
#define META(c) ((c)|0x1000)
#define	SEARCH(c) ((c)|0x2000)
#define CTRLX(c) ((c)|0x4000)
		move(rows, 0);
		(void)attrset(A_NORMAL);
		if (mesg) {
			attrset(A_REVERSE);
			addstr(mesg);
			mesg = NULL;
			attrset(A_NORMAL);
		}
		if (num >= 0) {
			char buf[10];
			snprintf(buf, 10, "%d ", num);
			addstr(buf);
		}
		if (meta & META(0))
			addstr("ESC...");
		if (meta & CTRLX(0))
			addstr("C-x ");
		if (meta & SEARCH(0)) {
			if (searchdir < 0)
				addstr("Backwards ");
			addstr("Search: ");
			addstr(search);
			if (search_notfound)
				addstr(" - Not Found.");
			search_notfound = 0;
		}
		clrtoeol();
		/* '+1' to skip over the leading +/-/| char */
		if (curs.alt && splitrow > 0)
			move((rows + splitrow)/2, curs.col - start + 1);
		else if (curs.alt && ((mode & (BEFORE|AFTER)) &&
				      (mode & (ORIG|RESULT))))
			move(row, curs.col-start + (cols-1)/2+2);
		else
			move(row, curs.col-start+1);
		switch (selftest) {
		case 0:
			c = getch(); break;
		case 1:
			c = 'n'; break;
		case 2:
			c = 'q'; break;
		}
		tmeta = meta; meta = 0;
		tnum = num; num = -1;
		tvpos = vpos; vpos = pos;
		cswitch = c | tmeta;
		/* Handle some ranges */
		/* case '0' ... '9': */
		if (cswitch >= '0' && cswitch <= '9')
			cswitch = '0';
		/* case SEARCH(' ') ... SEARCH('~'): */
		if (cswitch >= SEARCH(' ') && cswitch <= SEARCH('~'))
			cswitch = SEARCH(' ');

		switch (cswitch) {
		case 27: /* escape */
		case META(27):
			meta = META(0);
			break;

		case 'X'-64:
		case META('X'-64):
			meta = CTRLX(0);
			break;

		case META('<'): /* start of file */
		start:
			tpos = pos; row++;
			do {
				pos = tpos; row--;
				prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (tpos.p.m >= 0);
			if (row <= 0)
				row = 0;
			break;
		case META('>'): /* end of file */
		case 'G':
			if (tnum >= 0)
				goto start;
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (ci.merger[tpos.p.m].type != End);
			if (row >= lastrow)
				row = lastrow;
			break;
		case '0': /* actually '0'...'9' */
			if (tnum < 0)
				tnum = 0;
			num = tnum*10 + (c-'0');
			break;
		case 'C'-64:
			if (replace)
				mesg = "Autosave disabled";
			else
				mesg = "Use 'q' to quit";
			replace = 0;
			break;
		case 'S':
			mesg = "Will auto-save on exit, using Ctrl-C to cancel";
			replace = 1;
			break;
		case 'q':
			refresh = 2;
			answer = 0;
			if (replace)
				answer = 1;
			else if (changes)
				answer = help_window(save_query, NULL, 1);
			if (answer < 0)
				break;
			if (answer) {
				p->wiggles = 0;
				p->conflicts = isolate_conflicts(
					fm, fb, fa, csl1, csl2, 0,
					ci.merger, 0, &p->wiggles);
				p->chunks = p->conflicts;
				save_merge(fm, fb, fa, ci.merger,
					   p->file, !p->is_merge);
			}
			free(sm.body);
			free(sb.body);
			free(sa.body);
			free(fm.list);
			free(fb.list);
			free(fa.list);
			free(csl1);
			free(csl2);
			free(ci.merger);
			endwin();
			return answer;

		case 'I': /* Toggle ignoring of spaces */
			if (changes) {
				refresh = 2;
				answer = help_window(toggle_ignore, NULL, 1);
				if (answer <= 0)
					break;
				changes = 0;
			}
			free(fm.list);
			free(fb.list);
			free(fa.list);
			free(csl1);
			free(csl2);
			free(ci.merger);
			ignore_blanks = ignore_blanks ? 0 : IgnoreBlanks;
			fm = split_stream(sm, ByWord | ignore_blanks);
			fb = split_stream(sb, ByWord | ignore_blanks);
			fa = split_stream(sa, ByWord | ignore_blanks);

			if (ch)
				csl1 = pdiff(fm, fb, ch);
			else
				csl1 = diff(fm, fb);
			csl2 = diff_patch(fb, fa);

			ci = make_merger(fm, fb, fa, csl1, csl2, 0, 1, 0);
			for (i = 0; ci.merger[i].type != End; i++)
				ci.merger[i].oldtype = ci.merger[i].type;

		{
			int ln = pos.p.lineno;
			pos.p.m = 0; /* merge node */
			pos.p.s = 0; /* stream number */
			pos.p.o = -1; /* offset */
			pos.p.lineno = 1;
			pos.state = 0;
			next_mline(&pos, fm, fb, fa, ci.merger, mode);
			memset(&curs, 0, sizeof(curs));
			while (pos.p.lineno < ln && ci.merger[pos.p.m].type != End)
				next_mline(&pos, fm, fb, fa, ci.merger, mode);
			vpos = pos;
		}
			refresh = 2;
			break;

		case '/':
		case 'S'-64:
			/* incr search forward */
			meta = SEARCH(0);
			searchlen = 0;
			search[searchlen] = 0;
			searchdir = 1;
			break;
		case '\\':
		case 'R'-64:
			/* incr search backwards */
			meta = SEARCH(0);
			searchlen = 0;
			search[searchlen] = 0;
			searchdir = -1;
			break;
		case SEARCH('G'-64):
		case SEARCH('S'-64):
		case SEARCH('R'-64):
			/* search again */
			if ((c|tmeta) == SEARCH('R'-64))
				searchdir = -2;
			else
				searchdir = 2;
			meta = SEARCH(0);
			tpos = pos; trow = row;
			goto search_again;

		case SEARCH('H'-64):
		case SEARCH(KEY_BACKSPACE):
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
				start = a->start;
				curs = a->curs;
				curs.target = -1;
				search_notfound = a->notfound;
				searchlen = a->searchlen;
				search[searchlen] = 0;
				free(a);
				refresh = 1;
			}
			break;
		case SEARCH(' '): /* actually ' '...'~' */
		case SEARCH('\t'):
			meta = SEARCH(0);
			if (searchlen < sizeof(search)-1)
				search[searchlen++] = c & (0x7f);
			search[searchlen] = 0;
			tpos = pos; trow = row;
		search_again:
			search_notfound = 1;
			if (ignore_case == 1 || ignore_case == 2) {
				unsigned int i;
				ignore_case = 2;
				for (i=0; i < searchlen; i++)
					if (isupper(search[i])) {
						ignore_case = 1;
						break;
					}
			}
			do {
				if (mcontains(tpos, fm, fb, fa, ci.merger,
					      mmode, search, &curs, searchdir,
					      ignore_case >= 2)) {
					curs.target = -1;
					pos = tpos;
					row = trow;
					search_notfound = 0;
					break;
				}
				if (searchdir < 0) {
					trow--;
					prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				} else {
					trow++;
					next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				}
			} while (tpos.p.m >= 0 && ci.merger[tpos.p.m].type != End);
			searchdir /= abs(searchdir);

			break;
		case 'L'-64:
			refresh = 2;
			row = lastrow / 2;
			break;

		case ' ':
		case 'V'-64: /* page down */
			pos = botpos;
			if (botrow <= lastrow) {
				row = botrow;
				if (selftest == 1)
					selftest = 2;
			} else
				row = 2;
			refresh = 1;
			break;
		case KEY_BACKSPACE:
		case META('v'): /* page up */
			pos = toppos;
			row = lastrow-1;
			refresh = 1;
			break;

		case KEY_MOUSE:
			if (getmouse(&mevent) != OK)
				break;
			/* First see if this is on the 'other' pane */
			if (splitrow > 0) {
				/* merge mode, top and bottom */
				if ((curs.alt && mevent.y < splitrow) ||
				    (!curs.alt && mevent.y > splitrow)) {
					goto other_pane;
				}
			} else if (mode == (ORIG|RESULT|BEFORE|AFTER)) {
				/* side-by-side mode */
				if ((curs.alt && mevent.x < cols/2) ||
				    (!curs.alt && mevent.x > cols/2)) {
					goto other_pane;
				}
			}
			/* Now try to find the right line */
			if (splitrow < 0 || !curs.alt)
				trow = row;
			else
				trow = (rows + splitrow)/2;
			while (trow > mevent.y) {
				tpos = pos;
				prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				if (tpos.p.m >= 0) {
					pos = tpos;
					trow--;
				} else
					break;
			}
			while (trow < mevent.y) {
				tpos = pos;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				if (ci.merger[tpos.p.m].type != End) {
					pos = tpos;
					trow++;
				} else
					break;
			}
			if (splitrow < 0 || !curs.alt)
				/* it is OK to change the row */
				row = trow;

			/* Now set the target column */
			if (mode == (ORIG|RESULT|BEFORE|AFTER) &&
			    curs.alt)
				curs.target = start + mevent.x - cols / 2 - 1;
			else
				curs.target = start + mevent.x - 1;
			break;
		case 'j':
		case 'n':
		case 'N'-64:
		case KEY_DOWN:
			if (tnum < 0)
				tnum = 1;
			for (; tnum > 0 ; tnum--) {
				tpos = pos;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				if (ci.merger[tpos.p.m].type != End) {
					pos = tpos;
					row++;
				} else {
					if (selftest == 1)
						selftest = 2;
					break;
				}
			}
			break;
		case 'N':
			/* Next diff */
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (!(pos.state == 0
				   && (check_line(pos, fm, fb, fa, ci.merger, mmode)
				       & (CONFLICTED|WIGGLED)) == 0)
				 && ci.merger[tpos.p.m].type != End);
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (pos.state == 0
				 && (check_line(pos, fm, fb, fa, ci.merger, mmode)
				     & (CONFLICTED|WIGGLED)) == 0
				 && ci.merger[tpos.p.m].type != End);

			break;
		case 'C':
			/* Next conflict */
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (!(check_line(pos, fm, fb, fa, ci.merger, mmode)
				   & CONFLICTED) == 0
				 && ci.merger[tpos.p.m].type != End);
			tpos = pos; row--;
			do {
				pos = tpos; row++;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while ((check_line(pos, fm, fb, fa, ci.merger, mmode)
				  & CONFLICTED) == 0
				 && ci.merger[tpos.p.m].type != End);

			break;

		case 'P':
			/* Previous diff */
			tpos = pos; row++;
			do {
				pos = tpos; row--;
				prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (tpos.state == 0
				 && (check_line(tpos, fm, fb, fa, ci.merger, mmode)
				     & (CONFLICTED|WIGGLED)) == 0
				 && tpos.p.m >= 0);
			tpos = pos; row++;
			do {
				pos = tpos; row--;
				prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			} while (!(tpos.state == 0
				   && (check_line(tpos, fm, fb, fa, ci.merger, mmode)
				       & (CONFLICTED|WIGGLED)) == 0)
				 && tpos.p.m >= 0);
			break;

		case 'k':
		case 'p':
		case 'P'-64:
		case KEY_UP:
			if (tnum < 0)
				tnum = 1;
			for (; tnum > 0 ; tnum--) {
				tpos = pos;
				prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				if (tpos.p.m >= 0) {
					pos = tpos;
					row--;
				} else
					break;
			}
			break;

		case KEY_LEFT:
		case 'h':
			/* left */
			curs.target = curs.col - 1;
			if (curs.target < 0) {
				/* Try to go to end of previous line */
				tpos = pos;
				prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				if (tpos.p.m >= 0) {
					pos = tpos;
					row--;
					curs.pos = pos.p;
					curs.target = -1;
				} else
					curs.target = 0;
			}
			break;
		case KEY_RIGHT:
		case 'l':
			/* right */
			if (curs.width >= 0)
				curs.target = curs.col + curs.width;
			else {
				/* end of line, go to next */
				tpos = pos;
				next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
				if (ci.merger[tpos.p.m].type != End) {
					pos = tpos;
					curs.pos = pos.p;
					row++;
					curs.target = 0;
				}
			}
			break;

		case '^':
		case 'A'-64:
			/* Start of line */
			curs.target = 0;
			break;
		case '$':
		case 'E'-64:
			/* End of line */
			curs.target = 1000;
			break;

		case CTRLX('o'):
		case 'O':
		other_pane:
			curs.alt = !curs.alt;
			if (curs.alt && mode == (ORIG|RESULT))
				mmode = (BEFORE|AFTER);
			else
				mmode = mode;
			break;

		case 'a': /* 'after' view in patch window */
			if (mode == AFTER)
				goto set_merge;
			mode = AFTER; modename = "after"; modehelp = after_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;
		case 'b': /* 'before' view in patch window */
			if (mode == BEFORE)
				goto set_merge;
			mode = BEFORE; modename = "before"; modehelp = before_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;
		case 'o': /* 'original' view in the merge window */
			if (mode == ORIG)
				goto set_merge;
			mode = ORIG; modename = "original"; modehelp = orig_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;
		case 'r': /* the 'result' view in the merge window */
			if (mode == RESULT)
				goto set_merge;
			mode = RESULT; modename = "result"; modehelp = result_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;
		case 'd':
			if (mode == (BEFORE|AFTER))
				goto set_merge;
			mode = BEFORE|AFTER; modename = "diff"; modehelp = diff_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;
		case 'm':
		set_merge:
			mode = ORIG|RESULT; modename = "merge"; modehelp = merge_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;

		case '|':
			if (mode == (ORIG|RESULT|BEFORE|AFTER))
				goto set_merge;
			mode = ORIG|RESULT|BEFORE|AFTER; modename = "sidebyside"; modehelp = sidebyside_help;
			mmode = mode; curs.alt = 0;
			refresh = 3;
			break;

		case 'H': /* scroll window to the right */
			if (start > 0)
				start--;
			curs.target = start + 1;
			refresh = 1;
			break;
		case 'L': /* scroll window to the left */
			if (start < cols)
				start++;
			curs.target = start + 1;
			refresh = 1;
			break;

		case '<':
			prev_melmnt(&tvpos.p, fm, fb, fa, ci.merger);
			if (tvpos.p.m >= 0)
				vpos = tvpos;
			break;
		case '>':
			next_melmnt(&tvpos.p, fm, fb, fa, ci.merger);
			if (ci.merger[tvpos.p.m].type != End)
				vpos = tvpos;
			break;

		case 'x': /* Toggle rejecting of conflict.
			   * A 'Conflict' or 'Changed' becomes 'Unchanged'
			   * 'Unmatched' becomes 'Changed'
			   */
			if (ci.merger[curs.pos.m].oldtype == Conflict ||
			    ci.merger[curs.pos.m].oldtype == Changed)
				next = Unchanged;
			else if (ci.merger[curs.pos.m].oldtype == Unmatched)
				next = Changed;
			else
				break;

			if (ci.merger[curs.pos.m].type == next)
				ci.merger[curs.pos.m].type = ci.merger[curs.pos.m].oldtype;
			else
				ci.merger[curs.pos.m].type = next;
			p->conflicts = isolate_conflicts(
				fm, fb, fa, csl1, csl2, 0,
				ci.merger, 0, &p->wiggles);
			refresh = 1;
			changes = 1;
			break;

		case 'c': /* Toggle accepting of conflict.
			   * A 'Conflict' becomes 'Changed'
			   */
			if (ci.merger[curs.pos.m].oldtype != Conflict)
				break;

			if (ci.merger[curs.pos.m].type == Changed)
				ci.merger[curs.pos.m].type = ci.merger[curs.pos.m].oldtype;
			else
				ci.merger[curs.pos.m].type = Changed;
			p->conflicts = isolate_conflicts(
				fm, fb, fa, csl1, csl2, 0,
				ci.merger, 0, &p->wiggles);
			refresh = 1;
			changes = 1;
			break;

		case 'X': /* toggle where all Conflicts and Changeds
			   * in the current line are marked Unchanged.
			   * If any are not mark, mark them all, else
			   * un-mark them all.
			   */
			tpos = pos;
			do_mark = 0;
			do {
				if ((ci.merger[tpos.p.m].oldtype == Conflict ||
				     ci.merger[tpos.p.m].oldtype == Changed ||
				     ci.merger[tpos.p.m].oldtype == Unmatched)
				    && (ci.merger[tpos.p.m].type ==
					ci.merger[tpos.p.m].oldtype))
					do_mark = 1;
				e = prev_melmnt(&tpos.p, fm, fb, fa, ci.merger);
				if (tpos.p.m < 0)
					break;
			} while (!ends_line(e) ||
				 visible(mode & (RESULT|AFTER), ci.merger, &tpos) < 0);
			tpos = pos;
			do {
				if (ci.merger[tpos.p.m].oldtype == Conflict ||
				    ci.merger[tpos.p.m].oldtype == Changed ||
				    ci.merger[tpos.p.m].oldtype == Unmatched) {
					next = Unchanged;
					if (ci.merger[tpos.p.m].oldtype == Unmatched)
						next = Changed;
					if (do_mark)
						ci.merger[tpos.p.m].type = next;
					else
						ci.merger[tpos.p.m].type =
							ci.merger[tpos.p.m].oldtype;
				}
				e = prev_melmnt(&tpos.p, fm, fb, fa, ci.merger);
				if (tpos.p.m < 0)
					break;
			} while (!ends_line(e) ||
				 visible(mode & (RESULT|AFTER), ci.merger, &tpos) < 0);
			p->conflicts = isolate_conflicts(
				fm, fb, fa, csl1, csl2, 0,
				ci.merger, 0, &p->wiggles);
			refresh = 1;
			changes = 1;
			break;

		case '?':
			help_window(modehelp, merge_window_help, 0);
			refresh = 2;
			break;

		case KEY_RESIZE:
			refresh = 2;
			break;
		}

		if (meta == SEARCH(0)) {
			if (anchor == NULL ||
			    !same_mpos(anchor->pos, pos) ||
			    anchor->searchlen != searchlen ||
			    !same_mp(anchor->curs.pos, curs.pos)) {
				struct search_anchor *a = xmalloc(sizeof(*a));
				a->pos = pos;
				a->row = row;
				a->start = start;
				a->curs = curs;
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
		if (refresh == 3) {
			/* move backward and forward to make sure we
			 * are on a visible line
			 */
			tpos = pos;
			prev_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			if (tpos.p.m >= 0)
				pos = tpos;
			tpos = pos;
			next_mline(&tpos, fm, fb, fa, ci.merger, mmode);
			if (ci.merger[tpos.p.m].type != End)
				pos = tpos;
		}
	}
}

static int show_merge(char *origname, FILE *patch, int reverse,
		      int is_merge, char *before, char *after,
		      int replace, int selftest, int ignore_blanks)
{
	struct plist p;

	p.file = origname;
	if (patch) {
		p.start = 0;
		fseek(patch, 0, SEEK_END);
		p.end = ftell(patch);
		fseek(patch, 0, SEEK_SET);
	}
	p.calced = 0;
	p.is_merge = is_merge;
	p.before = before;
	p.after = after;

	freopen("/dev/null","w",stderr);
	return merge_window(&p, patch, reverse, replace, selftest,
			    ignore_blanks);
}

static void calc_one(struct plist *pl, FILE *f, int reverse,
		     int ignore_blanks)
{
	struct stream s1, s2;
	struct stream s = load_segment(f, pl->start, pl->end);
	struct stream sf;
	if (pl->is_merge) {
		if (reverse)
			split_merge(s, &sf, &s2, &s1);
		else
			split_merge(s, &sf, &s1, &s2);
		pl->chunks = 0;
	} else {
		sf = load_file(pl->file);
		if (reverse)
			pl->chunks = split_patch(s, &s2, &s1);
		else
			pl->chunks = split_patch(s, &s1, &s2);
	}
	if (sf.body == NULL || s1.body == NULL || s1.body == NULL) {
		pl->wiggles = pl->conflicts = -1;
	} else {
		struct file ff, fp1, fp2;
		struct csl *csl1, *csl2;
		struct ci ci;
		ff = split_stream(sf, ByWord | ignore_blanks);
		fp1 = split_stream(s1, ByWord | ignore_blanks);
		fp2 = split_stream(s2, ByWord | ignore_blanks);
		if (pl->chunks)
			csl1 = pdiff(ff, fp1, pl->chunks);
		else
			csl1 = diff(ff, fp1);
		csl2 = diff_patch(fp1, fp2);
		ci = make_merger(ff, fp1, fp2, csl1, csl2, 0, 1, 0);
		pl->wiggles = ci.wiggles;
		pl->conflicts = ci.conflicts;
		free(ci.merger);
		free(csl1);
		free(csl2);
		free(ff.list);
		free(fp1.list);
		free(fp2.list);
	}

	free(s1.body);
	free(s2.body);
	free(s.body);
	free(sf.body);
	pl->calced = 1;
}

static int get_prev(int pos, struct plist *pl, int n, int mode)
{
	int found = 0;
	if (pos == -1)
		return pos;
	do {
		if (pl[pos].prev == -1)
			return pl[pos].parent;
		pos = pl[pos].prev;
		while (pl[pos].open &&
		       pl[pos].last >= 0)
			pos = pl[pos].last;
		if (pl[pos].last >= 0)
			/* always see directories */
			found = 1;
		else if (mode == 0)
			found = 1;
		else if (mode <= 1 && pl[pos].wiggles > 0)
			found = 1;
		else if (mode <= 2 && pl[pos].conflicts > 0)
			found = 1;
	} while (pos >= 0 && !found);
	return pos;
}

static int get_next(int pos, struct plist *pl, int n, int mode,
		    FILE *f, int reverse, int ignore_blanks)
{
	int found = 0;
	if (pos == -1)
		return pos;
	do {
		if (pl[pos].open) {
			if (pos + 1 < n)
				pos =  pos+1;
			else
				return -1;
		} else {
			while (pos >= 0 && pl[pos].next == -1)
				pos = pl[pos].parent;
			if (pos >= 0)
				pos = pl[pos].next;
		}
		if (pos < 0)
			return -1;
		if (pl[pos].calced == 0 && pl[pos].end)
			calc_one(pl+pos, f, reverse, ignore_blanks);
		if (pl[pos].last >= 0)
			/* always see directories */
			found = 1;
		else if (mode == 0)
			found = 1;
		else if (mode <= 1 && pl[pos].wiggles > 0)
			found = 1;
		else if (mode <= 2 && pl[pos].conflicts > 0)
			found = 1;
	} while (pos >= 0 && !found);
	return pos;
}

static void draw_one(int row, struct plist *pl, FILE *f, int reverse,
		     int ignore_blanks)
{
	char hdr[12];
	hdr[0] = 0;

	if (pl == NULL) {
		move(row, 0);
		clrtoeol();
		return;
	}
	if (pl->calced == 0 && pl->end)
		/* better load the patch and count the chunks */
		calc_one(pl, f, reverse, ignore_blanks);
	if (pl->end == 0) {
		strcpy(hdr, "         ");
	} else {
		if (pl->chunks > 99)
			strcpy(hdr, "XX");
		else
			sprintf(hdr, "%2d", pl->chunks);
		if (pl->wiggles > 99)
			strcpy(hdr+2, " XX");
		else
			sprintf(hdr+2, " %2d", pl->wiggles);
		if (pl->conflicts > 99)
			strcpy(hdr+5, " XX ");
		else
			sprintf(hdr+5, " %2d ", pl->conflicts);
	}
	if (pl->end)
		strcpy(hdr+9, "= ");
	else if (pl->open)
		strcpy(hdr+9, "+ ");
	else
		strcpy(hdr+9, "- ");

	if (!pl->end)
		attrset(0);
	else if (pl->is_merge)
		attrset(a_saved);
	else if (pl->conflicts)
		attrset(a_has_conflicts);
	else if (pl->wiggles)
		attrset(a_has_wiggles);
	else
		attrset(a_no_wiggles);

	mvaddstr(row, 0, hdr);
	mvaddstr(row, 11, pl->file);
	clrtoeol();
}

static int save_one(FILE *f, struct plist *pl, int reverse,
		    int ignore_blanks)
{
	struct stream sp, sa, sb, sm;
	struct file fa, fb, fm;
	struct csl *csl1, *csl2;
	struct ci ci;
	int chunks;
	sp = load_segment(f, pl->start,
			  pl->end);
	if (reverse)
		chunks = split_patch(sp, &sa, &sb);
	else
		chunks = split_patch(sp, &sb, &sa);
	fb = split_stream(sb, ByWord | ignore_blanks);
	fa = split_stream(sa, ByWord | ignore_blanks);
	sm = load_file(pl->file);
	fm = split_stream(sm, ByWord | ignore_blanks);
	csl1 = pdiff(fm, fb, chunks);
	csl2 = diff_patch(fb, fa);
	ci = make_merger(fm, fb, fa, csl1, csl2, 0, 1, 0);
	return save_merge(fm, fb, fa, ci.merger,
			  pl->file, 1);
}

static char *main_help[] = {
	"   You are using the \"browse\" mode of wiggle.",
	"This page shows a list of files in a patch together with",
	"the directories that contain them.",
	"A directory is indicated by a '+' if the contents are",
	"listed or a '-' if the contents are hidden.  A file is",
	"indicated by an '='.  Typing <space> or <return> will",
	"expose or hide a directory, and will visit a file.",
	"",
	"The three columns of numbers are:",
	"  Ch   The number of patch chunks which applied to",
	"       this file",
	"  Wi   The number of chunks that needed to be wiggled",
	"       in to place",
	"  Co   The number of chunks that created an unresolvable",
	"       conflict",
	"",
	"Keystrokes recognised in this page are:",
	"  ?          Display this help",
	"  SPC        On a directory, toggle hiding of contents",
	"             On file, visit the file",
	"  RTN        Same as SPC",
	"  q          Quit program",
	"  control-C  Disable auto-save-on-exit",
	"  n,j,DOWN   Go to next line",
	"  p,k,UP     Go to previous line",
	"",
	"  A          list All files",
	"  W          only list files with a wiggle or a conflict",
	"  C          only list files with a conflict",
	"",
	"  S          Save this file with changes applied.  If",
	"             some but not all files are saved, wiggle will",
	"             prompt on exit to save the rest.",
	"  R          Revert the current saved file to its original",
	"             content",
	"  I          toggle whether spaces are ignored",
	"             when matching text.",
	NULL
};
static char *saveall_msg = " %d file%s (of %d) have not been saved.";
static char saveall_buf[200];
static char *saveall_query[] = {
	"",
	saveall_buf,
	" Would you like to save them?",
	"  Y = yes, save them all",
	"  N = no, exit without saving anything else",
	"  Q = Don't quit just yet",
	NULL
};
static void main_window(struct plist *pl, int *np, FILE *f, int reverse,
			int replace, int ignore_blanks)
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
	 *  A W C: select All Wiggles or Conflicts
	 *         mode
	 *
	 */
	char *mesg = NULL;
	char mesg_buf[1024];
	int last_mesg_len = 0;
	int pos = 0; /* position in file */
	int row = 1; /* position on screen */
	int rows = 0; /* size of screen in rows */
	int cols = 0;
	int tpos, i;
	int refresh = 2;
	int c = 0;
	int mode = 0; /* 0=all, 1= only wiggled, 2=only conflicted */
	int cnt; /* count of files that need saving */
	int any; /* count of files that have been save*/
	int ans;
	MEVENT mevent;
	char *debug = getenv("WIGGLE_DEBUG");

	if (debug && !*debug)
		debug = NULL;

	freopen("/dev/null","w",stderr);
	term_init(1);
	pl = sort_patches(pl, np);

	while (1) {
		if (refresh == 2) {
			clear(); (void)attrset(0);
			attron(A_BOLD);
			mvaddstr(0, 0, "Ch Wi Co Patched Files");
			attroff(A_BOLD);
			if (ignore_blanks)
				addstr(" (ignoring blanks)");
			move(2, 0);
			refresh = 1;
		}
		if (row < 1  || row >= rows)
			refresh = 1;
		if (refresh) {
			refresh = 0;
			getmaxyx(stdscr, rows, cols);

			if (row >= rows + 3)
				row = (rows+1)/2;
			if (row >= rows)
				row = rows-1;
			tpos = pos;
			for (i = row; i > 1; i--) {
				tpos = get_prev(tpos, pl, *np, mode);
				if (tpos == -1) {
					row = row - i + 1;
					break;
				}
			}
			/* Ok, row and pos could be trustworthy now */
			tpos = pos;
			for (i = row; i >= 1; i--) {
				draw_one(i, &pl[tpos], f, reverse, ignore_blanks);
				tpos = get_prev(tpos, pl, *np, mode);
			}
			tpos = pos;
			for (i = row+1; i < rows; i++) {
				tpos = get_next(tpos, pl, *np, mode, f, reverse,ignore_blanks);
				if (tpos >= 0)
					draw_one(i, &pl[tpos], f, reverse, ignore_blanks);
				else
					draw_one(i, NULL, f, reverse, ignore_blanks);
			}
		}
		attrset(0);
		if (last_mesg_len) {
			move(0, cols - last_mesg_len);
			clrtoeol();
			last_mesg_len = 0;
		}
		if (mesg) {
			last_mesg_len = strlen(mesg);
			move(0, cols - last_mesg_len);
			addstr(mesg);
			mesg = NULL;
		} else if (debug) {
			/* debugging help: report last keystroke */
			char bb[30];
			sprintf(bb, "last-key = 0%o", c);
			attrset(0);
			last_mesg_len = strlen(bb);
			mvaddstr(0, cols - last_mesg_len, bb);
		}
		move(row, 9);
		c = getch();
		switch (c) {
		case 'j':
		case 'n':
		case 'N':
		case 'N'-64:
		case KEY_DOWN:
			tpos = get_next(pos, pl, *np, mode, f, reverse, ignore_blanks);
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
			tpos = get_prev(pos, pl, *np, mode);
			if (tpos >= 0) {
				pos = tpos;
				row--;
			}
			break;

		case KEY_MOUSE:
			if (getmouse(&mevent) != OK)
				break;
			while (row < mevent.y &&
			       (tpos = get_next(pos, pl, *np, mode, f, reverse, ignore_blanks))
			       >= 0) {
				pos = tpos;
				row++;
			}
			while (row > mevent.y &&
			       (tpos = get_prev(pos, pl, *np, mode)) >= 0) {
				pos = tpos;
				row--;
			}
			if (row != mevent.y)
				/* couldn't find the line */
				break;
			/* FALL THROUGH */
		case ' ':
		case 13:
			if (pl[pos].end == 0) {
				pl[pos].open = !pl[pos].open;
				refresh = 1;
				if (pl[pos].open)
					mesg = "Opened folder";
				else
					mesg = "Closed folder";
			} else {
				int c;
				if (pl[pos].is_merge)
					c = merge_window(&pl[pos], NULL, reverse, 0, 0, ignore_blanks);
				else
					c = merge_window(&pl[pos], f, reverse, 0, 0, ignore_blanks);
				refresh = 2;
				if (c) {
					pl[pos].is_merge = 1;
					snprintf(mesg_buf, cols,
						 "Saved file %s.",
						 pl[pos].file);
					mesg = mesg_buf;
				}
			}
			break;
		case 27: /* escape */
			attrset(0);
			mvaddstr(0, cols-10, "ESC..."); clrtoeol();
			c = getch();
			switch (c) {
			}
			move(0, cols-10); clrtoeol();
			break;
		case 'C'-64:
			if (replace)
				mesg = "Save-on-exit disabled. Use 'q' to quit.";
			else
				mesg = "Use 'q' to quit.";
			replace = 0;
			break;

		case 'q':
			cnt = 0;
			any = 0;
			for (i = 0; i < *np; i++)
				if (pl[i].end && !pl[i].is_merge)
					cnt++;
				else if (pl[i].end)
					any++;
			if (!cnt) {
				endwin();
				return;
			}
			refresh = 2;
			if (replace)
				ans = 1;
			else if (any) {
				sprintf(saveall_buf, saveall_msg,
					cnt, cnt == 1 ? "" : "s", cnt+any);
				ans = help_window(saveall_query, NULL, 1);
			} else
				ans = 0;
			if (ans < 0)
				break;
			if (ans) {
				for (i = 0; i < *np; i++) {
					if (pl[i].end
					    && !pl[i].is_merge)
						save_one(f, &pl[i],
							 reverse,
							ignore_blanks);
				}
			} else
				cnt = 0;
			endwin();
			if (cnt)
				printf("%d file%s saved\n", cnt,
				       cnt == 1 ? "" : "s");
			return;

		case 'A':
			mode = 0; refresh = 1;
			mesg = "Showing ALL files";
			break;
		case 'W':
			mode = 1; refresh = 1;
			mesg = "Showing Wiggled files";
			break;
		case 'C':
			mode = 2; refresh = 1;
			mesg = "Showing Conflicted files";
			break;

		case 'S': /* Save updated file */
			if (pl[pos].end == 0) {
				/* directory */
				mesg = "Cannot save a folder.";
			} else if (pl[pos].is_merge) {
				/* Already saved */
				mesg = "File is already saved.";
			} else {
				if (save_one(f, &pl[pos], reverse, ignore_blanks) == 0) {
					pl[pos].is_merge = 1;
					snprintf(mesg_buf, cols,
						 "Saved file %s.",
						 pl[pos].file);
					pl[pos].chunks = pl[pos].conflicts;
					pl[pos].wiggles = 0;
				} else
					snprintf(mesg_buf, cols,
						 "Failed to save file %s.",
						 pl[pos].file);
				mesg = mesg_buf;
				refresh = 1;
			}
			break;

		case 'R': /* Restore updated file */
			if (pl[pos].end == 0)
				mesg = "Cannot restore a folder.";
			else if (!pl[pos].is_merge)
				mesg = "File has not been saved, cannot restore.";
			else {
				/* rename foo.porig to foo, and clear is_merge */
				char *file = pl[pos].file;
				char *orignew = xmalloc(strlen(file) + 20);
				strcpy(orignew, file);
				strcat(orignew, ".porig");
				if (rename(orignew, file) == 0) {
					mesg = "File has been restored.";
					pl[pos].is_merge = 0;
					refresh = 1;
					calc_one(&pl[pos], f, reverse, ignore_blanks);
				} else
					mesg = "Could not restore file!";
			}
			break;

		case 'I': /* Toggle ignoring blanks */
			ignore_blanks = ignore_blanks ? 0 : IgnoreBlanks;
			refresh = 2;
			for (i = 0; i < *np; i++)
				pl[i].calced = 0;
			break;

		case '?':
			help_window(main_help, NULL, 0);
			refresh = 2;
			break;

		case KEY_RESIZE:
			refresh = 2;
			break;
		}
	}
}

static void catch(int sig)
{
	if (sig == SIGINT && !intr_kills) {
		signal(sig, catch);
		return;
	}
	noraw();
	nl();
	endwin();
	printf("Died on signal %d\n", sig);
	fflush(stdout);
	if (sig != SIGBUS && sig != SIGSEGV)
		exit(2);
	else
		/* Otherwise return and die */
		signal(sig, NULL);
}

static void term_init(int doraw)
{

	static int init_done = 0;

	if (init_done)
		return;
	init_done = 1;

	signal(SIGINT, catch);
	signal(SIGQUIT, catch);
	signal(SIGTERM, catch);
	signal(SIGBUS, catch);
	signal(SIGSEGV, catch);

	initscr();
	if (doraw)
		raw();
	else
		cbreak();
	noecho();
	start_color();
	use_default_colors();
	if (!has_colors()) {
		a_delete = A_UNDERLINE;
		a_added = A_BOLD;
		a_common = A_NORMAL;
		a_sep = A_STANDOUT;
		a_already = A_STANDOUT;
		a_has_conflicts = A_UNDERLINE;
		a_has_wiggles = A_BOLD;
		a_no_wiggles = A_NORMAL;
	} else {
		init_pair(1, COLOR_RED, -1);
		a_delete = COLOR_PAIR(1);
		init_pair(2, COLOR_GREEN, -1);
		a_added = COLOR_PAIR(2);
		a_common = A_NORMAL;
		init_pair(3, COLOR_WHITE, COLOR_GREEN);
		a_sep = COLOR_PAIR(3); a_sep = A_STANDOUT;
		init_pair(4, -1, COLOR_YELLOW);
		a_void = COLOR_PAIR(4);
		init_pair(5, COLOR_BLUE, -1);
		a_unmatched = COLOR_PAIR(5);
		init_pair(6, COLOR_CYAN, -1);
		a_extra = COLOR_PAIR(6);

		init_pair(7, COLOR_BLACK, COLOR_CYAN);
		a_already = COLOR_PAIR(7);

		a_has_conflicts = a_delete;
		a_has_wiggles = a_added;
		a_no_wiggles = a_unmatched;
		a_saved = a_extra;
	}
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	mousemask(ALL_MOUSE_EVENTS, NULL);
}

int vpatch(int argc, char *argv[], int patch, int strip,
	   int reverse, int replace, int selftest, int ignore_blanks)
{
	/* NOTE argv[0] is first arg...
	 * Behaviour depends on number of args:
	 * 0: A multi-file patch is read from stdin
	 * 1: if 'patch', parse it as a multi-file patch and allow
	 *    the files to be browsed.
	 *    if filename ends '.rej', then treat it as a patch again
	 *    a file with the same basename
	 *    Else treat the file as a merge (with conflicts) and view it.
	 * 2: First file is original, second is patch
	 * 3: Files are: original previous new.  The diff between 'previous' and
	 *    'new' needs to be applied to 'original'.
	 *
	 * If a multi-file patch is being read, 'strip' tells how many
	 * path components to strip.  If it is -1, we guess based on
	 * existing files.
	 * If 'reverse' is given, when we invert any patch or diff
	 * If 'replace' then we save the resulting merge.
	 */
	FILE *in;
	FILE *f;
	struct plist *pl;
	int num_patches;

	switch (argc) {
	default:
		fprintf(stderr, "%s: too many file names given.\n", Cmd);
		exit(1);

	case 0: /* stdin is a patch */
		if (lseek(fileno(stdin), 0L, 1) == -1) {
			/* cannot seek, so need to copy to a temp file */
			f = tmpfile();
			if (!f) {
				fprintf(stderr, "%s: Cannot create temp file\n", Cmd);
				exit(1);
			}
			pl = parse_patch(stdin, f, &num_patches);
			in = f;
		} else {
			pl = parse_patch(stdin, NULL, &num_patches);
			in = fdopen(dup(0), "r");
		}
		/* use stderr for keyboard input */
		dup2(2, 0);
		if (set_prefix(pl, num_patches, strip) == 0) {
			fprintf(stderr, "%s: aborting\n", Cmd);
			exit(2);
		}
		main_window(pl, &num_patches, in, reverse, replace, ignore_blanks);
		plist_free(pl, num_patches);
		fclose(in);
		break;

	case 1: /* a patch, a .rej, or a merge file */
		f = fopen(argv[0], "r");
		if (!f) {
			fprintf(stderr, "%s: cannot open %s\n", Cmd, argv[0]);
			exit(1);
		}
		check_dir(argv[0], fileno(f));
		if (patch) {
			pl = parse_patch(f, NULL, &num_patches);
			if (set_prefix(pl, num_patches, strip) == 0) {
				fprintf(stderr, "%s: aborting\n", Cmd);
				exit(2);
			}
			main_window(pl, &num_patches, f, reverse, replace,ignore_blanks);
			plist_free(pl, num_patches);
		} else if (strlen(argv[0]) > 4 &&
			 strcmp(argv[0]+strlen(argv[0])-4, ".rej") == 0) {
			char *origname = strdup(argv[0]);
			origname[strlen(origname) - 4] = '\0';
			show_merge(origname, f, reverse, 0, NULL, NULL,
				   replace, selftest, ignore_blanks);
		} else
			show_merge(argv[0], f, reverse, 1, NULL, NULL,
				   replace, selftest, ignore_blanks);

		break;
	case 2: /* an orig and a diff/.ref */
		f = fopen(argv[1], "r");
		check_dir(argv[1], fileno(f));
		if (!f) {
			fprintf(stderr, "%s: cannot open %s\n", Cmd, argv[0]);
			exit(1);
		}
		show_merge(argv[0], f, reverse, 0, NULL, NULL,
			   replace, selftest, ignore_blanks);
		break;
	case 3: /* orig, before, after */
		show_merge(argv[0], NULL, reverse, 0, argv[1], argv[2],
			   replace, selftest, ignore_blanks);
		break;
	}

	noraw();
	nl();
	endwin();
	exit(0);
}
