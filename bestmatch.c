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
 * Find the best match for a patch against
 * a file.
 * The quality of a match is the length of the match minus the
 * differential between the endpoints.
 * We progress through the matrix recording the best
 * match as we find it.
 *
 * We perform a full diagonal bredth first traversal assessing
 * the quality of matches at each point.
 * At each point there are two or three previous points,
 * up, back or diagonal if there is a match.
 * We assess the value of the match at each point and choose the
 * best.  No match at all is given a score of -3.
 *
 * For any point, the best possible score using that point
 * is a complete diagonal to the nearest edge.  We ignore points
 * which cannot contibute to a better overall score.
 *
 */

/* This structure keeps track of the current match at each point.
 * It holds the start of the match as x,k where k is the
 * diagonal, so y = x-k.
 * Also the length of the match so far.
 * If l == 0, there is no match.
 */

#include	<malloc.h>
#include	<ctype.h>
#include	<stdlib.h>
#include	"wiggle.h"


struct v {
	int x,y;  /* location of start of match */
	int val;  /* value of match from x,y to here */
	int k;    /* diagonal of last match */
	int inmatch; /* 1 if last point was a match */
	int c; /* chunk number */
};

/*
 * Here must must determine the 'value' of a partial match.
 * The input parameters are:
 *   length - the total number of symbols matches
 *   errs  - the total number of insertions or deletions
 *   dif   - the absolute difference between number of insertions and deletions.
 *
 * In general we want length to be high, errs to be low, and dif to be low.
 * Particular questions that must be answered include:
 *  - When does adding an extra symbol after a small gap improve the match
 *  - When does a match become so bad that we would rather start again.
 *
 * We would like symetry in our answers so that a good sequence with an out-rider on
 * one end is evaluated the same as a good sequence with an out-rider on the other end.
 * However to do this we cannot really use value of the good sequence to weigh in the
 * outriders favour as in the case of a leading outrider, we do not yet know the value of 
 * of the good sequence.
 * First, we need an arbitrary number, X, to say "Given a single symbol, after X errors, we
 * forget that symbol".  5 seems a good number.
 * Next we need to understand how replacements compare to insertions or deletions.
 * Probably a replacement is the same cost as an insertion or deletion.
 * Finally, a few large stretches are better then lots of little ones, so the number
 * of disjoint stretches should be kept low.
 * So:
 *   Each match after the first adds 5 to value.
 *   The first match in a string adds 6.
 *   Each non-match subtracts one unless it is the other half of a replacement.
 *   A value of 0 causes us to forget where we are and start again.
 *
 * We need to not only assess the value at a particular location, but also
 * assess the maximum value we could get if all remaining symbols matched, to
 * help exclude parts of the matrix.
 * The value of that possibility is 6 times the number of remaining symbols, -1 if we
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
	} else {
		v->inmatch = 0;
		if (dir * (v->k - k) > 0) {
			/* other half of replacement */
		} else {
			v->val -= 1;
		}
	}
}
static inline int best_val(struct v *v, int max)
{
	if (v->val <= 0)
		return 4+max*3-1;
	else
		return max*3-1+v->inmatch+v->val;
}

#ifdef OLDSTUFF
#if 0
#define value(v,kk,xx) (v.l ? (v.l - abs(kk-v.k)): -3)
#else
# if 0
# define value(v,kk,xx) (v.l ? (v.l - (xx-v.x)/2): -3)
# else
# define value(v,kk,xx) (v.l ? (v.l - (xx-v.x)*2/v.l): -3)
# endif
#endif
#endif
struct best {
	int xlo,ylo,xhi,yhi,val;
};

static inline int min(int a, int b) {
	return a < b ? a : b;
}

