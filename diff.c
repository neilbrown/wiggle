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
 * calculate longest common sequence between two sequences
 *
 * Each sequence contains strings with
 *   hash start length
 * We produce a list of tripples: a b len
 * where A and B point to elements in the two sequences, and len is the number
 * of common elements there
 *
 * To help keep matches close together (important when matching a changed fragment
 * against a whole) we track the disagonal of the first and last match on any path.
 * When choosing the best of two paths, we choose the furthest reaching unless
 * the other has a match and it's absolute diagonal difference is significantly smaller.
 * 'Significant' if the reduction in difference exceeds the loss of progress by a
 * factor of 2.
 *
 */

#include	<malloc.h>
#include	"wiggle.h"
#include	<string.h>
#include	<stdlib.h>


struct v {
	int x;	/* x location of furthest reaching path of current cost */
	int md; /* diagonal location of midline crossing */
	int l;  /* number of continuous common sequences found so far */
};


static int find_common(struct file *a, struct file *b,
		       int *alop, int *ahip,
		       int *blop, int *bhip, 
		       int mid,
		       struct v *v)
{
	/* examine matrix from alo to ahi and blo to bhi
	 * finding longest subsequence.
	 * return new {a,b}{lo,hi} either side of midline.
	 * i.e. alo+blo <= mid <= ahi+bhi
	 *  and alo,blo to ahi,bhi is a common (possibly empty) subseq
	 *
	 * v is scratch space each is indexable from
	 * alo-bhi to ahi-blo inclusive
	 */
		
	int klo, khi;
	int k;
	int alo = *alop;
	int ahi = *ahip;
	int blo = *blop;
	int bhi = *bhip;
	int x,y;

	int best = (ahi-alo)+(bhi-blo);
	int dist;

	klo = khi = alo-blo;
	v[klo].x = alo;
	v[klo].l = 0;

	while(1) {

		for (k=klo ; k <= khi ; k+= 2) {
			int snake = 0;
			struct v vnew = v[k];
			x = v[k].x;
			y = x-k;
			if (y > bhi) abort();
			while (x < ahi && y < bhi &&
			       match(&a->list[x], &b->list[y])
				) {
				x++;
				y++;
				snake=1;
			}
			vnew.x = x;
			vnew.l += snake;
			dist = (ahi-x)+(bhi-y);
			if (dist < best) best = dist;
			if (x+y >= mid && 
			    v[k].x+v[k].x-k <= mid) {
				vnew.md = k;
			}
			v[k] = vnew;

			if (dist == 0) {
				/* OK! We have arrived.
				 * We crossed the midpoint at or after v[k].xm,v[k].ym
				 */
				if (x != ahi) abort();
				x = (v[k].md+mid)/2;
				y = x-v[k].md;
				*alop = x;
				*blop = y;

				while (x < ahi && y < bhi &&
				       match(&a->list[x], &b->list[y])
					) {
					x++;
					y++;
				}

				*ahip = x;
				*bhip = y;

				return k;
			}
		}

		for (k=klo+1; k <= khi-1 ; k+= 2) {
			if (v[k-1].x+1 >= v[k+1].x ) {
				v[k] = v[k-1];
				v[k].x++;
			} else {
				v[k] = v[k+1];
			}
		}

		x = v[klo].x; y = x -(klo-1);
		dist = abs((ahi-x)-(bhi-y));
		if (dist <= best) {
			v[klo-1] = v[klo];
			klo --;
		} else
			while (dist > best) {
				klo ++;
				x = v[klo].x; y = x -(klo-1);
				dist = abs((ahi-x)-(bhi-y));
			}

		x = v[khi].x+1; y = x - (khi+1);
		dist = abs((ahi-x)-(bhi-y));
		if (dist <= best) {
			v[khi+1] = v[khi];
			v[khi+1].x++;
			khi ++;
		} else 
			while (dist > best) {
				khi --;
				x = v[khi].x+1; y = x - (khi+1);
				dist = abs((ahi-x)-(bhi-y));
			}
	}
}

static struct csl *lcsl(struct file *a, int alo, int ahi,
		 struct file *b, int blo, int bhi,
		 struct csl *csl,
		 struct v *v)
{
	int len;
	int alo1 = alo;
	int ahi1 = ahi;
	int blo1 = blo;
	int bhi1 = bhi;
	struct csl *rv = NULL;
	int k;

	if (ahi <= alo || bhi <= blo) 
		return csl;


	k = find_common(a,b,
			&alo1, &ahi1,
			&blo1, &bhi1,
			(ahi+bhi+alo+blo)/2,
			v);
	if (k != ahi-bhi) abort();

	len = v[k].l;

	if (csl == NULL) {
		rv = csl = malloc((len+1)*sizeof(*csl));
		csl->len = 0;
	}
	if (len) {
		csl = lcsl(a,alo,alo1,	
			   b,blo,blo1,
			   csl, v);

		if (ahi1 > alo1) {
			/* need to add this common seq, possibly attach
			 * to last
			 */
			if (csl->len &&
			    csl->a+csl->len == alo1 &&
			    csl->b+csl->len == blo1) {
				csl->len += ahi1-alo1;
			} else {
				if (csl->len) csl++;
				csl->len = ahi1-alo1;
				csl->a = alo1;
				csl->b = blo1;
				csl[1].len = 0;
			}
		}
		csl = lcsl(a,ahi1,ahi,
			   b,bhi1,bhi,
			   csl,v);
	}
	if (rv) {
		if (csl->len)
			csl++;
		csl->a = ahi;
		csl->b = bhi;
#if 1
		if (rv+len != csl || csl->len != 0)
			abort(); /* number of runs was wrong */
#endif
		return rv;
	} else
		return csl;
}

