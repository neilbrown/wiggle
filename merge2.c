/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2005 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2010-2013 Neil Brown <neilb@suse.de>
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

#include "wiggle.h"
#include <limits.h>

/*
 * Second attempt at merging....
 *
 * We want to create a mergelist which identifies 'orig' and 'after'
 * sections (from a and c) and conflicts (which are ranges of a,b,c which
 * all don't match).
 * It is also helpful to differentiate 'orig' sections that aren't
 * matched in 'b' with orig sections that are.
 * To help with highlighting, it will be useful to know where
 * the conflicts match the csl lists.
 *
 * This can all be achieved with a list of (a,b,c,c1,c1) 5-tuples.
 * If two consecutive differ in more than one of a,b,c, it is a
 * conflict.
 * If only 'a' differ, it is un-matched original.
 * If only 'b' differ, it is matched, unchanged original
 * If only 'c' differ, it is 1
 */

static inline int min(int a, int b)
{
	return a < b ? a : b;
}

static int check_alreadyapplied(struct file af, struct file cf,
				struct merge *m)
{
	int i;
	if (m->al != m->cl)
		return 0;
	for (i = 0; i < m->al; i++) {
		if (af.list[m->a+i].len != cf.list[m->c+i].len)
			return 0;
		if (strncmp(af.list[m->a+i].start,
			    cf.list[m->c+i].start,
			    af.list[m->a+i].len) != 0)
			return 0;
	}
	if (do_trace) {
		printf("already applied %d,%d,%d - %d,%d,%d\n",
		       m->a, m->b, m->c, m->al, m->bl, m->cl);
		printf(" %.10s - %.10s\n", af.list[m->a].start,
		       cf.list[m->c].start);
	}
	m->type = AlreadyApplied;
	return 1;
}

/* A 'cut-point' is a location in the merger where it is reasonable
 * the change the mode of display - between displaying the merger
 * and displaying the separate streams.
 * A 'conflict' can only be displayed as separate stream so when
 * one is found, we need to find a preceding and trailing cut-point
 * and enlarge the conflict to that range.
 * A suitable location is one where all three streams are at a line-end.
 */
static int is_cutpoint(struct merge m,
		       struct file af, struct file bf, struct file cf)
{
	return ((m.a == 0 || ends_line(af.list[m.a-1])) &&
		(m.b == 0 || ends_line(bf.list[m.b-1])) &&
		(m.c == 0 || ends_line(cf.list[m.c-1])));
}

