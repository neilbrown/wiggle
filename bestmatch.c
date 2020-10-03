/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2011-2013 Neil Brown <neilb@suse.de>
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

/*
 * Find the best match for a patch against a file.  A patch is a
 * sequence of chunks each of which is expected to match a particular
 * locality of the file.  So we expect big gaps between where chunks
 * match, but only small gaps within chunks.
 *
 * The matching algorithm is similar to that in diff.c, so you should
 * understand that first.  However it takes fewer shortcuts and
 * analyses cost in a more detailed way.
 *
 * We walk the whole matrix in a breadth first fashion following a
 * 'front' on which x+y is constant.  Along this front we examine each
 * diagonal.  For each point we calculate a 'value' for the match so
 * far.  This will be in some particular chunk.  For each chunk we
 * separately record the best value found so far, and where it was.
 * To choose a new value for each point we calculate based on the
 * previous value on each neighbouring diagonal and on this diagonal.
 *
 * This can result is a set of 'best' matches for each chunk which are
 * not in the same order that the chunks initially were.  This
 * probably isn't desired, so we choose a 'best' best match and
 * recurse on each side of it.
 *
 * The quality of a match is a somewhat complex function that is
 * roughly 3 times the number of matching symbols minus the number
 * of replaced, added, or deleted.  This seems to work.
 *
 * For any point, the best possible score using that point
 * is a complete diagonal to the nearest edge.  We ignore points
 * which cannot contibute to a better overall score.
 *
 * As this is a fairly expensive search we remove uninteresting
 * symbols before searching.  Specifically we only keep alphanumeric
 * (plus '_') strings.  Spaces and punctuation is ignored.  This should
 * contain enough information to achieve a reliable match while scanning
 * many fewer symbols.
 */

#include	<ctype.h>
#include	<stdlib.h>
#include	"wiggle.h"

/* This structure keeps track of the current match at each point.
 * It holds the start of the match as x,k where k is the
 * diagonal, so y = x-k.
 * Also the length of the match so far.
 * If l == 0, there is no match.
 */
struct v {
	int x, y;  /* location of start of match */
	int val;  /* value of match from x,y to here */
	int k;    /* diagonal of last match - if val > 0 */
	int inmatch; /* 1 if last point was a match */
	int c; /* chunk number */
};

/*
 * Here we must determine the 'value' of a partial match.
 * The input parameters are:
 *   length - the total number of symbols matched
 *   errs  - the total number of insertions or deletions
 *   dif   - the absolute difference between number of insertions and deletions.
 *
 * In general we want length to be high, errs to be low, and dif to be low.
 * Particular questions that must be answered include:
 *  - When does adding an extra symbol after a small gap improve the match
 *  - When does a match become so bad that we would rather start again.
 *
 * We would like symmetry in our answers so that a good sequence with
 * an out-rider on one end is evaluated the same as a good sequence
 * with an out-rider on the other end.
 *
 * However to do this we cannot really use the value of the good
 * sequence to weigh in the out-riders favour as in the case of a
 * leading outrider, we do not yet know the value of the good
 * sequence.
 *
 * First, we need an arbitrary number, X, to say "Given a single
 * symbol, after X errors, we forget that symbol".  5 seems a good
 * number.
 *
 * Next we need to understand how replacements compare to insertions
 * or deletions.  Probably a replacement is the same cost as an
 * insertion or deletion.  Finally, a few large stretches are better
 * then lots of little ones, so the number of disjoint stretches
 * should be kept low.
 *
 * So:
 *   The first match sets the value to 6.
 *   Each consecutive match adds 3
 *   A non-consecutive match which value is still +ve adds 2
 *   Each non-match subtracts one unless it is the other half of a replacement.
 *   A value of 0 causes us to forget where we are and start again.
 *
 * We need to not only assess the value at a particular location, but
 * also assess the maximum value we could get if all remaining symbols
 * matched, to help exclude parts of the matrix.  The value of that
 * possibility is 6 times the number of remaining symbols, -1 if we
 * just had a match.
 */
