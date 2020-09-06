/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (C) 2011-2013 Neil Brown <neilb@suse.de>
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
 * Calculate longest common subsequence between two sequences
 *
 * Each sequence contains strings with
 *   hash start length
 * We produce a list of tripples: a b len
 * where A and B point to elements in the two sequences, and len is the number
 * of common elements there.  The list is terminated by an entry with len==0.
 *
 * This is roughly based on
 *   "An O(ND) Difference Algorithm and its Variations", Eugene Myers,
 *   Algorithmica Vol. 1 No. 2, 1986, pp. 251-266;
 * http://xmailserver.org/diff2.pdf
 *
 * However we don't run the basic algorithm both forward and backward until
 * we find an overlap as Myers suggests.  Rather we always run forwards, but
 * we record the location of the (possibly empty) snake that crosses the
 * midline.  When we finish, this recorded location for the best path shows
 * us where to divide and find further midpoints.
 *
 * In brief, the algorithm is as follows.
 *
 * Imagine a Cartesian Matrix where x co-ordinates correspond to symbols in
 * the first sequence (A, length a) and y co-ordinates correspond to symbols
 * in the second sequence (B, length b).  At the origin we have the first
 * sequence.
 * Movement in the x direction represents deleting the symbol as that point,
 * so from x=i-1 to x=i deletes symbol i from A.

 * Movement in the y direction represents adding the corresponding symbol
 * from B.  So to move from the origin 'a' spaces along X and then 'b' spaces
 * up Y will remove all of the first sequence and then add all of the second
 * sequence.  Similarly moving firstly up the Y axis, then along the X
 * direction will add the new sequence, then remove the old sequence.  Thus
 * the point a,b represents the second sequence and a part from 0,0 to a,b
 * represent an sequence of edits to change A into B.
 *
 * There are clearly many paths from 0,0 to a,b going through different
 * points in the matrix in different orders.  At some points in the matrix
 * the next symbol to be added from B is the same as the next symbol to be
 * removed from A.  At these points we can take a diagonal step to a new
 * point in the matrix without actually changing any symbol.  A sequence of
 * these diagonal steps is called a 'snake'.  The goal then is to find a path
 * of x-steps (removals), y-steps (additions) and diagonals (steps and
 * snakes) where the number of (non-diagonal) steps is minimal.
 *
 * i.e. we aim for as many long snakes as possible.
 * If the total number of 'steps' is called the 'cost', we aim to minimise
 * the cost.
 *
 * As storing the whole matrix in memory would be prohibitive with large
 * sequences we limit ourselves to linear storage proportional to a+b and
 * repeat the search at most log2(a+b) times building up the path as we go.
 * Specifically we perform a search on the full matrix and record where each
 * path crosses the half-way point. i.e. where x+y = (a+b)/2 (== mid).  This
 * tells us the mid point of the best path.  We then perform two searches,
 * one on each of the two halves and find the 1/4 and 3/4 way points.  This
 * continues recursively until we have all points.
 *
 * The storage is an array v of 'struct v'.  This is indexed by the
 * diagonal-number k = x-y.  Thus k can be negative and the array is
 * allocated to allow for that.  During the search there is an implicit value
 * 'c' which is the cost (length in steps) of all the paths currently under
 * consideration.
 * v[k] stores details of the longest reaching path of cost c that finishes
 *      on diagonal k.  "longest reaching" means "finishes closest to a,b".
 * Details are:
 *   The location of the end point.  'x' is stored. y = x - k.
 *   The diagonal of the midpoint crossing. md is stored. x = (mid + md)/2
 *                                                        y = (mid - md)/2
 *                                                          = x - md
 *   (note: md is a diagonal so md = x-y.  mid is an anti-diagonal: mid = x+y)
 *   The number of 'snakes' in the path (l).  This is used to allocate the
 *   array which will record the snakes and to terminate recursion.
 *
 * A path with an even cost (c is even) must end on an even diagonal (k is
 * even) and when c is odd, k must be odd.  So the v[] array is treated as
 * two sub arrays, the even part and the odd part.  One represents paths of
 * cost 'c', the other paths of cost c-1.
 *
 * Initially only v[0] is meaningful and there are no snakes.  We firstly
 * extend all paths under consideration with the longest possible snake on
 * that diagonal.
 *
 * Then we increment 'c' and calculate for each suitable 'k' whether the best
 * path to diagonal k of cost c comes from taking an x-step from the c-1 path
 * on diagonal k-1, or from taking a y-step from the c-1 path on diagonal
 * k+1.  Obviously we need to avoid stepping out of the matrix.  Finally we
 * check if the 'v' array can be extended or reduced at the boundaries.  If
 * we hit a border we must reduce.  If the best we could possibly do on that
 * diagonal is less than the worst result from the current leading path, then
 * we also reduce.  Otherwise we extend the range of 'k's we consider.
 *
 * We continue until we find a path has reached a,b.  This must be a minimal
 * cost path (cost==c).  At this point re-check the end of the snake at the
 * midpoint and report that.
 *
 * This all happens recursively for smaller and smaller subranges stopping
 * when we examine a submatrix and find that it contains no snakes.  As we
 * are usually dealing with sub-matrixes we are not walking from 0,0 to a,b
 * from alo,blo to ahi,bhi - low point to high point.  So the initial k is
 * alo-blo, not 0.
 *
 */

