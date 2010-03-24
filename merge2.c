/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2005 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2010 Neil Brown <neilb@suse.de>
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

#include "wiggle.h"
#include <stdlib.h>
#include <malloc.h>

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
	return a<b?a:b;
}
static inline void assert(int a)
{
	if (!a) abort();
}

int check_alreadyapplied(struct file af, struct file cf,
			  struct merge *m)
{
	int i;
	if (m->al != m->cl)
		return 0;
	for (i=0; i<m->al; i++) {
		if (af.list[m->a+i].len != cf.list[m->c+i].len)
			return 0;
		if (strncmp(af.list[m->a+i].start,
			    cf.list[m->c+i].start,
			    af.list[m->a+i].len)!= 0)
			return 0;
	}
#if 0
	printf("already applied %d,%d,%d - %d,%d,%d\n",
	       m->a,m->b,m->c,m->al,m->bl,m->cl);
	printf(" %.10s - %.10s\n", af.list[m->a].start,
	       cf.list[m->c].start);
#endif
	m->type = AlreadyApplied;
	return 1;
}

inline int isolate_conflicts(struct file af, struct file bf, struct file cf,
			      struct csl *csl1, struct csl *csl2, int words,
			      struct merge *m)
{
	/* A conflict indicates that something is definitely wrong
	 * and so we need to be a bit suspicious of nearby apparent matches.
	 * To display a conflict effectively we expands it's effect to
	 * include any Extraneous, Unmatched or Changed text.
	 * Also, unless 'words', we need to include any partial lines
	 * in the Unchanged text that forms the border of a conflict.
	 *
	 * A Changed text may also border a conflict, but it can
	 * only border one conflict (where as an Unchanged can border
	 * a preceeding and a following conflict).
	 * The 'new' section of a Changed text appears in the
	 * conflict as does any part of the original before
	 * a newline.
	 *
	 */
	int i,j,k;
	int cnt = 0;

	for (i=0; m[i].type != End; i++)
		if (m[i].type == Conflict) {
			/* We have a conflict here.
			 * First search backwards for an Unchanged marking
			 * things as in_conflict.  Then find the
			 * cut-point in the Unchanged.  If there isn't one,
			 * keep looking.
			 *
			 * Then search forward doing the same thing.
			 */
			cnt++;
			m[i].in_conflict = 1;
			j = i;
			while (--j >= 0) {
				if (!m[j].in_conflict) {
					m[j].in_conflict = 1;
					m[j].lo = 0;
				} else if (m[j].type == Changed) {
					/* This can no longer form a border */
					m[j].lo = 0; m[j].hi = -1;
					/* We merge these conflicts and stop searching */
					cnt--;
					break;
				}

				if (m[j].type == Unchanged || m[j].type == Changed) {
					if (words) {
						m[j].hi = m[j].al;
						break;
					}
					/* need to find the last line-break, which
					 * might be after the last newline, if there
					 * is one, or might be at the start
					 */
					for (k=m[j].al; k>0; k--)
						if (ends_line(af.list[m[j].a+k-1]))
							break;
					if (k > 0)
						m[j].hi = k;
					else if ((m[j].a == 0 || ends_line(af.list[m[j].a-1])) &&
						 (m[j].b == 0 || ends_line(bf.list[m[j].b-1])) &&
						 (m[j].c == 0 || ends_line(cf.list[m[j].c-1])))
						m[j].hi = 0;
					else
						/* no start-of-line found... */
						m[j].hi = -1;
					if (m[j].hi > 0 && m[j].type == Changed) {
						/* this can only work if start is also a linke break */
						if ((m[j].a == 0 || ends_line(af.list[m[j].a-1])) &&
						    (m[j].b == 0 || ends_line(bf.list[m[j].b-1])) &&
						    (m[j].c == 0 || ends_line(cf.list[m[j].c-1])))
							/* ok */;
						else
							m[j].hi = -1;
					}
					if (m[j].hi >= 0)
						break;
				}
			}
#if 0
			if (j>=0 && m[j].in_conflict && m[j].type == Unchanged &&
			    m[j].hi == m[j].lo) {
				/* nothing to separate conflicts, merge them */
				m[j].lo = 0;
				m[j].hi = -1;
				cnt--;
			}
#endif
			/* now the forward search */
			for (j=i+1; m[j].type != End; j++) {
				m[j].in_conflict = 1;
				if (m[j].type == Unchanged || m[j].type == Changed) {
					m[j].hi = m[j].al;
					if (words) {
						m[j].lo = 0;
						break;
					}
					/* need to find a line-break, which might be at
					 * the very beginning, or might be after the
					 * first newline - if there is one
					 */
					if ((m[j].a == 0 || ends_line(af.list[m[j].a-1])) &&
					    (m[j].b == 0 || ends_line(bf.list[m[j].b-1])) &&
					    (m[j].c == 0 || ends_line(cf.list[m[j].c-1])))
						m[j].lo = 0;
					else {
						for (k = 0 ; k < m[j].al ; k++)
							if (ends_line(af.list[m[j].a+k]))
								break;
						if (k<m[j].al)
							m[j].lo = k+1;
						else
							/* no start-of-line found */
							m[j].lo = m[j].al+1;
					}
					if (m[j].lo <= m[j].al+1 && m[j].type == Changed) {
						/* this can only work if the end is a line break */
						if (ends_line(af.list[m[j].a+m[j].al-1]) &&
						    ends_line(bf.list[m[j].b+m[j].bl-1]) &&
						    ends_line(cf.list[m[j].c+m[j].cl-1]))
							/* ok */;
						else
							m[j].lo = m[j].al+1;
					}
					if (m[j].lo < m[j].al+1)
						break;
				}
			}
			i = j - 1;
		}
	return cnt;
}