int isolate_conflicts(struct file af, struct file bf, struct file cf,
		      struct csl *csl1, struct csl *csl2, int words,
		      struct merge *m, int show_wiggles,
		      int *wigglesp)
{
	/* A Conflict indicates that something is definitely wrong
	 * and so we need to be a bit suspicious of nearby apparent matches.
	 * To display a conflict effectively we expands its effect to
	 * include any Extraneous, Unmatched, Changed or AlreadyApplied text.
	 * Also, unless 'words', we need to include any partial lines
	 * in the Unchanged text that forms the border of a conflict.
	 *
	 * A Changed text may also border a conflict, but it can
	 * only border one conflict (where as an Unchanged can border
	 * a preceding and a following conflict).
	 * The 'new' section of a Changed text appears in the
	 * conflict as does any part of the original before
	 * a newline.
	 *
	 * A hunk header (Extraneous) is never considered part of a
	 * conflict.  It thereby can serve as a separator between
	 * conflicts.
	 *
	 * Extended conflicts are marked by setting ->in_conflict in
	 * the "struct merge".  This is '1' for an Unchanged, Changed,
	 * or (Extraneous) hunk header which borders the conflict,
	 * '2' for a merger which is truly in conflict, and '3' for
	 * a merger which is causing a 'wiggle'.
	 * When in_conflict == 1, the 'lo' and 'hi' fields indicate
	 * how much of the 'a' file is included in the conflict, the rest
	 * being part of the clean result.
	 * Elements in af from m->a to m->a+m->lo are in the preceding
	 * conflict, from m->a+m->lo to m->a+m->hi are clean, and
	 * m->a+m->hi to m->a+m->al are in the following conflict.
	 *
	 * We need to ensure there is adequate context for the conflict.
	 * So ensure there are at least 3 newlines in Extraneous or
	 * Unchanged on both sides of a Conflict - but don't go so far
	 * as including a hunk header.
	 * If there are 3, and they are all in 'Unchanged' sections, then
	 * that much context is not really needed - reduce it a bit.
	 *
	 * If a wiggle is adjacent to a conflict then:
	 * - if show_wiggles is set, we just merge them
	 * - if it is not set, we still want to count the wiggle.
	 */
	int i, j, k;
	int cnt = 0, wiggles = 0;
	int in_wiggle = 0;
	int changed = 0;
	int unmatched = 0;
	int extraneous = 0;

	for (i = 0; m[i].type != End; i++)
		m[i].in_conflict = 0;

	for (i = 0; m[i].type != End; i++) {
		/* The '3' here is a count of newlines.  Once we find
		 * that many newlines of the particular type, we have escaped.
		 */
		if (m[i].type == Changed)
			changed = 3;
		if (m[i].type == Unmatched)
			unmatched = 3;
		if (m[i].type == Extraneous && bf.list[m[i].b].start[0])
			/* hunk headers don't imply wiggles, other
			 * extraneous text does.
			 */
			extraneous = 3;

		if (m[i].type != Unchanged && changed && (unmatched || extraneous)) {
			if (!in_wiggle)
				wiggles++;
			in_wiggle = 1;
		} else
			in_wiggle = 0;

		if ((m[i].type == Conflict) ||
		    (show_wiggles && in_wiggle)) {
			/* We have a conflict or wiggle here.
			 * First search backwards for an Unchanged marking
			 * things as in_conflict.  Then find the
			 * cut-point in the Unchanged.  If there isn't one,
			 * keep looking.
			 *
			 * Then search forward doing the same thing.
			 */
			int newlines = 0;
			m[i].in_conflict = m[i].type == Conflict ? 2 : 3;
			j = i;
			while (--j >= 0) {
				int firstk;

				if (m[j].type == Extraneous &&
				    bf.list[m[j].b].start[0] == '\0')
					/* hunk header - not conflict any more */
					break;
				if (m[j].in_conflict > 1)
					/* Merge the conflicts */
					break;
				if (!m[j].in_conflict) {
					m[j].in_conflict = 1;
					m[j].lo = 0;
				}
				/* Following must set m[j].hi, or set
				 * in_conflict > 1
				 */
				if (m[j].type == Extraneous) {
					for (k = m[j].bl; k > 0; k--)
						if (ends_line(bf.list[m[j].b+k-1]))
							newlines++;
				}

				if (m[j].type != Unchanged &&
				    m[j].type != Changed) {
					if (m[j].type == Conflict)
						m[j].in_conflict = 2;
					else
						m[j].in_conflict = m[i].in_conflict;
					continue;
				}
				/* If we find enough newlines in this section,
				 * then we only really need 1, but would rather
				 * it wasn't the first one.  'firstk' allows us
				 * to track which newline we actually use
				 */
				firstk = m[j].al+1;
				if (words) {
					m[j].hi = m[j].al;
					break;
				}
				/* need to find the last line-break, which
				 * might be after the last newline, if there
				 * is one, or might be at the start
				 */
				for (k = m[j].al; k > 0; k--) {
					if (m[j].a + k >= af.elcnt)
						/* FIXME impossible!*/
						break;
					if (ends_line(af.list[m[j].a+k-1])) {
						if (firstk > m[j].al)
							firstk = k;
						newlines++;
						if (newlines >= 3) {
							k = firstk;
							break;
						}
					}
				}
				if (k > 0)
					m[j].hi = k;
				else if (j == 0)
					m[j].hi = firstk;
				else if (is_cutpoint(m[j], af,bf,cf))
					m[j].hi = 0;
				else
					/* no start-of-line found... */
					m[j].hi = -1;
				if (m[j].hi > 0 &&
				    (m[j].type == Changed)) {
					/* this can only work if start is
					 * also a line break */
					if (is_cutpoint(m[j], af,bf,cf))
						/* ok */;
					else
						m[j].hi = -1;
				}
				if (m[j].hi >= 0)
					break;
				m[j].in_conflict = m[i].in_conflict;
			}

			/* now the forward search */
			newlines = 0;
			for (j = i+1; m[j].type != End; j++) {
				if (m[j].type == Extraneous) {
					for (k = 0; k < m[j].bl; k++)
						if (ends_line(bf.list[m[j].b+k]))
							newlines++;
				}
				if (m[j].type != Unchanged &&
				    m[j].type != Changed) {
					if (m[j].type == Conflict)
						m[j].in_conflict = 2;
					else
						m[j].in_conflict = m[i].in_conflict;
					continue;
				}
				m[j].in_conflict = 1;
				m[j].hi = m[j].al;
				if (words) {
					m[j].lo = 0;
					break;
				}
				/* need to find a line-break, which might be at
				 * the very beginning, or might be after the
				 * first newline - if there is one
				 */
				if (is_cutpoint(m[j], af,bf,cf))
					m[j].lo = 0;
				else {
					/* If we find enough newlines in this section,
					 * then we only really need 1, but would rather
					 * it wasn't the first one.  'firstk' allows us
					 * to track which newline we actually use
					 */
					int firstk = -1;
					for (k = 0 ; k < m[j].al ; k++)
						if (ends_line(af.list[m[j].a+k])) {
							if (firstk < 0)
								firstk = k;
							newlines++;
							if (newlines >= 3) {
								k = firstk;
								break;
							}
						}
					if (newlines < 3 &&
					    m[j+1].type  == End)
						/* Hit end of file, pretend we found 3 newlines. */
						k = firstk;

					if (firstk >= 0 &&
					    m[j+1].type == Unmatched) {
						/* If this Unmatched exceeds 3 lines, just stop here */
						int p;
						int nl = 0;
						for (p = 0; p < m[j+1].al ; p++)
							if (ends_line(af.list[m[j+1].a+p])) {
								nl++;
								if (nl > 3)
									break;
							}
						if (nl > 3)
							k = firstk;
					}
					if (k < m[j].al)
						m[j].lo = k+1;
					else
						/* no start-of-line found */
						m[j].lo = m[j].al+1;
				}
				if (m[j].lo <= m[j].al+1 &&
				    (m[j].type == Changed)) {
					/* this can only work if the end is a line break */
					if (is_cutpoint(m[j+1], af,bf,cf))
						/* ok */;
					else
						m[j].lo = m[j].al+1;
				}
				if (m[j].lo < m[j].al+1)
					break;
				m[j].in_conflict = m[i].in_conflict;
			}
			if (m[j-1].in_conflict == 1)
				i = j - 1;
			else
				/* A hunk header bordered the conflict */
				i = j;

			/* If any of the merges are Changed or Conflict,
			 * then this really is a Conflict or Wiggle.
			 * If not they are just Unchanged, Unmatched,
			 * Extraneous or AlreadyApplied, and so don't
			 * really count.
			 * Note that the first/last merges (in_conflict==1)
			 * can be Changed and so much be check separately.
			 */
			if (m[j].type == Changed)
				goto out;
			for (j = i-1; j >= 0 && m[j].in_conflict > 1; j--)
				if (m[j].type == Changed || m[j].type == Conflict)
					goto out;
			if (j >= 0 && m[j].type == Changed)
				goto out;
			/* False alarm, no real conflict/wiggle here as
			 * nothing changed. */
			if (j < 0)
				j = 0;
			if (m[j].in_conflict == 1) {
				m[j].hi = m[j].al;
				if (m[j].lo == 0)
					m[j].in_conflict = 0;
				j++;
			}
			while (j <= i)
				m[j++].in_conflict = 0;
		out:
			if (m[i].type == End)
				break;
		}
		for (k = 1; k < m[i].al; k++) {
			if (m[i].a + k >= af.elcnt)
				/* FIXME this should be impossible, but
				 * it happened.
				 */
				break;
			if (words || ends_line(af.list[m[i].a+k])) {
				if (unmatched)
					unmatched--;
				if (changed)
					changed--;
				if (extraneous)
					extraneous--;
			}
		}
	}
	if (!show_wiggles)
		*wigglesp = wiggles;
	/* Now count the conflicts and wiggles */
	for (i = 0; m[i].type != End; i++) {
		int true_conflict = 0;
		if (!m[i].in_conflict)
			continue;

		for (j = i; m[j].type != End && m[j].in_conflict; j++) {
			if (m[j].in_conflict == 2)
				true_conflict = 1;
			if (j > i &&
			    m[j].in_conflict == 1) {
				/* end of region */
				if (!m[j+1].in_conflict)
					j++;
				break;
			}
		}
		if (true_conflict)
			cnt++;
		else
			wiggles++;
		i = j-1;
	}
	if (show_wiggles)
		*wigglesp = wiggles;
	return cnt;
}