void find_best(struct file *a, struct file *b,
	      int alo, int ahi,
	      int blo, int bhi, struct best *best)
{
	int klo, khi, k;
	int f;

	struct v *valloc = malloc(sizeof(struct v)*((ahi-alo)+(bhi-blo)+5));
	struct v *v = valloc + (bhi-alo+2);

	k = klo = khi = alo-blo;
	f = alo+blo; /* front that moves forward */
	v[k].val = 0;
	v[k].c = -1;

	while (f < ahi+bhi) {
		int x,y;

		f++;

#if 0
		if (f == ahi+bhi)
			printf("f %d klo %d khi %d\n", f,klo,khi);
#endif
		for (k=klo+1; k <= khi-1 ; k+=2) {
			struct v vnew, vnew2;
			x = (k+f)/2;
			y = x-k;
			/* first consider the diagonal */
			if (match(&a->list[x-1], &b->list[y-1])) {
				vnew = v[k];
				update_value(&vnew, 0, k, x);
#if 0
				printf("new %d,%d %d,%d (%d) ...",
				       vnew.x, vy(vnew), x, y, value(vnew,k,x));
#endif
				if (vnew.c < 0) abort();
				if (vnew.val > best[vnew.c].val) {
#if 0
					printf("New best for %d at %d,%d %d,%d, val %d\n",
					       vnew.c, vnew.x, vnew.y,x,y,vnew.val);
#endif
					best[vnew.c].xlo = vnew.x;
					best[vnew.c].ylo = vnew.y;
					best[vnew.c].xhi = x;
					best[vnew.c].yhi = y;
					best[vnew.c].val = vnew.val;
				}
				v[k] = vnew;
			} else {
				vnew = v[k+1];
				update_value(&vnew, -1, k,x);
				/* might cross a chunk boundary */
				if (b->list[y-1].len && b->list[y-1].start[0]==0) {
					vnew.c = atoi(b->list[y-1].start+1);
					vnew.val = 0;
				}
				vnew2 = v[k-1];
				update_value(&vnew2, 1, k, x);

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
		update_value(&v[klo],-1,klo,x);
		if (y<=bhi && b->list[y-1].len && b->list[y-1].start[0]==0) {
			v[klo].c = atoi(b->list[y-1].start+1);
#if 0
			printf("entered %d at %d,%d\n", v[klo].c, x, y);
#endif
			v[klo].val = 0;
		}
		while (klo+2 < (ahi-bhi) && 
		       (y > bhi || 
			(best_val(&v[klo], min(ahi-x,bhi-y)) < best[v[klo].c].val &&
			 best_val(&v[klo+1], min(ahi-x,bhi-y+1)) < best[v[klo+1].c].val
				)
			       )) {
			klo+=2;
			x = (klo+f)/2; y = x-klo;
		}

		khi++;
		v[khi] = v[khi-1];
		x = (khi+f)/2; y = x - khi;
		update_value(&v[khi],-1,khi,x);
		while(khi-2 > (ahi-bhi) &&
		      (x > ahi ||
		       (best_val(&v[khi], min(ahi-x,bhi-y)) < best[v[khi].c].val &&
			best_val(&v[khi-1], min(ahi-x+1,bhi-y)) < best[v[khi].c].val
			       )
			      )) {
			khi -= 2;
			x = (khi+f)/2; y = x - khi;
		}

	}
	free(valloc);
}

struct csl *csl_join(struct csl *c1, struct csl *c2)
{
	struct csl *c,*cd,  *rv;
	int cnt;
	if (c1 == NULL)
		return c2;
	if (c2 == NULL)
		return c1;

	cnt = 1; /* the sentinal */
	for (c=c1; c->len; c++) cnt++;
	for (c=c2; c->len; c++) cnt++;
	cd = rv = malloc(sizeof(*rv)*cnt);
	for (c=c1; c->len; c++)
		*cd++ = *c;
	for (c=c2; c->len; c++)
		*cd++ = *c;
	cd->len = 0;
	free(c1);
	free(c2);
	return rv;
}

#if 0
static void printword(struct elmnt e)
{
	if (e.start[0])
		printf("%.*s", e.len, e.start);
	else {
		int a,b,c;
		sscanf(e.start+1, "%d %d %d", &a, &b, &c);
		printf("*** %d,%d **** %d\n", b,c,a);
	}
}
#endif

/*
 * reduce a file by discarding less interesting words
 * Words that end with a newline are interesting (so all words
 * in line-mode are interesting) and words that start with
 * and alphanumeric are interesting.  This excludes spaces and 
 * special characters in word mode
 * Doing a best-fit comparision on only interesting words is
 * much fast than on all words, and it nearly as good
 */

static inline int is_skipped(struct elmnt e)
{
	return !( ends_line(e) || 
		  isalnum(e.start[0]) ||
		  e.start[0] == '_');
}
struct file reduce(struct file orig)
{
	int cnt=0;
	int i;
	struct file rv;

	for (i=0; i<orig.elcnt; i++)
		if (!is_skipped(orig.list[i]))
			cnt++;

	if (cnt == orig.elcnt) return orig;

	rv.elcnt = cnt;
	rv.list = malloc(cnt*sizeof(struct elmnt));
	cnt = 0;
	for (i=0; i<orig.elcnt; i++)
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
void remap(struct best *best, int cnt,
	   struct file a1, struct file b1,
	   struct file a2, struct file b2)
{
	int b;
	int pa,pb;
	pa=pb=0;

	for (b=1; b<cnt; b++) {
#if 0
		printf("best %d,%d  %d,%d\n",
		       best[b].xlo,best[b].ylo,
		       best[b].xhi,best[b].yhi);
#endif
		while (pa<a2.elcnt &&
		       a2.list[pa].start != a1.list[best[b].xlo].start)
			pa++;
		if (pa == a2.elcnt) abort();
		while (pb<b2.elcnt &&
		       b2.list[pb].start != b1.list[best[b].ylo].start)
			pb++;
		if (pb == b2.elcnt) abort();

		/* pa,pb is the start of this best bit.  Step
		 * backward over ignored words 
		 */
		while (pa>0 && is_skipped(a2.list[pa-1]))
			pa--;
		while (pb>0 && is_skipped(b2.list[pb-1]))
			pb--;

#if 0
		printf("-> %d,%d\n", pa,pb);
#endif
		best[b].xlo = pa;
		best[b].ylo = pb;

		while (pa<a2.elcnt &&
		       a2.list[pa-1].start != a1.list[best[b].xhi-1].start)
			pa++;
		if (pa == a2.elcnt && best[b].xhi != a1.elcnt) abort();
		while (pb<b2.elcnt &&
		       b2.list[pb-1].start != b1.list[best[b].yhi-1].start)
			pb++;
		if (pb == b2.elcnt && best[b].yhi != b1.elcnt) abort();

		/* now step pa,pb forward over ignored words */
		while (pa<a2.elcnt && is_skipped(a2.list[pa]))
			pa++;
		while (pb<b2.elcnt && is_skipped(b2.list[pb]))
			pb++;
#if 0
		printf("-> %d,%d\n", pa,pb);
#endif
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
	int bad=0;
	int bestval, bestpos=0;
	for (i=bestlo; i<besthi; i++) best[i].val = 0;
	find_best(a,b,alo,ahi,blo,bhi,best);
	for (i=bestlo+1; i<besthi; i++)
		if (best[i-1].val > 0 &&
		    best[i].val > 0 &&
		    best[i-1].xhi >= best[i].xlo)
			bad = 1;

	if (!bad)
		return;
	bestval = 0;
	for (i=bestlo; i<besthi; i++)
		if (best[i].val > bestval) {
			bestval = best[i].val;
			bestpos = i;
		}
	if (bestpos > bestlo) {
		/* move top down below chunk marker */
		int y = best[bestpos].ylo;
		while (b->list[y].start[0]) y--;
		find_best_inorder(a,b,
				  alo, best[bestpos].xlo,
				  blo, y,
				  best, bestlo, bestpos);
	}
	if (bestpos < besthi-1) {
		/* move bottom up to chunk marker */
		int y = best[bestpos].yhi;
		while (b->list[y].start[0]) y++;
		find_best_inorder(a,b,
				  best[bestpos].xhi, ahi,
				  y, bhi,
				  best, bestpos+1, besthi);
	}
}

struct csl *pdiff(struct file a, struct file b, int chunks)
{
	int alo,ahi,blo,bhi;
	struct csl *csl1, *csl2;
	struct best *best = malloc(sizeof(struct best)*(chunks+1));
	int i;
	struct file asmall, bsmall;

	asmall = reduce(a);
	bsmall = reduce(b);

	alo = blo = 0;
	ahi = asmall.elcnt;
	bhi = bsmall.elcnt;
/*	printf("start: %d,%d  %d,%d\n", alo,blo,ahi,bhi); */

	for (i=0; i<chunks+1; i++)
		best[i].val = 0;
	find_best_inorder(&asmall,&bsmall, 
		  0, asmall.elcnt, 0, bsmall.elcnt,
		  best, 1, chunks+1);
#if 0
/*	for(i=0; i<b.elcnt;i++) { printf("%d: ", i); printword(b.list[i]); }*/
	for (i=1; i<=chunks; i++) {
		printf("end: %d,%d  %d,%d\n", best[i].xlo,best[i].ylo,best[i].xhi,best[i].yhi);
		printf("<"); printword(bsmall.list[best[i].ylo]); printf("><");
		printword(bsmall.list[best[i].yhi-1]);printf(">\n");
	}
#endif
	remap(best,chunks+1,asmall,bsmall,a,b);
#if 0
/*	for(i=0; i<b.elcnt;i++) { printf("%d: ", i); printword(b.list[i]); }*/
	for (i=1; i<=chunks; i++)
		printf("end: %d,%d  %d,%d\n", best[i].xlo,best[i].ylo,best[i].xhi,best[i].yhi);
	printf("small: a %d b %d --- normal: a %d b %d\n", asmall.elcnt, bsmall.elcnt, a.elcnt, b.elcnt);
#endif

	csl1 = NULL;
	for (i=1; i<=chunks; i++)
		if (best[i].val>0) {
#if 0
			int j;
			printf("Before:\n");
			for (j=best[i].xlo; j<best[i].xhi; j++)
				printword(a.list[j]);
			printf("After:\n");
			for (j=best[i].ylo; j<best[i].yhi; j++)
				printword(b.list[j]);
#endif
			csl2 = diff_partial(a,b,
					    best[i].xlo,best[i].xhi,
					    best[i].ylo,best[i].yhi);
			csl1 = csl_join(csl1,csl2);
		}
	if (csl1) {
		for (csl2=csl1; csl2->len; csl2++);
		csl2->a = a.elcnt;
		csl2->b = b.elcnt;
	} else {
		csl1 = malloc(sizeof(*csl1));
		csl1->len = 0;
		csl1->a = a.elcnt;
		csl1->b = b.elcnt;
	}
	free(best);
	return csl1;
}