#include	"wiggle.h"
#include	<stdlib.h>

struct v {
	int x;	/* x location of furthest reaching path of current cost */
	int md; /* diagonal location of midline crossing */
	int l;  /* number of continuous common sequences found so far */
};

static int find_common(struct file *a, struct file *b,
			    int *alop, int *ahip,
			    int *blop, int *bhip,
			    struct v *v)
{
	/* Examine matrix from alo to ahi and blo to bhi.
	 * i.e. including alo and blo, but less than ahi and bhi.
	 * Finding longest subsequence and
	 * return new {a,b}{lo,hi} either side of midline.
	 * i.e. mid = ( (ahi-alo) + (bhi-blo) ) / 2
	 *      alo+blo <= mid <= ahi+bhi
	 *  and alo,blo to ahi,bhi is a common (possibly empty)
	 *  subseq - a snake.
	 *
	 * v is scratch space which is indexable from
	 * alo-bhi to ahi-blo inclusive.
	 * i.e. even though there is no symbol at ahi or bhi, we do
	 * consider paths that reach there as they simply cannot
	 * go further in that direction.
	 *
	 * Return the number of snakes found.
	 */

	int klo, khi;
	int alo = *alop;
	int ahi = *ahip;
	int blo = *blop;
	int bhi = *bhip;

	int mid = (ahi+bhi+alo+blo)/2;

	/* 'worst' is the worst-case extra cost that we need
	 * to pay before reaching our destination.  It assumes
	 * no more snakes in the furthest-reaching path so far.
	 * We use this to know when we can trim the extreme
	 * diagonals - when their best case does not improve on
	 * the current worst case.
	 */
	int worst = (ahi-alo)+(bhi-blo);

	klo = khi = alo-blo;
	v[klo].x = alo;
	v[klo].l = 0;