struct ci make_merger(struct file af, struct file bf, struct file cf,
		      struct csl *csl1, struct csl *csl2, int words,
		      int ignore_already, int show_wiggles)
{
	/* find the wiggles and conflicts between csl1 and csl2
	 */
	struct ci rv;
	int i, l;
	int a, b, c, c1, c2;
	int header_checked = -1;
	int header_found = 0;

	rv.conflicts = rv.wiggles = rv.ignored = 0;

	for (i = 0; csl1[i].len; i++)
		;
	l = i;
	for (i = 0; csl2[i].len; i++)
		;
	l += i;
	/* maybe a bit of slack at each end */
	l = l * 4 + 10;

	rv.merger = xmalloc(sizeof(struct merge)*l);

	a = b = c = c1 = c2 = 0;
	i = 0;
	while (1) {
		int match1, match2;
		match1 = (a >= csl1[c1].a && b >= csl1[c1].b); /* c1 doesn't match */
		match2 = (b >= csl2[c2].a && c >= csl2[c2].b);

		if (header_checked != c2) {
			/* Check if there is a hunk header in this range */
			int j;
			header_found = -1;
			for (j = b; j < csl2[c2].a + csl2[c2].len; j++)
				if (bf.list[j].start[0] == '\0') {
					header_found = j;
					break;
				}
			header_checked = c2;
		}
		rv.merger[i].a = a;
		rv.merger[i].b = b;
		rv.merger[i].c = c;
		rv.merger[i].c1 = c1;
		rv.merger[i].c2 = c2;
		rv.merger[i].in_conflict = 0;

		if (!match1 && match2) {
			/* This is either Unmatched or Extraneous - probably both.
			 * If the match2 has a hunk-header Extraneous, it must
			 * align with an end-of-line in 'a', so adjust endpoint
			 */
			int newa = csl1[c1].a;
			if (header_found >= 0) {
				while (newa > a &&
				       !ends_line(af.list[newa-1]))
					newa--;
			}
			if (a == newa && b == csl1[c1].b)
				newa = csl1[c1].a;
			if (a < newa) {
				/* some unmatched text */
				rv.merger[i].type = Unmatched;
				rv.merger[i].al = newa - a;
				rv.merger[i].bl = 0;
				rv.merger[i].cl = 0;
			} else {
				int newb;
				assert(b < csl1[c1].b);
				/* some Extraneous text */
				/* length is min of unmatched on left
				 * and matched on right.
				 * However a hunk-header must be an
				 * Extraneous section by itself, so if this
				 * start with one, the length is 1, and if
				 * there is one in the middle, only take the
				 * text up to there for now.
				 */
				rv.merger[i].type = Extraneous;
				rv.merger[i].al = 0;
				newb = b +
					min(csl1[c1].b - b,
					    csl2[c2].len - (b-csl2[c2].a));
				if (header_found == b) {
					newb = b + 1;
					header_checked = -1;
				} else if (header_found > b && header_found < newb) {
					newb = header_found;
					header_checked = -1;
				}
				assert(newb > b);
				rv.merger[i].cl =
					rv.merger[i].bl = newb - b;
			}
		} else if (match1 && !match2) {
			/* some changed text
			 * if 'c' is currently at a suitable cut-point, then
			 * we can look for a triple-cut-point for start.
			 * Also, if csl2[c2].b isn't in a conflict, and is
			 * a suitable cut-point, then we could make a
			 * triple-cut-point for end of a conflict.
			 */

			rv.merger[i].type = Changed;
			rv.merger[i].bl = min(csl1[c1].b+csl1[c1].len, csl2[c2].a) - b;
			rv.merger[i].al = rv.merger[i].bl;
			rv.merger[i].cl = csl2[c2].b - c;
		} else if (match1 && match2) {
			/* Some unchanged text
			 */
			rv.merger[i].type = Unchanged;
			rv.merger[i].bl =
				min(csl1[c1].len - (b-csl1[c1].b),
				    csl2[c2].len - (b-csl2[c2].a));
			rv.merger[i].al = rv.merger[i].cl =
				rv.merger[i].bl;
		} else {
			/* must be a conflict.
			 * Move a and c to next match, and b to closest of the two
			 */
			rv.merger[i].type = Conflict;
			rv.merger[i].al = csl1[c1].a - a;
			rv.merger[i].cl = csl2[c2].b - c;
			rv.merger[i].bl = min(csl1[c1].b, csl2[c2].a) - b;
			if (ignore_already &&
			    check_alreadyapplied(af, cf, &rv.merger[i]))
				rv.ignored++;
			else if (rv.merger[i].bl == 0 &&
				 rv.merger[i].cl > 0)
				/* As the 'before' stream is empty, this
				 * could look like Unmatched in the
				 * original, and an insertion in the
				 * diff.  Reporting it like that is
				 * probably more useful that as a full
				 * conflict.
				 * Leave the type for the insertion as
				 * Conflict (not Changed) as there is some
				 * real uncertainty here, but allow the
				 * original to become Unmatched.
				 */
				rv.merger[i].al = 0;
		}
		rv.merger[i].oldtype = rv.merger[i].type;
		a += rv.merger[i].al;
		b += rv.merger[i].bl;
		c += rv.merger[i].cl;
		i++;

		while (csl1[c1].a + csl1[c1].len <= a && csl1[c1].len)
			c1++;
		assert(csl1[c1].b + csl1[c1].len >= b);
		while (csl2[c2].b + csl2[c2].len <= c && csl2[c2].len)
			c2++;
		assert(csl2[c2].a + csl2[c2].len >= b);
		if (csl1[c1].len == 0 && csl2[c2].len == 0 &&
		    a == csl1[c1].a && b == csl1[c1].b &&
		    b == csl2[c2].a && c == csl2[c2].b)
			break;
	}
	rv.merger[i].type = End;
	rv.merger[i].oldtype = End;
	rv.merger[i].a = a;
	rv.merger[i].b = b;
	rv.merger[i].c = c;
	rv.merger[i].c1 = c1;
	rv.merger[i].c2 = c2;
	rv.merger[i].in_conflict = 0;
	assert(i < l);

	/* Now revert any AlreadyApplied that aren't bounded by
	 * Unchanged or Changed.
	 */
	for (i = 0; rv.merger[i].type != End; i++) {
		if (rv.merger[i].type != AlreadyApplied)
			continue;
		if (i > 0 && rv.merger[i-1].type != Unchanged &&
		    rv.merger[i-1].type != Changed)
			rv.merger[i].type = Conflict;
		if (rv.merger[i+1].type != Unchanged &&
		    rv.merger[i+1].type != Changed &&
		    rv.merger[i+1].type != End)
			rv.merger[i].type = Conflict;
	}
	rv.conflicts = isolate_conflicts(af, bf, cf, csl1, csl2, words,
					 rv.merger, show_wiggles, &rv.wiggles);
	return rv;
}