/* dir == 0 for match, 1 for k increase, -1 for k decrease */
static inline void update_value(struct v *v, int dir, int k, int x)
{
	if (dir == 0) {
		if (v->val <= 0) {
			v->x = x-1;
			v->y = x-k-1;
			v->inmatch = 0;
			v->val = 4;
		}
		v->val += 2+v->inmatch;
		v->inmatch = 1;
		v->k = k;
	} else if (v->val > 0) {
		v->inmatch = 0;
		if (dir * (v->k - k) > 0) {
			/* other half of replacement */
		} else {
			v->val -= 1;
		}
	}
}

/* Calculate the best possible value that this 'struct v'
 * could reach if there are 'max' symbols remaining
 * that could possibly be matches.
 */
static inline int best_val(struct v *v, int max)
{
	if (v->val <= 0)
		return 4+max*3-1;
	else
		return max*3-1+v->inmatch+v->val;
}

struct best {
	int xlo, ylo;
	int xhi, yhi;
	int val;
};

static inline int min(int a, int b)
{
	return a < b ? a : b;
}

static void find_best(struct file *a, struct file *b,
		      int alo, int ahi,
		      int blo, int bhi, struct best *best)
{
	int klo, khi, k;
	int f;

	struct v *valloc = wiggle_xmalloc(sizeof(struct v)*((ahi-alo)+(bhi-blo)+5));
	struct v *v = valloc + (bhi-alo+2);

	k = klo = khi = alo-blo;
	f = alo+blo; /* front that moves forward */
	v[k].val = 0;
	v[k].c = -1;

	while (f < ahi+bhi) {
		int x, y;

		f++;
		for (k = klo+1; k <= khi-1 ; k += 2) {
			struct v vnew, vnew2;
			x = (k+f)/2;
			y = x-k;
			/* first consider the diagonal - if possible
			 * it is always preferred
			 */
			if (match(&a->list[x-1], &b->list[y-1])) {
				vnew = v[k];
				update_value(&v[k], 0, k, x);
				if (v[k].c < 0)
					abort();
				if (v[k].val > best[v[k].c].val) {
					int chunk = v[k].c;
					best[chunk].xlo = v[k].x;
					best[chunk].ylo = v[k].y;
					best[chunk].xhi = x;
					best[chunk].yhi = y;
					best[chunk].val = v[k].val;
				}
			} else {
				/* First consider a y-step: adding a
				 * symbol from B */
				vnew = v[k+1];
				update_value(&vnew, -1, k, x);
				/* might cross a chunk boundary */
				if (b->list[y-1].len && b->list[y-1].start[0] == 0) {
					vnew.c = atoi(b->list[y-1].start+1);
					vnew.val = 0;
				}

				/* Not consider an x-step: deleting
				 * a symbol.  This cannot be a chunk
				 * boundary as there aren't any in 'A'
				 */
				vnew2 = v[k-1];
				update_value(&vnew2, 1, k, x);

				/* Now choose the best. */
				if (vnew2.val > vnew.val)
					v[k] = vnew2;
				else
					v[k] = vnew;
			}
		}
		/* extend or contract range */
		klo--;
		v[klo] = v[klo+1];
		x = (klo+f)/2; y = x-klo;
		update_value(&v[klo], -1, klo, x);
		if (y <= bhi && b->list[y-1].len && b->list[y-1].start[0] == 0) {
			v[klo].c = atoi(b->list[y-1].start+1);
			v[klo].val = 0;
		}
		while (klo+2 < (ahi-bhi) &&
		       (y > bhi ||
			(best_val(&v[klo], min(ahi-x, bhi-y)) < best[v[klo].c].val &&
			 best_val(&v[klo+1], min(ahi-x, bhi-y+1)) < best[v[klo+1].c].val
			)
		)) {
			klo += 2;
			x = (klo+f)/2; y = x-klo;
		}

		khi++;
		v[khi] = v[khi-1];
		x = (khi+f)/2; y = x - khi;
		update_value(&v[khi], -1, khi, x);
		while (khi-2 > (ahi-bhi) &&
		       (x > ahi ||
			(v[khi].c >= 0 &&
			 best_val(&v[khi], min(ahi-x, bhi-y)) < best[v[khi].c].val &&
			 best_val(&v[khi-1], min(ahi-x+1, bhi-y)) < best[v[khi].c].val
			)
		)) {
			khi -= 2;
			x = (khi+f)/2; y = x - khi;
		}

	}
	free(valloc);
}