/* if two common sequences are separated by only an add or remove,
 * and the first common ends the same as the middle text,
 * extend the second and contract the first in the hope that the 
 * first might become empty.  This ameliorates against the greedyness
 * of the 'diff' algorithm.
 * Once this is done, repeat the process but extend the first
 * in favour of the second.  The acknowledges that semantic units
 * more often end with common text ("return 0;\n}\n", "\n") than
 * start with it.
 */
static void fixup(struct file *a, struct file *b, struct csl *list)
{
	struct csl *list1, *orig;
	int lasteol = -1;
	if (!list) return;
	orig = list;
	list1 = list+1;
	while (list->len && list1->len) {
		if ((list->a+list->len == list1->a &&
		     /* text at b inserted */
		     match(&b->list[list->b+list->len-1],
			   &b->list[list1->b-1])
			)
		    ||
		    (list->b+list->len == list1->b &&
		     /* text at a deleted */
		     match(&a->list[list->a+list->len-1],
			   &a->list[list1->a-1])
			    )
			) {
/*			printword(a->list[list1->a-1]);
			printf("fixup %d,%d %d : %d,%d %d\n",
			       list->a,list->b,list->len,
			       list1->a,list1->b,list1->len);
*/			if (ends_line(a->list[list->a+list->len-1])
			    && a->list[list->a+list->len-1].len==1
			    && lasteol == -1
				) {
/*				printf("E\n");*/
				lasteol = list1->a-1;
			}
			list1->a--;
			list1->b--;
			list1->len++;
			list->len--;
			if (list->len == 0) {
				lasteol = -1;
				if (list > orig)
					list--;
				else {
					*list = *list1++;
/*					printf("C\n");*/
				}
			}
		} else {
			if (lasteol >= 0) {
/*				printf("seek %d\n", lasteol);*/
				while (list1->a <= lasteol && list1->len>1) {
					list1->a++;
					list1->b++;
					list1->len--;
					list->len++;
				}
				lasteol=-1;
			}
			*++list = *list1++;
		}
	}
	list[1] = list1[0];
}

struct csl *diff(struct file a, struct file b)
{
	struct v *v;
	struct csl *csl;
	v = malloc(sizeof(struct v)*(a.elcnt+b.elcnt+2));
	v += b.elcnt+1;

	csl = lcsl(&a, 0, a.elcnt,
		   &b, 0, b.elcnt,
		   NULL, v);
	free(v-(b.elcnt+1));
	fixup(&a, &b, csl);
	if (!csl) {
		csl = malloc(sizeof(*csl));
		csl->len = 0;
		csl->a = a.elcnt;
		csl->b = b.elcnt;
	}
	return csl;
}

struct csl *diff_partial(struct file a, struct file b, 
			 int alo, int ahi, int blo, int bhi)
{
	struct v *v;
	struct csl *csl;
	v = malloc(sizeof(struct v)*(ahi-alo+bhi-blo+2));
	v += bhi-alo+1;

	csl = lcsl(&a, alo, ahi,
		   &b, blo, bhi,
		   NULL, v);
	free(v-(bhi-alo+1));
	fixup(&a, &b, csl);
	return csl;
}


#ifdef MAIN

main(int argc, char *argv[])
{
	struct file a, b;
	struct csl *csl;
	struct elmnt *lst = malloc(argc*sizeof(*lst));
	int arg;
	int alo, ahi, blo, bhi;
	struct v *v;
	int ln;

	arg = 1;
	a.elcnt = 0;
	a.list = lst;
	while (argv[arg] && strcmp(argv[arg],"--")) {
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
	while (argv[arg] && strcmp(argv[arg],"--")) {
		lst->hash = 0;
		lst->start = argv[arg];
		lst->len = strlen(argv[arg]);
		b.elcnt++;
		lst++;
		arg++;
	}

	v = malloc(sizeof(struct v)*(a.elcnt+b.elcnt+2));
	v += b.elcnt+1;
	alo = blo = 0;
	ahi = a.elcnt;
	bhi = b.elcnt;
#if 0
	ln = find_common(&a, &b,
			 &alo, &ahi, &blo, &bhi,
			 (ahi+bhi)/2,
			 v);

	printf("ln=%d (%d,%d) -> (%d,%d)\n", ln,
	       alo,blo,ahi,bhi);
#else
	csl = lcsl(&a, 0, a.elcnt,
		   &b, 0, b.elcnt,
		   NULL, v);
	fixup(&a, &b, csl);
	while (csl && csl->len) {
		int i;
		printf("%d,%d for %d:\n", csl->a,csl->b,csl->len);
		for (i=0; i<csl->len; i++) {
			printf("  %.*s (%.*s)\n",
			       a.list[csl->a+i].len, a.list[csl->a+i].start,
			       b.list[csl->b+i].len, b.list[csl->b+i].start);
		}
		csl++;
	}
#endif

	exit(0);
}

#endif