	while (1) {
		int x, y;
		int cost;
		int k;

		/* Find the longest snake extending on each current
		 * diagonal, and record if it crosses the midline.
		 * If we reach the end, return.
		 */
		for (k = klo ; k <= khi ; k += 2) {
			int snake = 0;

			x = v[k].x;
			y = x-k;
			if (y > bhi)
				abort();

			/* Follow any snake that is here */
			while (x < ahi && y < bhi &&
			       match(&a->list[x], &b->list[y])
				) {
				x++;
				y++;
				snake = 1;
			}

			/* Refine the worst-case remaining cost */
			cost = (ahi-x)+(bhi-y);
			if (cost < worst)
				worst = cost;

			/* Check for midline crossing */
			if (x+y >= mid &&
			     v[k].x + v[k].x-k <= mid)
				v[k].md = k;

			v[k].x = x;
			v[k].l += snake;

			if (cost == 0) {
				/* OK! We have arrived.
				 * We crossed the midpoint on diagonal v[k].md
				 */
				if (x != ahi)
					abort();

				/* The snake could start earlier than the
				 * midline.  We cannot just search backwards
				 * as that might find the wrong path - the
				 * greediness of the diff algorithm is
				 * asymmetric.
				 * We could record the start of the snake in
				 * 'v', but we will find the actual snake when
				 * we recurse so there is no need.
				 */
				x = (v[k].md+mid)/2;
				y = x-v[k].md;

				*alop = x;
				*blop = y;

				/* Find the end of the snake using the same
				 * greedy approach as when we first found the
				 * snake
				 */
				while (x < ahi && y < bhi &&
				       match(&a->list[x], &b->list[y])
					) {
					x++;
					y++;
				}
				*ahip = x;
				*bhip = y;

				return v[k].l;
			}
		}

		/* No success with previous cost, so increment cost (c) by 1
		 * and for each other diagonal, set from the end point of the
		 * diagonal on one side of it or the other.
		 */
		for (k = klo+1; k <= khi-1 ; k += 2) {
			if (v[k-1].x+1 > ahi) {
				/* cannot step to the right from previous
				 * diagonal as there is no room.
				 * So step up from next diagonal.
				 */
				v[k] = v[k+1];
			} else if (v[k+1].x - k > bhi || v[k-1].x+1 >= v[k+1].x) {
				/* Cannot step up from next diagonal as either
				 * there is no room, or doing so wouldn't get us
				 * as close to the endpoint.
				 * So step to the right.
				 */
				v[k] = v[k-1];
				v[k].x++;
			} else {
				/* There is room in both directions, but
				 * stepping up from the next diagonal gets us
				 * closer
				 */
				v[k] = v[k+1];
			}
		}

		/* Now we need to either extend or contract klo and khi
		 * so they both change parity (odd vs even).
		 * If we extend we need to step up (for klo) or to the
		 * right (khi) from the adjacent diagonal.  This is
		 * not possible if we have hit the edge of the matrix, and
		 * not sensible if the new point has a best case remaining
		 * cost that is worse than our current worst case remaining
		 * cost.
		 * The best-case remaining cost is the absolute difference
		 * between the remaining number of additions and the remaining
		 * number of deletions - and assumes lots of snakes.
		 */
		/* new location if we step up from klo to klo-1*/
		x = v[klo].x; y = x - (klo-1);
		cost = abs((ahi-x)-(bhi-y));
		klo--;
		if (y <= bhi && cost <= worst) {
			/* Looks acceptable - step up. */
			v[klo] = v[klo+1];
		} else do {
				klo += 2;
				x = v[klo].x; y = x - (klo-1);
				cost = abs((ahi-x)-(bhi-y));
			} while (cost > worst);

		/* new location if we step to the right from khi to khi+1 */
		x = v[khi].x+1; y = x - (khi+1);
		cost = abs((ahi-x)-(bhi-y));
		khi++;
		if (x <= ahi && cost <= worst) {
			/* Looks acceptable - step to the right */
			v[khi] = v[khi-1];
			v[khi].x++;
		} else do {
				khi -= 2;
				x = v[khi].x+1; y = x - (khi+1);
				cost = abs((ahi-x)-(bhi-y));
			} while (cost > worst);
	}
}

struct cslb {
	int		size;	/* How much is alloced */
	int		len;	/* How much is used */
	struct csl	*csl;
};

static void csl_add(struct cslb *buf, int a, int b, int len)
{
	struct csl *csl;
	if (len && buf->len) {
		csl = buf->csl + buf->len - 1;
		if (csl->a + csl->len == a &&
		    csl->b + csl->len == b) {
			csl->len += len;
			return;
		}
	}
	if (buf->size <= buf->len) {
		if (buf->size < 64)
			buf->size = 64;
		else
			buf->size += buf->size;
		buf->csl = realloc(buf->csl, sizeof(buf->csl[0]) * buf->size);
	}
	csl = buf->csl + buf->len;
	csl->len = len;
	csl->a = a;
	csl->b = b;
	buf->len += 1;
}

static void lcsl(struct file *a, int alo, int ahi,
		 struct file *b, int blo, int bhi,
		 struct cslb *cslb,
		 struct v *v)
{
	/* lcsl == longest common sub-list.
	 * This calls itself recursively as it finds the midpoint
	 * of the best path.
	 * On first call, 'csl' is NULL and will need to be allocated and
	 * is returned.
	 * On subsequence calls when 'csl' is not NULL, we add all the
	 * snakes we find to csl, and return a pointer to the next
	 * location where future snakes can be stored.
	 */
	int alo1 = alo;
	int ahi1 = ahi;
	int blo1 = blo;
	int bhi1 = bhi;