struct ci make_merger(struct file af, struct file bf, struct file cf,
		      struct csl *csl1, struct csl *csl2, int words,
		      int ignore_already)
{
	/* find the wiggles and conflicts between csl1 and csl2
	 */
	struct ci rv;
	int i,l;
	int a,b,c,c1,c2;
	int wiggle_found = 0;

	rv.conflicts = rv.wiggles = rv.ignored = 0;

	for (i=0; csl1[i].len; i++);
	l = i;
	for (i=0; csl2[i].len; i++);
	l += i;
	/* maybe a bit of slack at each end */
	l = l* 4 + 10;

	rv.merger = malloc(sizeof(struct merge)*l);
	if (!rv.merger)
		return rv;

	a=b=c=c1=c2 = 0;
	i = 0;
	while (1) {
		int match1, match2;
		match1 = (a>=csl1[c1].a && b >= csl1[c1].b); /* c1 doesn't match */
		match2 = (b>=csl2[c2].a && c >= csl2[c2].b);

		rv.merger[i].a = a;
		rv.merger[i].b = b;
		rv.merger[i].c = c;
		rv.merger[i].c1 = c1;
		rv.merger[i].c2 = c2;
		rv.merger[i].in_conflict = 0;

		if (!match1 && match2) {
			if (a < csl1[c1].a) {
				/* some unmatched text */
				rv.merger[i].type = Unmatched;
				rv.merger[i].al = csl1[c1].a - a;
				rv.merger[i].bl = 0;
				rv.merger[i].cl = 0;
				wiggle_found ++;
			} else {
				int newb;
				int j;
				assert(b<csl1[c1].b);
				/* some Extraneous text */
				/* length is min of unmatched on left
				 * and matched on right
				 */
				rv.merger[i].type = Extraneous;
				rv.merger[i].al = 0;
				rv.merger[i].cl =
					rv.merger[i].bl =
					min(csl1[c1].b - b,
					    csl2[c2].len - (b-csl2[c2].a));
				newb = b+rv.merger[i].bl;
				for (j=b; j<newb; j++) {
					if (bf.list[j].start[0] == '\0') {
						if (wiggle_found > 1)
							rv.wiggles++;
						wiggle_found = 0;
					} else
						wiggle_found ++;
				}
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
			    check_alreadyapplied(af,cf,&rv.merger[i]))
				rv.ignored ++;
		}
		a += rv.merger[i].al;
		b += rv.merger[i].bl;
		c += rv.merger[i].cl;
		i++;

		while (csl1[c1].a + csl1[c1].len <= a && csl1[c1].len) c1++;
		assert(csl1[c1].b + csl1[c1].len >= b);
		while (csl2[c2].b + csl2[c2].len <= c && csl2[c2].len) c2++;
		assert(csl2[c2].a + csl2[c2].len >= b);
		if (csl1[c1].len == 0 && csl2[c2].len == 0 &&
		    a == csl1[c1].a && b == csl1[c1].b &&
		    b == csl2[c2].a && c == csl2[c2].b)
			break;
	}
	rv.merger[i].type = End;
	rv.merger[i].in_conflict = 0;
	rv.conflicts = isolate_conflicts(af,bf,cf,csl1,csl2,words,rv.merger);
	if (wiggle_found)
		rv.wiggles++;
	return rv;
}

void printrange(FILE *out, struct file *f, int start, int len)
{
	while (len> 0) {
		printword(out, f->list[start]);
		start++;
		len--;
	}
}