static int printrange(FILE *out, struct file *f, int start, int len,
		      int offset)
{
	int lines = 0;
	while (len > 0 && start < f->elcnt) {
		struct elmnt e = f->list[start];
		printword(out, e);
		if (e.start[e.plen-1] == '\n' &&
		    offset > 0)
			lines++;
		offset--;
		start++;
		len--;
	}
	return lines;
}

static const char *conflict_types[] = {
	"", " border"," conflict"," wiggle" };

int print_merge(FILE *out, struct file *a, struct file *b, struct file *c,
		int words, struct merge *merger,
		struct merge *mpos, int streampos, int offsetpos)
{
	struct merge *m;
	int lineno = 1;
	int rv = 0;
	int offset = INT_MAX;
	int first_matched;

	for (m = merger; m->type != End ; m++) {
		struct merge *cm;
		if (do_trace)
			printf("[%s: %d-%d,%d-%d,%d-%d%s(%d,%d)]\n",
			       m->type==Unmatched ? "Unmatched" :
			       m->type==Unchanged ? "Unchanged" :
			       m->type==Extraneous ? "Extraneous" :
			       m->type==Changed ? "Changed" :
			       m->type==AlreadyApplied ? "AlreadyApplied" :
			       m->type==Conflict ? "Conflict":"unknown",
			       m->a, m->a+m->al-1,
			       m->b, m->b+m->bl-1,
			       m->c, m->c+m->cl-1,
			       conflict_types[m->in_conflict],
			       m->lo, m->hi);

		while (m->in_conflict) {
			/* need to print from 'hi' to 'lo' of next
			 * Unchanged which is < it's hi
			 */
			int found_conflict = 0;
			int st = 0, st1;
			if (m->in_conflict == 1)
				st = m->hi;
			st1 = st;

			if (m == mpos)
				offset = offsetpos;
			if (m->in_conflict == 1 && m->type == Unchanged)
				lineno += printrange(out, a, m->a+m->lo, m->hi - m->lo, offset - m->lo);

			if (m == mpos)
				rv = lineno;
			if (do_trace)
				for (cm = m; cm->in_conflict; cm++) {
					printf("{%s: %d-%d,%d-%d,%d-%d%s(%d,%d)}%s\n",
					       cm->type==Unmatched?"Unmatched":
					       cm->type==Unchanged?"Unchanged":
					       cm->type==Extraneous?"Extraneous":
					       cm->type==Changed?"Changed":
					       cm->type==AlreadyApplied?"AlreadyApplied":
					       cm->type==Conflict?"Conflict":"unknown",
					       cm->a, cm->a+cm->al-1,
					       cm->b, cm->b+cm->bl-1,
					       cm->c, cm->c+cm->cl-1,
					       conflict_types[m->in_conflict],
					       cm->lo, cm->hi,
					       (cm->type == Extraneous &&
						b->list[cm->b].start[0] == '\0') ?
					       b->list[cm->b].start+1: ""
					);
					if (cm->in_conflict == 1 && cm != m)
						break;
				}

			if (m->in_conflict == 1 &&
			    m[1].in_conflict == 1) {
				/* Nothing between two conflicts */
				m++;
				continue;
			}

			fputs(words ? "<<<---" : "<<<<<<< found\n", out);
			if (!words)
				lineno++;
			for (cm = m; cm->in_conflict; cm++) {
				if (cm == mpos && streampos == 0)
					offset = offsetpos;
				if (cm->type == Conflict)
					found_conflict = 1;
				if (cm->in_conflict == 1 && cm != m) {
					lineno += printrange(out, a, cm->a, cm->lo, offset);
					break;
				}
				lineno += printrange(out, a, cm->a+st1, cm->al-st1, offset-st1);
				st1 = 0;
				if (cm == mpos && streampos == 0)
					rv = lineno;
			}
			if (cm == mpos && streampos == 0)
				rv = lineno;
		restart:
			fputs(words ? "|||" : "||||||| expected\n", out);
			if (!words)
				lineno++;
			st1 = st;
			first_matched = 1;
			for (cm = m; cm->in_conflict; cm++) {
				if (cm->type == Extraneous &&
				    b->list[cm->b].start[0] == '\0') {
					/* This is a hunk header, skip it and possibly
					 * abort this section
					 */
					if (first_matched)
						continue;
					break;
				}
				if (cm->type != Unchanged && cm->type != Unmatched)
					first_matched = 0;
				if (cm == mpos && streampos == 1)
					offset = offsetpos;
				if (cm->in_conflict == 1 && cm != m) {
					lineno += printrange(out, a, cm->a, cm->lo, offset);
					break;
				}
				lineno += printrange(out, b, cm->b+st1, cm->bl-st1, offset-st1);
				st1 = 0;
				if (cm == mpos && streampos == 1)
					rv = lineno;
			}
			if (cm == mpos && streampos == 1)
				rv = lineno;
			fputs(words ? "===" : "=======\n", out);
			if (!words)
				lineno++;
			st1 = st;
			first_matched = 1;
			for (cm = m; cm->in_conflict; cm++) {
				if (cm->type == Extraneous &&
				    b->list[cm->b].start[0] == '\0') {
					/* This is a hunk header, skip it and possibly
					 * abort this section and restart.
					 */
					if (first_matched)
						continue;
					m = cm;
					/* If remaining merges are all
					 * Extraneous, Unchanged, or Unmatched,
					 * we don't need them.
					 */
					while (cm->in_conflict > 1 &&
					       (cm->type == Extraneous ||
						cm->type == Unmatched ||
						cm->type == Unchanged))
						cm ++;
					if (!cm->in_conflict)
						/* Nothing more to report */
						break;
					if (cm->in_conflict == 1 &&
					       (cm->type == Extraneous ||
						cm->type == Unmatched ||
						cm->type == Unchanged))
						/* border between conflicts, but
						 * still nothing to report.
						 */
						break;
					fputs(words ? ">>>" : ">>>>>>> replacement\n", out);
					fputs(words ? "<<<" : "<<<<<<< found\n", out);
					st = 0;
					goto restart;
				}
				if (cm->type != Unchanged && cm->type != Unmatched)
					first_matched = 0;
				if (cm == mpos && streampos == 2)
					offset = offsetpos;
				if (cm->in_conflict == 1 && cm != m) {
					if (cm->type == Unchanged)
						lineno += printrange(out, a, cm->a, cm->lo, offset);
					else
						lineno += printrange(out, c, cm->c, cm->cl, offset);
					break;
				}
				if (cm->type == Changed)
					st1 = 0; /* All of result of change must be printed */
				lineno += printrange(out, c, cm->c+st1, cm->cl-st1, offset-st1);
				st1 = 0;
				if (cm == mpos && streampos == 2)
					rv = lineno;
			}
			if (cm == mpos && streampos == 2)
				rv = lineno;
			if (!found_conflict) {
				/* This section was wiggled in successfully,
				 * but full conflict display was requested.
				 * So now print out the wiggled result as well.
				 */
				fputs(words ? "&&&" : "&&&&&&& resolution\n", out);
				if (!words)
					lineno++;
				st1 = st;
				for (cm = m; cm->in_conflict; cm++) {
					int last = 0;
					if (cm->in_conflict == 1 && cm != m)
						last = 1;
					switch (cm->type) {
					case Unchanged:
					case AlreadyApplied:
					case Unmatched:
						lineno += printrange(out, a, cm->a+st1,
								     last ? cm->lo : cm->al-st1, offset-st1);
						break;
					case Extraneous:
						break;
					case Changed:
						lineno += printrange(out, c, cm->c,
								     last ? cm->lo : cm->cl, offset);
						break;
					case Conflict:
					case End:
						assert(0);
					}
					if (last)
						break;
					st1 = 0;
				}
			}
			fputs(words ? "--->>>" : ">>>>>>> replacement\n", out);
			if (!words)
				lineno++;
			m = cm;
			if (m->in_conflict == 1 && m[1].in_conflict == 0) {
				/* End of a conflict, no conflict follows */
				if (m == mpos)
					offset = offsetpos;
				if (m->type == Unchanged)
					lineno += printrange(out, a, m->a+m->lo, m->hi-m->lo, offset-m->lo);
				if (m == mpos)
					rv = lineno;
				m++;
			}
		}

		/* there is always some non-conflict after a conflict,
		 * unless we hit the end
		 */
		if (m->type == End)
			break;

		if (do_trace) {
			printf("<<%s: %d-%d,%d-%d,%d-%d%s(%d,%d)>>\n",
			       m->type==Unmatched?"Unmatched":
			       m->type==Unchanged?"Unchanged":
			       m->type==Extraneous?"Extraneous":
			       m->type==Changed?"Changed":
			       m->type==AlreadyApplied?"AlreadyApplied":
			       m->type==Conflict?"Conflict":"unknown",
			       m->a, m->a+m->al-1,
			       m->b, m->b+m->bl-1,
			       m->c, m->c+m->cl-1,
			       conflict_types[m->in_conflict],
			       m->lo, m->hi);
		}
		if (m == mpos)
			offset = offsetpos;

		switch (m->type) {
		case Unchanged:
		case AlreadyApplied:
		case Unmatched:
			lineno += printrange(out, a, m->a, m->al, offset);
			break;
		case Extraneous:
			break;
		case Changed:
			lineno += printrange(out, c, m->c, m->cl, offset);
			break;
		case Conflict:
		case End:
			assert(0);
		}
		if (m == mpos)
			rv = lineno;
	}
	return rv;
}