	if (ahi <= alo || bhi <= blo)
		return;

	if (!find_common(a, b,
			 &alo1, &ahi1,
			 &blo1, &bhi1,
			 v))
		return;

	/* There are more snakes to find - keep looking. */

	/* With depth-first recursion, this adds all the snakes
	 * before 'alo1' to 'csl'
	 */
	lcsl(a, alo, alo1,
	     b, blo, blo1,
	     cslb, v);

	if (ahi1 > alo1)
		/* need to add this common seq, possibly attach
		 * to last
		 */
		csl_add(cslb, alo1, blo1, ahi1 - alo1);

	/* Now recurse to add all the snakes after ahi1 to csl */
	lcsl(a, ahi1, ahi,
	     b, bhi1, bhi,
	     cslb, v);
}

/* If two common sequences are separated by only an add or remove,
 * and the first sequence ends the same as the middle text,
 * extend the second and contract the first in the hope that the
 * first might become empty.  This ameliorates against the greediness
 * of the 'diff' algorithm.
 * i.e. if we have:
 *   [ foo X ] X [ bar ]
 *   [ foo X ] [ bar ]
 * Then change it to:
 *   [ foo ] X [ X bar ]
 *   [ foo ] [ X bar ]
 * We treat the final zero-length 'csl' as a common sequence which
 * can be extended so we must make sure to add a new zero-length csl
 * to the end.
 * If this doesn't make the first sequence disappear, and (one of the)
 * X(s) was a newline, then move back so the newline is at the end
 * of the first sequence.  This encourages common sequences
 * to be whole-line units where possible.
 */
static void fixup(struct file *a, struct file *b, struct csl *list)
{
	struct csl *list1, *orig;
	int lasteol = -1;
	int found_end = 0;

	if (!list)
		return;

	/* 'list' and 'list1' are adjacent pointers into the csl.
	 * If a match gets deleted, they might not be physically
	 * adjacent any more.  Once we get to the end of the list
	 * this will cease to matter - the list will be a bit
	 * shorter is all.
	 */
	orig = list;
	list1 = list+1;
	while (list->len) {
		if (list1->len == 0)
			found_end = 1;

		/* If a single token is either inserted or deleted
		 * immediately after a matching token...
		 */
		if ((list->a+list->len == list1->a &&
		     list->b+list->len != list1->b &&
		     /* text at b inserted */
		     match(&b->list[list->b+list->len-1],
			   &b->list[list1->b-1])
			    )
		    ||
		    (list->b+list->len == list1->b &&
		     list->a+list->len != list1->a &&
		     /* text at a deleted */
		     match(&a->list[list->a+list->len-1],
			   &a->list[list1->a-1])
			    )
			) {
			/* If the last common token is a simple end-of-line
			 * record where it is.  For a word-wise diff, this is
			 * any EOL.  For a line-wise diff this is a blank line.
			 * If we are looking at a deletion it must be deleting
			 * the eol, so record that deleted eol.
			 */
			if (ends_line(a->list[list->a+list->len-1])
			    && a->list[list->a+list->len-1].len == 1
			    && lasteol == -1
				) {
				lasteol = list1->a-1;
			}
			/* Expand the second match, shrink the first */
			list1->a--;
			list1->b--;
			list1->len++;
			list->len--;

			/* If the first match has become empty, make it
			 * disappear.. (and forget about the eol).
			 */
			if (list->len == 0) {
				lasteol = -1;
				if (found_end) {
					/* Deleting just before the last
					 * entry */
					*list = *list1;
					list1->a += list1->len;
					list1->b += list1->len;
					list1->len = 0;
				} else if (list > orig)
					/* Deleting in the  middle */
					list--;
				else {
					/* deleting the first entry */
					*list = *list1++;
				}
			}
		} else {
			/* Nothing interesting here, though if we
			 * shuffled back past an eol, shuffle
			 * forward to line up with that eol.
			 * This causes an eol to bind more strongly
			 * with the preceding line than the following.
			 */
			if (lasteol >= 0) {
				while (list1->a <= lasteol
				       && (list1->len > 1 ||
					   (found_end && list1->len > 0))) {
					list1->a++;
					list1->b++;
					list1->len--;
					list->len++;
				}
				lasteol = -1;
			}
			*++list = *list1;
			if (found_end) {
				list1->a += list1->len;
				list1->b += list1->len;
				list1->len = 0;
			} else
				list1++;
		}
		if (list->len && list1 == list)
			abort();
	}
}