struct ci print_merge2(FILE *out, struct file *a, struct file *b, struct file *c,
		       struct csl *c1, struct csl *c2,
		       int words, int ignore_already)
{
	struct ci rv = make_merger(*a, *b, *c, c1, c2, 
				   words, ignore_already);
	struct merge *m;

	for (m=rv.merger; m->type != End ; m++) {
		struct merge *cm;
#if 1
		static int v= -1;
		if (v == -1) {
			if (getenv("WiggleVerbose"))
				v = 1;
			else
				v = 0;
		}
		if (v)
		printf("[%s: %d-%d,%d-%d,%d-%d%s(%d,%d)]\n",
		       m->type==Unmatched?"Unmatched":
		       m->type==Unchanged?"Unchanged":
		       m->type==Extraneous?"Extraneous":
		       m->type==Changed?"Changed":
		       m->type==AlreadyApplied?"AlreadyApplied":
		       m->type==Conflict?"Conflict":"unknown",
		       m->a, m->a+m->al-1,
		       m->b, m->b+m->bl-1,
		       m->c, m->c+m->cl-1, m->in_conflict?" in_conflict":"",
		       m->lo, m->hi);
#endif
		while (m->in_conflict) {
			/* need to print from 'hi' to 'lo' of next
			 * Unchanged which is < it's hi
			 */
			int st = m->hi;
			if (m->hi <= m->lo)
				st = 0;

			if (m->type == Unchanged)
				printrange(out, a, m->a+m->lo, m->hi - m->lo);

#if 1
		if (v)
			for (cm=m; cm->in_conflict; cm++) {
				printf("{%s: %d-%d,%d-%d,%d-%d%s(%d,%d)}\n",
				       cm->type==Unmatched?"Unmatched":
				       cm->type==Unchanged?"Unchanged":
				       cm->type==Extraneous?"Extraneous":
				       cm->type==Changed?"Changed":
				       cm->type==AlreadyApplied?"AlreadyApplied":
				       cm->type==Conflict?"Conflict":"unknown",
				       cm->a, cm->a+cm->al-1,
				       cm->b, cm->b+cm->bl-1,
				       cm->c, cm->c+cm->cl-1, cm->in_conflict?" in_conflict":"",
				       cm->lo, cm->hi);
				if ((cm->type == Unchanged ||cm->type == Changed) && cm != m && cm->lo < cm->hi)
					break;
			}
#endif

			fputs(words?"<<<---":"<<<<<<<\n", out);
			for (cm=m; cm->in_conflict; cm++) {
				if ((cm->type == Unchanged || cm->type == Changed) && cm != m && cm->lo < cm->hi) {
					printrange(out, a, cm->a, cm->lo);
					break;
				}
				printrange(out, a, cm->a+st, cm->al-st);
				st = 0;
			}
			fputs(words?"|||":"|||||||\n", out);
			st = m->hi;
			for (cm=m; cm->in_conflict; cm++) {
				if ((cm->type == Unchanged || cm->type == Changed) && cm != m && cm->lo < cm->hi) {
					printrange(out, b, cm->b, cm->lo);
					break;
				}
				printrange(out, b, cm->b+st, cm->bl-st);
				st = 0;
			}
			fputs(words?"===":"=======\n", out);
			st = m->hi;
			for (cm=m; cm->in_conflict; cm++) {
				if (cm->type == Unchanged && cm != m && cm->lo < cm->hi) {
					printrange(out, c, cm->c, cm->lo);
					break;
				}
				if (cm->type == Changed)
					st = 0; /* All of result of change must be printed */
				printrange(out, c, cm->c+st, cm->cl-st);
				st = 0;
			}
			fputs(words?"--->>>":">>>>>>>\n", out);
			m = cm;
			if (m->in_conflict && m->hi >= m->al) {
				assert(m->type == Unchanged);
				printrange(out, a, m->a+m->lo, m->hi-m->lo);
				m++;
			}
		}

		/* there is always some non-conflict after a conflict,
		 * unless we hit the end
		 */
		if (m->type == End)
			break;
#if 1
		if (v) {
			printf("<<%s: %d-%d,%d-%d,%d-%d%s(%d,%d)>>\n",
			       m->type==Unmatched?"Unmatched":
			       m->type==Unchanged?"Unchanged":
			       m->type==Extraneous?"Extraneous":
			       m->type==Changed?"Changed":
			       m->type==AlreadyApplied?"AlreadyApplied":
			       m->type==Conflict?"Conflict":"unknown",
			       m->a, m->a+m->al-1,
			       m->b, m->b+m->bl-1,
			       m->c, m->c+m->cl-1, m->in_conflict?" in_conflict":"",
			       m->lo, m->hi);
		}
#endif
		switch(m->type) {
		case Unchanged:
		case AlreadyApplied:
		case Unmatched:
			printrange(out, a, m->a, m->al);
			break;
		case Extraneous:
			break;
		case Changed:
			printrange(out, c, m->c, m->cl);
			break;
		case Conflict:
		case End: assert(0);
		}
	}
	return rv;
}