/*
 * Reduce a file by discarding less interesting words
 * Words that end with a newline are interesting (so all words
 * in line-mode are interesting) and words that start with
 * and alphanumeric are interesting.  This excludes spaces and
 * special characters in word mode
 * Doing a best-fit comparison on only interesting words is
 * much faster than on all words, and is nearly as good
 */

static inline int is_skipped(struct elmnt e)
{
	return !(ends_line(e) ||
		 isalnum(e.start[0]) ||
		 e.start[0] == '_');
}

static struct file reduce(struct file orig)
{
	int cnt = 0;
	int i;
	struct file rv;

	for (i = 0; i < orig.elcnt; i++)
		if (!is_skipped(orig.list[i]))
			cnt++;

	if (cnt == orig.elcnt)
		return orig;

	rv.elcnt = cnt;
	rv.list = wiggle_xmalloc(cnt*sizeof(struct elmnt));
	cnt = 0;
	for (i = 0; i < orig.elcnt; i++)
		if (!is_skipped(orig.list[i]))
			rv.list[cnt++] = orig.list[i];
	return rv;
}

/* Given a list of best matches between a1 and b1 which are
 * subsets of a2 and b2, convert that list to indexes into a2/b2
 *
 * When we find the location in a2/b2, we expand to include all
 * immediately surrounding words which were skipped
 */
static void remap(struct best *best, int cnt,
		  struct file a1, struct file b1,
		  struct file a2, struct file b2)
{
	int b;
	int pa, pb; /* pointers into the a2 and b2 arrays */

	pa = pb = 0;

	if (a1.elcnt == 0 && a2.elcnt == 0)
		return;

	for (b = 1; b < cnt; b++)
	    if (best[b].val > 0) {
		while (pa < a2.elcnt &&
		       a2.list[pa].start != a1.list[best[b].xlo].start)
			pa++;
		if (pa == a2.elcnt)
			abort();
		while (pb < b2.elcnt &&
		       b2.list[pb].start != b1.list[best[b].ylo].start)
			pb++;
		if (pb == b2.elcnt)
			abort();

		/* pa,pb is the start of this best bit.  Step
		 * backward over ignored words
		 */
		while (pa > 0 && is_skipped(a2.list[pa-1]))
			pa--;
		while (pb > 0 && is_skipped(b2.list[pb-1]))
			pb--;

		if (pa <= 0)
			pa = 1;
		if (pb <= 0)
			pb = 1;

		best[b].xlo = pa;
		best[b].ylo = pb;

		while (pa < a2.elcnt &&
		       (pa == 0 || (a2.list[pa-1].start
				    != a1.list[best[b].xhi-1].start)))
			pa++;
		if (pa == a2.elcnt && best[b].xhi != a1.elcnt)
			abort();
		while (pb < b2.elcnt &&
		       (pb == 0 || (b2.list[pb-1].start
				    != b1.list[best[b].yhi-1].start)))
			pb++;
		if (pb == b2.elcnt && best[b].yhi != b1.elcnt)
			abort();

		/* pa,pb is now the end of the best bit.
		 * Step pa,pb forward over ignored words.
		 */
		while (pa < a2.elcnt && is_skipped(a2.list[pa]))
			pa++;
		while (pb < b2.elcnt && is_skipped(b2.list[pb]))
			pb++;
		best[b].xhi = pa;
		best[b].yhi = pb;
	}
}

static void find_best_inorder(struct file *a, struct file *b,
			      int alo, int ahi, int blo, int bhi,
			      struct best *best, int bestlo, int besthi)
{
	/* make sure the best matches we find are inorder.
	 * If they aren't we find a overall best, and
	 * recurse either side of that
	 */
	int i;
	int bad = 0;
	int bestval, bestpos = 0;

	for (i = bestlo; i < besthi; i++)
		best[i].val = 0;
	find_best(a, b, alo, ahi, blo, bhi, best);
	for (i = bestlo + 1; i < besthi; i++)
		if (best[i-1].val > 0 &&
		    best[i].val > 0 &&
		    best[i-1].xhi >= best[i].xlo)
			bad = 1;