static int elcmp(const void *v1, const void *v2)
{
	const struct elmnt *e1 = v1;
	const struct elmnt *e2 = v2;

	if (e1->hash != e2->hash) {
		if (e1->hash < e2->hash)
			return -1;
		return 1;
	}
	if (e1->start[0] == 0 && e2->start[0] == 0)
		return 0;
	if (e1->len != e2->len)
		return e1->len - e2->len;
	return strncmp(e1->start, e2->start, e1->len);
}

#define BPL (sizeof(unsigned long) * 8)
static struct file filter_unique(struct file f, struct file ref)
{
	/* Use a bloom-filter to record all hashes in 'ref' and
	 * then if there are consequtive entries in 'f' that are
	 * not in 'ref', reduce each such run to 1 entry
	 */
	struct file n;
	int fi, cnt;
	struct file sorted;

	sorted.list = xmalloc(sizeof(sorted.list[0]) * ref.elcnt);
	sorted.elcnt = ref.elcnt;
	memcpy(sorted.list, ref.list, sizeof(sorted.list[0]) * sorted.elcnt);
	qsort(sorted.list, sorted.elcnt, sizeof(sorted.list[0]),
	      elcmp);

	n.list = xmalloc(sizeof(n.list[0]) * f.elcnt);
	n.elcnt = 0;
	cnt = 0;
	for (fi = 0; fi < f.elcnt; fi++) {
		int lo = 0, hi = sorted.elcnt;
		while (lo + 1 < hi) {
			int mid = (lo + hi) / 2;
			if (elcmp(&f.list[fi], &sorted.list[mid]) < 0)
				hi = mid;
			else
				lo = mid;
		}
		if (match(&f.list[fi], &sorted.list[lo]))
			cnt = 0;
		else
			cnt += 1;
		if (cnt <= 1)
			n.list[n.elcnt++] = f.list[fi];
	}
	free(sorted.list);
	return n;
}

static void remap(struct csl *csl, int which, struct file from, struct file to)
{
	/* The a,b pointer in csl points to 'from' we need to remap to 'to'.
	 * 'to' has everything that 'from' has, plus more.
	 * Each list[].start is unique
	 */
	int ti = 0;
	while (csl->len) {
		int fi = which ? csl->b : csl->a;
		while (to.list[ti].start != from.list[fi].start) {
			ti += 1;
			if (ti > to.elcnt)
				abort();
		}
		if (which)
			csl->b = ti;
		else
			csl->a = ti;
		csl += 1;
	}
	if (which)
		csl->b = to.elcnt;
	else
		csl->a = to.elcnt;
}
/* Main entry point - find the common-sub-list of files 'a' and 'b'.
 * The final element in the list will have 'len' == 0 and will point
 * beyond the end of the files.
 */
struct csl *diff(struct file a, struct file b)
{
	struct v *v;
	struct cslb cslb = {};
	struct file af, bf;

	/* Remove runs of 2 or more elements in one file that don't
	 * exist in the other file.  This often makes the number of
	 * elements more manageable.
	 */
	af = filter_unique(a, b);
	bf = filter_unique(b, a);

	v = xmalloc(sizeof(struct v)*(af.elcnt+bf.elcnt+2));
	v += bf.elcnt+1;

	lcsl(&af, 0, af.elcnt,
	     &bf, 0, bf.elcnt,
	     &cslb, v);
	csl_add(&cslb, af.elcnt, bf.elcnt, 0);
	free(v-(bf.elcnt+1));
	remap(cslb.csl, 0, af, a);
	remap(cslb.csl, 1, bf, b);
	free(af.list);
	free(bf.list);
	fixup(&a, &b, cslb.csl);
	return cslb.csl;
}