	if (!bad)
		return;
	bestval = 0;
	for (i = bestlo; i < besthi; i++)
		if (best[i].val > bestval) {
			bestval = best[i].val;
			bestpos = i;
		}
	if (bestpos > bestlo) {
		/* move top down below chunk marker */
		int y = best[bestpos].ylo;
		while (b->list[y].start[0])
			y--;
		find_best_inorder(a, b,
				  alo, best[bestpos].xlo,
				  blo, y,
				  best, bestlo, bestpos);
	}
	if (bestpos < besthi-1) {
		/* move bottom up to chunk marker */
		int y = best[bestpos].yhi;
		while (b->list[y].start[0])
			y++;
		find_best_inorder(a, b,
				  best[bestpos].xhi, ahi,
				  y, bhi,
				  best, bestpos+1, besthi);
	}
}

struct csl *wiggle_pdiff(struct file a, struct file b, int chunks)
{
	struct csl *csl1, *csl2;
	struct best *best = wiggle_xmalloc(sizeof(struct best)*(chunks+1));
	int i;
	struct file asmall, bsmall;
	int xmin;

	asmall = reduce(a);
	bsmall = reduce(b);

	for (i = 0; i < chunks+1; i++)
		best[i].val = 0;
	find_best_inorder(&asmall, &bsmall,
			  0, asmall.elcnt, 0, bsmall.elcnt,
			  best, 1, chunks+1);
	remap(best, chunks+1, asmall, bsmall, a, b);
	if (asmall.list != a.list)
		free(asmall.list);
	if (bsmall.list != b.list)
		free(bsmall.list);

	csl1 = NULL;
	xmin = 0;
	for (i = 1; i <= chunks; i++)
		if (best[i].val > 0) {
			/* If we there are any newlines in the hunk before
			 * ylo, then extend xlo back that many newlines if
			 * possible and wiggle_diff_partial the extended regions.
			 */
			int lines = 0;
			int ylo = best[i].ylo;
			int yhi;
			while (ylo > 0 && b.list[ylo-1].start[0]) {
				ylo--;
				lines += !!ends_line(b.list[ylo]);
			}
			if (lines) {
				int xlo = best[i].xlo;
				while (lines && xlo > xmin) {
					xlo--;
					lines -= !!ends_line(a.list[xlo]);
				}
				while (xlo > xmin && !ends_line(a.list[xlo-1]))
					xlo--;
				csl2 = wiggle_diff_partial(a, b,
							   xlo, best[i].xlo,
							   ylo, best[i].ylo);
				csl1 = wiggle_csl_join(csl1, csl2);
			}

			/* Now wiggle_diff_partial the good bit of the hunk with the
			 * good match
			 */
			csl2 = wiggle_diff_partial(a, b,
						   best[i].xlo, best[i].xhi,
						   best[i].ylo, best[i].yhi);
			csl1 = wiggle_csl_join(csl1, csl2);

			/* Now if there are newlines at the end of the
			 * hunk that weren't matched, find as many in
			 * original and wiggle_diff_partial those bits
			 */
			lines = 0;
			yhi = best[i].yhi;
			while (yhi < b.elcnt && b.list[yhi].start[0]) {
				lines += !!ends_line(b.list[yhi]);
				yhi++;
			}
			xmin = best[i].xhi;
			if (lines) {
				int xhi = best[i].xhi;
				int xmax = a.elcnt;
				if (i < chunks)
					xmax = best[i+1].xlo;
				while (lines && xhi < xmax) {
					lines -= !!ends_line(a.list[xhi]);
					xhi++;
				}
				csl2 = wiggle_diff_partial(a, b,
							   best[i].xhi, xhi,
							   best[i].yhi, yhi);
				csl1 = wiggle_csl_join(csl1, csl2);
				xmin = xhi;
			}
		} else {
			/* FIXME we just lost a hunk!! */;
		}
	if (csl1) {
		for (csl2 = csl1; csl2->len; csl2++)
			;
		csl2->a = a.elcnt;
		csl2->b = b.elcnt;
	} else {
		csl1 = wiggle_xmalloc(sizeof(*csl1));
		csl1->len = 0;
		csl1->a = a.elcnt;
		csl1->b = b.elcnt;
	}
	free(best);
	return csl1;
}