/* Alternate entry point - find the common-sub-list in two
 * subranges of files.
 */
struct csl *diff_partial(struct file a, struct file b,
			 int alo, int ahi, int blo, int bhi)
{
	struct v *v;
	struct cslb cslb = {};
	v = xmalloc(sizeof(struct v)*(ahi-alo+bhi-blo+2));
	v += bhi-alo+1;

	lcsl(&a, alo, ahi,
	     &b, blo, bhi,
	     &cslb, v);
	csl_add(&cslb, ahi, bhi, 0);
	free(v-(bhi-alo+1));
	fixup(&a, &b, cslb.csl);
	return cslb.csl;
}

struct csl *csl_join(struct csl *c1, struct csl *c2)
{
	int cnt1, cnt2;
	int offset = 0;
	if (c1 == NULL)
		return c2;
	if (c2 == NULL)
		return c1;

	for (cnt1 = 0; c1[cnt1].len; cnt1++)
		;
	for (cnt2 = 0; c2[cnt2].len; cnt2++)
		;
	if (cnt1 && cnt2 &&
	    c1[cnt1-1].a + c1[cnt1-1].len == c2[0].a &&
	    c1[cnt1-1].b + c1[cnt1-1].len == c2[0].b) {
		/* Merge these two */
		c1[cnt1-1].len += c2[0].len;
		offset = 1;
		cnt2--;
	}
	c1 = realloc(c1, (cnt1+cnt2+1)*sizeof(*c1));
	while (cnt2 >= 0) {
		c1[cnt1+cnt2] = c2[cnt2 + offset];
		cnt2--;
	}
	free(c2);
	return c1;
}

/* When rediffing a patch, we *must* make sure the hunk headers
 * line up.  So don't do a full diff, but rather find the hunk
 * headers and diff the bits between them.
 */
struct csl *diff_patch(struct file a, struct file b)
{
	int ap, bp;
	struct csl *csl = NULL;
	if (a.elcnt == 0 || b.elcnt == 0 ||
	    a.list[0].start[0] != '\0' ||
	    b.list[0].start[0] != '\0')
		/* this is not a patch */
		return diff(a, b);

	ap = 0; bp = 0;
	while (ap < a.elcnt && bp < b.elcnt) {
		int alo = ap;
		int blo = bp;
		struct csl *cs;

		do
			ap++;
		while (ap < a.elcnt &&
		       a.list[ap].start[0] != '\0');
		do
			bp++;
		while (bp < b.elcnt &&
		       b.list[bp].start[0] != '\0');
		cs = diff_partial(a, b, alo, ap, blo, bp);
		csl = csl_join(csl, cs);
	}
	return csl;
}

#ifdef MAIN

main(int argc, char *argv[])
{
	struct file a, b;
	struct csl *csl;
	struct elmnt *lst = xmalloc(argc*sizeof(*lst));
	int arg;
	struct v *v;
	int ln;

	arg = 1;
	a.elcnt = 0;
	a.list = lst;
	while (argv[arg] && strcmp(argv[arg], "--")) {
		lst->hash = 0;
		lst->start = argv[arg];
		lst->len = strlen(argv[arg]);
		a.elcnt++;
		lst++;
		arg++;
	}
	if (!argv[arg]) {
		printf("AARGH\n");
		exit(1);
	}
	arg++;
	b.elcnt = 0;
	b.list = lst;
	while (argv[arg] && strcmp(argv[arg], "--")) {
		lst->hash = 0;
		lst->start = argv[arg];
		lst->len = strlen(argv[arg]);
		b.elcnt++;
		lst++;
		arg++;
	}

	csl = diff(a, b);
	fixup(&a, &b, csl);
	while (csl && csl->len) {
		int i;
		printf("%d,%d for %d:\n", csl->a, csl->b, csl->len);
		for (i = 0; i < csl->len; i++) {
			printf("  %.*s (%.*s)\n",
			       a.list[csl->a+i].len, a.list[csl->a+i].start,
			       b.list[csl->b+i].len, b.list[csl->b+i].start);
		}
		csl++;
	}

	exit(0);
}

#endif

