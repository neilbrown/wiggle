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
 * This file contains routines use to create a merge.
 * The core process is to take two coincidence lists, A-B and B-C,
 * which identify coincidences and, but ommission, changes, and
 * to apply to replace every part of A that matches B with the
 * part of C that aligns with that part of B.  In the case where
 * a B-C difference does not align completely with an A-B coincidence,
 * we have a conflict.
 *
 * Throught the processing of merges we need a concept of a position in the
 * overall merge.  This is represented by an index into one of the files, and
 * and indicator as to which file.
 * If the point is in:
 *    A - then it is an unmatched part of A, before a coincidence.
 *    B - then it is in a section where A matches B and B matches C.
 *    C - then it is in an unmatched part of C, but the corresponding part
 *        of B completely coincides with A.
 * With each position we keep indexes into the coincidence lists for
 * the containing or next coincidence in each.
 *
 *
 * The first stage of merge processing is to identify conflicts.
 * A conflict is identified by a start point and an end point.
 * The first approximation for the start point is the end
 * of the last A-B coincidence that starts before the B start
 * of the B-C difference that causes the conflict.

 *
 * We have a concept of a 'point'
 * The start and end of the file are each points.
 * Also the start and end of any conflict is a point.
 * Outside of a conflict, we can move points forwards or backwards 
 * through the merger.  Inside a conflict movement is not well defined.
 * Any point is 'forward looking' or 'backward looking'.
 * A forward looking point can be moved forward but not backward.
 * A backward looking point can be moved backward, not forward.
 *
 * If a forward looking point is a tri-point, in a double-coincidence,
 * then c1/c2 will be set to the furthest forward double coincidence that is before 
 * or contains the point, thus it well-defines the start of a double coincidence
 * or that end of a conflict.
 * inversely, a BL point well-defines the end of a DC or start of a conflict.
 *
 * The start of a conflict is a backward looking point.
 * The end of a conflict is a forward looking point.
 *
 * In normal (Word/line) mode, we move the boundaries of a conflict out 
 * until they are at end-of-line.
 * When moving forward, this is until we cross over a newline word.
 * When moving backward, this is until one step before crossing over
 * a newline word, so we need to remember our last position.
 *
 * Away from a conflict, every point can be clearly defined as a
 * location either in A or in C.  The 'point' is immediately before
 * the word at that location.
 * At the end of a conflict, this is still well defined as the 'next word' 
 * is outside a conflict.
 * At the start of a conflict this may not be well defined as there may not 
 * be a clear 'next' word.  We choose the point the would be reached by
 * the step-forward algorithm so that it is easy to test if at start-of-conflict.
 *
 * A conflict is always bounded by a double coincidence.  i.e. the word before a conflict
 * is the same in all 3 texts, and the word after a conflict is the same in all
 * 3 texts.  To allow for conflicts at start and end of file, we consider the
 * start and end of the three texts to each be double co-incidences.
 *
 * Each double co-incidence has a start and an end.  When we find a conflict, it 
 * is taken to extend from the end of the previous double coincidence to the 
 * start of the next double co-incidence.
 * Between conflicts we can mergers which can be printed simply be advancing the start
 * point and printing each word as we go.
 *
 * The double co-incidence at the start begins forward-looking A=0 or C=0,
 * depending on which word is first, and ends at backward-looking A=0.
 * The double co-incidence at the end begins at forward-looking
 * C=max and ends at backward looking A=max or C=max depending on which
 * would be the last word.
 *
 * Each point is defined by a flag "in_a" which is true if the point is in A,
 * and index 'pos' which gives the position in A or C depending on "in_a", and
 * an index into each co-incidence list, c1 and c2.
 *
 * For forward looking points:
 *   if in_a:
 *       c1 is the first co-incidence that ends after pos. - or is tail co-incidence.
 *       c2 is the first co-incidence that ends at or after c1.b
 *   if in_c:
 *       c2 is the first co-incidence that ends after pos - or is tail co-incidence.
 *       c1 is the first co-incidence that ends at or after c2.a
 *
 * For a backward looking point:
 *   if in_a:
 *       c1 is the last co-incidence that starts before pos, or -1
 *       c2 is the last co-incidence that starts at or before c1.b
 *   if in_c:
 *       c2 is the last co-incidence that starts before pos, or -1
 *       c1 is the last co-incidence that .. lines up properly.
 *
 * To advance a point we increment pos, then
 *   if in_a and at start of c1
 *      slide up to c and if at end of c2, advance c2, then c1 and repeat
 *   if in_c and within c2 and corresponding a at end of c1, and c1->len != 0
 *      slide down to a, increment c1 and advance c2, then repeat.
 *
 * To retreat a backward facing point
 *   if in_a and at end of c1 and c1!=-1, 
 *      slide up to c and if at start of c2, retreat c2, thenc 1, and repeat
 *   if in_c and within c2 and corresponding a at start of c1
 *      slide down to a, decrement c1 and retreat c2, then repeat.
 *   Then decrement pos.
 *
 * We never actually compare points for ordering.  We should 'know' the likely order
 * and only compare equal or not.  This can be tested independant of direction,
 * and done by simply comparing in_a and pos.
 */ 


/* Each point involves a location in each of A, B, and C.
 * There is a conflict for each change in B-C where the B section
 * is not wholey contained in an A-B co-incidence.
 * The start point of a conflict is determined as:
 *   C is the end of the C side of the  previous B-C coincidence (or 
 *       start of file
 *   B is the end of the B side of the matching A-B coincidence if
 *       the point is in an A-B coincidence, or the end of the previous
 *       A-B coincidence of not.
 *     As B moves backwards searching for an A-B coincidence, if it enters
 *      a B-C coincidence, C is moved backwards too.
 *   A is the matching point to B in the A-B coincidence that B is in.
 *
 * The end point of a conflict is determined in a similar way, 
 * except that B is in a coincidence that is at, or *follows* the
 * end of the next B-C coincidence.
 *
 * Once these coincidences have been enumerated, the endpoints are
 * optionally moved to be at start-of-line.  The start point is moved
 * backwards and the endpoint forwards.  The endofline must be in an
 * A-B coincidence and may be in C if there is also a B-C coincidence.
 *
 * The next step is to merge adjacent conflicts where the B point
 * from one overlaps the next.
 *
 */
#include	<unistd.h>
#include	<stdlib.h>
#include	"wiggle.h"

/* A point is somewhere either in_a or not in_a (in which case, in C).
 * if in_a, c1 points to the next a-b coincidence strictly after pos
 *          c2 points to the b-c coincidence that contains (possibly as end point) or follows c1.b
 * if !in_a, c2 points to the b-c coincidence that contains (possibly as endpoint) or follows pos
 *           c1 points to the a-b coincidence that contains c2.b
 *
 * A point is not well defined inside a conflict, Though it is at the
 * 'start' and 'end' of a conflict.
 *
 * At the start of the file c1 and c2 will be the firsts match in A-B and B-C
 * If [c1]->a is 0, then !in_a and pos is [c2]->b+x where x is
 * chosen such that [c1]->b == [c2]->a+x and x < [c2]->len.  If such choice 
 * is not possible, there is a conflict at the start of the file and so we choose
 * a point as if [c1]->a were not 0.
 *
 * If [c1]->a is not 0, then in_a and pos == 0.
 *
 * To find the start of file, we set in_a and pos==-1, and advance one step.
 *
 * At the end of the file, c1 will be the EOF match in A-B, c2 will be the
 * EOF match in B-C, !in_a and pos == [c2]->b
 */
struct point { int pos, in_a; int c1,c2; };


static int tripoint(struct point *here,
		    struct csl *c1, struct csl *c2,
		    int *a, int *b, int *c)
{
	/* find a, b, and c for 'here'.
	 * If any are not well defined, return 0.
	 */
	c1 += here->c1;
	c2 += here->c2;

	if (here->in_a) {
		*a = here->pos;

		if (here->c1 < 0) {
			if (*a) return 0;
			*b = 0;
		} else if (c1->a <= *a && c1->a+c1->len >= *a)
			*b = c1->b + (*a - c1->a);
		else
			return 0;

		if (here->c2 < 0) {
			if (*b) return 0;
			*c = 0;
		} else if (c2->a <= *b && c2->a + c2->len >= *b)
			*c = c2->b + *b - c2->a;
		else 
			return 0;
	} else {
		*c = here->pos;

		if (here->c2 < 0) {
			if (*c) return 0;
			*b = 0;
		} else if (c2->b <= *c && c2->b +c2->len >= *c)
			*b = c2->a + *c - c2->b;
		else	
			return 0;


		if (here->c1 < 0) {
			if (*b) return 0;
			*a = 0;
		} else if (c1->b <= *b && c1->b + c1->len >= *b)
			*a = c1->a + *b - c1->b;
		else
			return 0;
	}
	return 1;
}

static int retreat(struct csl *c1, struct csl *c2, struct point *p)
{
	int a,b,c;
	int slid = 0;

 retry:
	if (p->in_a) {
		/* retreat c1 to first coincidence containing or after pos */
		a = p->pos;
		while ((p->c1 == 0 && a == 0) ||
		       (p->c1 > 0 && c1[p->c1-1].a + c1[p->c1-1].len >= a)) {
			if (!slid) 
				if ( a >= c1[p->c1].a)
					break;
			p->c1--;
		}

		/* if we aren't in a co-incidence, just return */
		if (p->c1 >=0 &&
		    c1[p->c1].a > a)
			return 1;

		/* retreat c2 to first coincidence containing or after pos->b */
		if (p->c1 == -1)
			b = 0;
		else
			b = c1[p->c1].b + a - c1[p->c1].a;
		while ((p->c2 == 0 && b == 0) ||
		       (p->c1 > 0 && c2[p->c2-1].a + c2[p->c2-1].len >= b)) {
			if (!slid)
				if (b >= c2[p->c2].a)
					break;
			p->c2--;
		}

		/* check if this is a conflict */
		if ((p->c2>=0 && c2[p->c2].a > b))
			return 2;

		if (p->c2 == -1)
			c = 0;
		else
			c = c2[p->c2].b + b - c2[p->c2].a;

		/* ok, this is the furthest backward double coincidence
		 * if we are not at the start of the A-B coincidence,
		 * slip up to C
		 */
		if (p->c1 >= 0 && a > c1[p->c1].a) {
			p->in_a = 0;
			p->pos = c;
			slid = 1;
			goto retry;
		}
	} else {
		/* retreat c2 to first coincidence containing or after pos */
		c = p->pos;
		while ((p->c2 == 0 && c == 0) ||
		       (p->c2 > 0 && c2[p->c2-1].b + c2[p->c2-1].len >= c)) {
			if (!slid)
				if (c >= c2[p->c2].b)
					break;
			p->c2--;
		}

		/* if we aren't in a coincidence, return */
		if (p->c2 >= 0 &&
		    c2[p->c2].b > c)
			return 1;

		/* retreat c1 to first coincidence containing or afer pos->b */
		if (p->c2 == -1)
			b = 0;
		else
			b = c2[p->c2].a + c - c2[p->c2].b;
		while ((p->c1==0 && b == 0) ||
		       (p->c1 > 0 && c1[p->c1-1].b + c1[p->c1-1].len >= b)) {
			if (!slid)
				if (b >= c1[p->c1].b)
					break;
			p->c1--;
		}

		/* check if this is a conflict */
		if ((p->c1>=0 && c1[p->c1].b > b))
			return 2;

		if (p->c1 == -1)
			a = 0;
		else
			a = c1[p->c1].a + b - c1[p->c1].b;

		/* ok, this is the furthest backward double coincidence
		 * if we are at the start of the A-B coincidence, slide down to A
		 */
		if (p->c1 == -1 ||
		    a == c1[p->c1].a) {
			p->in_a = 1;
			p->pos = a;
			slid = 1;
			goto retry;
		}
	}
	if (p->pos == 0)
		return 0; /* StartOfFile */

	if (!slid) {
		slid = 1;
		goto retry;
	}

	return 1;
}

static int advance(struct csl *c1, struct csl *c2, struct point *p)
{
	int a,b,c;
	int slid = 0;
	/* make next char at point is the 'right' one, either in a or c.
	 * This might involve move p->c1 and p->c2 forward
	 * and changing pos/in_a to an 'equivalent' point
	 */
/*
	if (!p->in_a && c2[p->c2].b == p->pos && c2[p->c2].len == 0)
		return 0; / * at end of file * /
*/
 retry:
	if (p->in_a) {
		/* advance c1 to last coincidence containing or before pos */
		a = p->pos;
		while ((p->c1 == -1 || c1[p->c1].len) &&
		       c1[p->c1+1].a <= a) {
			if (!slid)
				if ((p->c1== -1 && a ==0) ||
				    (p->c1>=0 && a <= c1[p->c1].a+c1[p->c1].len))
					break;
			p->c1++;
		}

		/* if we aren't in a co-incidence, just return */
		if (p->c1 == -1 || c1[p->c1].a+c1[p->c1].len < a)
			return 1;

		/* advance c2 to last coincidence containing or before pos->b */
		b = c1[p->c1].b + a- c1[p->c1].a;
		while ((p->c2 == -1 || c2[p->c2].len) &&
		       c2[p->c2+1].a <= b) {
			if (!slid)
				if ((p->c2 == -1 && b == 0) ||
				    (p->c2 >= 0 && b <= c2[p->c2].a+c2[p->c2].len))
					break;
			p->c2++;
		}

		/* check if this is a conflict */
		if ((p->c2 == -1 && b >0) ||
		    (p->c2>=0 && c2[p->c2].a + c2[p->c2].len < b))
			return 2;

		if (p->c2 == -1)
			c = 0;
		else
			c = c2[p->c2].b + b - c2[p->c2].a;

		/* Ok, this is the furthest forward double coincidence
		 * If we are at eof, or the next char is in the coincidence
		 * slip up to c
		 */
		if (c1[p->c1].len == 0 ||
		    a < c1[p->c1].a + c1[p->c1].len) {
			p->in_a = 0;
			p->pos = c;
			slid = 1;
			goto retry;
		}
	} else {
		/* advance c2 to last coincidence containing or before pos */
		c = p->pos;
		while ((p->c2 == -1 || c2[p->c2].len) &&
		       c2[p->c2+1].b <= c) {
			if (!slid) 
				if ((p->c2 == -1 && c == 0) ||
				    (p->c2 >= 0 && c <= c2[p->c2].b+c2[p->c2].len))
					break;
			p->c2++;
		}

		/* if we aren't in a co-incidence then just return */
		if (p->c2 == -1 || c2[p->c2].b+c2[p->c2].len < c)
			return 1;

		/* advance c1 to last coincidence containing or before pos->b */
		b = c2[p->c2].a + c - c2[p->c2].b;
		while ((p->c1 == -1 || c1[p->c1].len) &&
		       c1[p->c1+1].b <= b) {
			if (!slid)
				if ((p->c1 == -1 && b ==0) ||
				    (p->c1 >= 0 && b <= c1[p->c1].b+c1[p->c1].len))
					break;
			p->c1++;
		}

		/* check if this is a conflict */
		if (p->c1 == -1 || c1[p->c1].b + c1[p->c1].len < b)
			return 2;

		a = c1[p->c1].a + b - c1[p->c1].b;

		/* ok, this is the furthest forward double coincidence 
		 * If it is the end of an A-B coincidence but not EOF,
		 * slide down to A
		 */
		if (a == c1[p->c1].a+ c1[p->c1].len &&
		    c1[p->c1].len) {
			p->in_a = 1;
			p->pos = a;
			slid = 1;
			goto retry;
		}
	}
	if (!p->in_a && c2[p->c2].b == p->pos && c2[p->c2].len == 0)
		return 0; /* at end of file */
	if (!slid) {
		slid = 1;
		goto retry;
	}
	return 1;
}

static int point_crossed(struct point first, struct point second, 
		      struct csl *cs1, struct csl *cs2)
{
	int a1,b1,c1;
	int a2,b2,c2;

	if (tripoint(&first, cs1,cs2, &a1,&b1,&c1) &&
	    tripoint(&second, cs1,cs2, &a2,&b2,&c2))
		return a1>=a2 && b1>=b2 && c1>=c2;
	return 0;
/*
	return first.in_a == second.in_a &&
		first.pos == second.pos;
*/
}


static void print_merger(FILE *out, struct file *a, struct file *c,
		 struct csl *cs1, struct csl *cs2,
		 struct point start, struct point end)
{
	while (!point_crossed(start, end, cs1,cs2)) {
#if 0
		printf("%c %d (%d,%d)\n", start.in_a?'A':'C', start.pos, start.c1,start.c2);
#endif
		if (start.in_a)
			printword(out, a->list[start.pos]);
		else
			printword(out, c->list[start.pos]);
		fflush(out); /* DEBUG */

		start.pos++;
		if (point_crossed(start, end, cs1,cs2))
			break;
		advance(cs1, cs2, &start);

	}
}

static int inline at_sol(struct file *f, int i)
{
	return i == 0 || i == f->elcnt || 
		ends_line(f->list[i-1]);
}

static void print_range(FILE *out, struct file *f, int start, int end)
{
	for (; start < end ; start++) 
		printword(out, f->list[start]);
}

static int print_conflict(FILE *out, struct file *a, struct file *b, struct file *c,
		    struct csl *c1, struct csl *c2,
		    struct point start, struct point end,
		    int words)
{
	int astart, bstart, cstart;
	int aend, bend, cend;
	int bi;

#if 0
	if (point_same(start,end))
		return 0; /* no conflict here !! */
#endif
	if (!tripoint(&start, c1,c2, &astart, &bstart, &cstart))
		abort();
	if (!tripoint(&end,   c1,c2, &aend,   &bend,   &cend))
		abort();
	

	/* Now contract the conflict if possible, but insist on
	 * an end-of-line boundary unless 'words'.
	 */
	/* first contract leading removed text.
	 * so <<<--- X 1 ||| X 2 === 3 --->>> becomes <<<--- 1 ||| 2 === 3 --->>>
	 */
	bi = bstart;
	while (bi < bend && start.c1 >= 0 && bi >= c1[start.c1].b && bi < c1[start.c1].b + c1[start.c1].len) {
		bi++;
		if (words || at_sol(b,bi)) {
			astart += bi-bstart;
			bstart = bi;
		}
	}
	/* and contract trailing removed text */
	bi = bend;
	while (bi > bstart && bi > c1[end.c1].b) {
		bi--;
		if (words || at_sol(b, bi)) {
			aend -= bend-bi;
			bend = bi;
		}
	}

	/* now contract leading unmatched text so
	 * <<<--- 1 ||| X 2 === X 3 --->>> becomes <<<--- 1 ||| 2 === 3 --->>>
	 */
	bi = bstart;
	while (bi < bend && start.c2 >= 0 && bi >= c2[start.c2].a && bi < c2[start.c2].a + c2[start.c2].len) {
		bi++;
		if (words || at_sol(b,bi)) {
			cstart += bi-bstart;
			bstart = bi;
		}
	}
	/* and trailing unmatched */
	bi = bend;
	while (bi > bstart && bi > c2[end.c2].a) {
		bi--;
		if (words || at_sol(b,bi)) {
			cend -= bend-bi;
			bend = bi;
		}
	}
	if (astart >= aend && bstart >= bend && cstart >= cend)
		return 0;

	fputs(words?"<<<---":"<<<<<<<\n", out);
	print_range(out, a, astart, aend);
	fputs(words?"|||":"|||||||\n", out);
	print_range(out, b, bstart, bend);
	fputs(words?"===":"=======\n", out);
	print_range(out, c, cstart, cend);
	fputs(words?"--->>>":">>>>>>>\n", out);
	return 1;
}

static int end_of_file(struct point p, struct csl *c1, struct csl *c2)
{
	return advance(c1,c2,&p)==0;
}

static int next_conflict(struct point here, struct csl *start_c1, struct csl *start_c2, 
			 struct point *start, struct point *end, 
			 struct file *a, struct file *b, struct file *c)
{
	/* We want to find the start and end of the 'next' conflict.
	 * There may not be another conflict, in which case set start and
	 * end to the end of the files.
	 * The start and end of a conflict must be the end and start of 
	 * regions where A matches B and B matches C - except for
	 * The start which might be the start of the file.
	 * 'here' is a potentially valid starting point. Any other starting
	 * point must be the end of a double coincidence.
	 *
	 * So we walk c1 and c2 looking for double coincidences and conflicts.
	 * When we find a conflict, we remember the fact.
	 * When we find a double coincidence we:
	 *   Set 'end' to the start of the DC.
	 *   If conflict-found - return.
	 *   Set 'start' to the end of the DC.
	 *   If the DC was EOF, start will == end == EOF, and we return.
	 *
	 * A double coincidence is easily detected by just looking at a single
	 * entry in c1 and c2.  If
	 *    c1->b+c1->len > c2->a && c2->a+c2->len > c1->b
	 *    || c1->len == c2->len == 0
	 * then we have a double coincidence.
	 *
	 * A conflict is detected when stepping forward.
	 * If we step c2 forward and the new coincidence is beyond or at the
	 * end of c1, or we step forward c1 and it's start is beyond or at the end of c2,
	 * then that is a conflict.
	 * Also, we can detect a conflict at start-of-file (here.in_a, here.pos==0) if 
	 * c2 doesn't start at 0.
	 *
	 * 'here' is significant only for its c1/c2 values. They will contain a
	 * double coincidence, though it might be start-of-file.
	 * start must be set to a backward-looking point at the end of a double-coincidence
	 * and end to a forward-looking point and the start of a double-coincidence
	 */


	int conflict_found = 0;
	struct csl *c1 = start_c1;
	struct csl *c2 = start_c2;

	
	c1 += here.c1;
	c2 += here.c2;

	*start = here;

	while (1) {
		/* Step one of c1 or c2 forward
		 * depending on which ends earlier.
		 * Watch to see if we are stepping over a conflict.
		 */
		if (c2 < start_c2) {
			/* start-of-file.
			 * Move both c1 and c2 forward.
			 *
			 * We have a conflict iff new c1->b > 0 and c2->a > 0
			 * or c1->b >0 && c2->b > 0
			 */
			c1++; c2++;
			if (c1->b > 0 &&
			    (c2->a > 0 || c2->b > 0))
				conflict_found = 1;
			if (c2->a+c2->len < c1->b)
				conflict_found = 1;
		} else if (c1->b+c1->len == c2->a+c2->len) {
			/* both coincidences end at same place. There is
			 * a conflict if there is a gap in c1->b or 
			 * c2->a has no gap but c2->b does (implying insertion
			 * at undefined location 
			 */
			if (c1->len && c2->len) {
				if (c1[1].b > c1->b + c1->len ||
				    (c2[1].a == c2->a + c2->len &&
				     c2[1].b > c2->b + c2->len))
					conflict_found = 1;
			}
			if (c1->len)
				c1++; 
			if (c2->len)
				c2++;
		} else if (c2->len ==0 || (c1->len && c1->b+c1->len < c2->a+c2->len)) {
			/* c1 ends earlier.  If the new start of c1 is
			 * beyond the current end of c2, we have a conflict
			 */
			c1++;
			if (c1->b > c2->a+c2->len)
				conflict_found = 1;
		} else {
			/* c2 ends earlier. If the new start of c2 is
			 * beyond the end of c1, we have a conflict.
			 * Also if the new start of c2 is at the end of c1,
			 * and the old end of c2 is also at end of c1,
			 * then have a conflict, as long as there was actually
			 * something inserted there...
			 */
			c2++;
			if (c2->a > c1->b+c1->len)
				conflict_found = 1;
		}
		if ((c1->len == 0 && c2->len ==0) ||
		    (c1->b+c1->len >= c2->a && c2->a+c2->len >= c1->b)
			) {
			/* double coincidence !
			 * It starts at max of c1->b and c2->a, in c
			 * and ends at min of c1->b+len (in a), c2->a+len (in c) 
			 */
			end->c1 = c1-start_c1;
			end->c2 = c2-start_c2;

			if (conflict_found) {
				/* end->c1/c2 holds the end of the conflict,
				 * and start->c1/c2 holds the start
				 * We need to set in_a and pos for each
				 * so that start is backward-looking and the end
				 * of a double-coincidence, and end is forward-looking
				 * at the start of a double-coincidence.
				 */

				c1 = start_c1;
				c2 = start_c2;

				if (start->c1 == -1) {
					start->in_a = 1;
					start->pos = 0;
				} else if (c1[start->c1].b+c1[start->c1].len <=
					   c2[start->c2].a+c2[start->c2].len) {
					start->in_a = 1;
					start->pos = c1[start->c1].a+c1[start->c1].len;
				} else {
					start->in_a = 0;
					start->pos = c2[start->c2].b+c2[start->c2].len;
				}
				retreat(c1,c2, start);

				if (c1[end->c1].b <= c2[end->c2].a) {
					end->in_a = 0;
					end->pos = c2[end->c2].b;
				} else {
					end->in_a = 0;
					end->pos = c2[end->c2].b +
						c1[end->c1].b - c2[end->c2].a;
				}
				advance(c1,c2, end);
				return 1;
			}
			start->c1 = c1-start_c1;
			start->c2 = c2-start_c2;

			if (c1->len == 0 && c2->len == 0) {
				/* eof and no conflict found.
				 * set start and end to eof
				 */
				start->in_a = end->in_a = 0;
				start->pos = end->pos = c2->b;
				return 0; 
			}
		}
	}
}

static int already_applied(struct csl *cs1, struct csl *cs2,
			   struct point start, struct point end,
			   struct file *a, struct file *b, struct file *c)
{
	/* check if this conflict reflects and already-applied change
	 * i.e. the section in a matches the section in b
	 */
	int a1,b1,c1;
	int a2,b2,c2;

	if (!tripoint(&start,cs1,cs2,&a1,&b1,&c1))
		abort();
	if (!tripoint(&end,cs1,cs2,&a2,&b2,&c2))
		abort();
	if (a1==a2 && b1==b2) return 0;
	if ((a2-a1) != (c2-c1)) return 0;

	while (a1<a2) {
		if (!match(&a->list[a1], &c->list[c1]))
			return 0;
		a1++;
		c1++;
	}
	return 1;
}

static int Startofline(struct point p, struct csl *cs1, struct csl *cs2,
		       struct file *a, struct file *b, struct file *c)
{
	int a1,b1,c1;
	return 
		tripoint(&p,cs1,cs2,&a1,&b1,&c1) &&
		at_sol(a,a1) && at_sol(b,b1) && at_sol(c,c1);

}

struct ci print_merge(FILE *out, struct file *a, struct file *b, struct file *c,
		 struct csl *c1, struct csl *c2,
		 int words)
{
	struct point start_last, end_last, start_next, end_next;

	struct ci rv;
	rv.ignored = rv.conflicts = 0;

#if 0
	{ int i;
	for (i=0; c1[i].len; i++) printf("%2d c1 %d:%d %d\n", i, c1[i].a,c1[i].b,c1[i].len);
	printf("%2d c1 %d:%d END\n", i, c1[i].a,c1[i].b);
	for (i=0; c2[i].len; i++) printf("%2d c2 %d:%d %d\n", i, c2[i].a,c2[i].b,c2[i].len);
	printf("%2d c2 %d:%d END\n", i, c2[i].a,c2[i].b);
	}
#endif
	/* end_last is a forward looking point */
	end_last.pos = 0;
	end_last.in_a = 1;
	end_last.c1 = end_last.c2 = -1;
	advance(c1,c2, &end_last);

	/* start_last is a backward looking point */
	start_last.pos = 0;
	start_last.in_a = 1;
	start_last.c1 = start_last.c2 = 0;
	retreat(c1,c2, &start_last);

	while (!end_of_file(end_last, c1, c2)) {
		next_conflict(end_last, c1, c2, &start_next, &end_next, a, b, c);
		while (already_applied(c1,c2,start_next,end_next,a,b,c)) {
			rv.ignored++;
			next_conflict(end_next, c1,c2,&start_next,&end_next,a,b,c);
		}
#if 0
		printf("start %d %d (%d,%d)  end %d %d (%d,%d)\n",
		       start_next.in_a, start_next.pos, start_next.c1, start_next.c2,
		       end_next.in_a, end_next.pos, end_next.c1, end_next.c2);
#endif
		while (!point_crossed(end_last, start_next,c1,c2) &&
		       !(words || Startofline(end_last, c1,c2, a,b,c))) {
			end_last.pos++;
			advance(c1,c2, &end_last);
		}

		while (!point_crossed(end_last, start_next, c1,c2) &&
		       !(words || Startofline(start_next, c1,c2, a,b,c))) {
			start_next.pos--;
			retreat(c1,c2, &start_next);
		}

		if (point_crossed(end_last, start_next, c1,c2)) {
			end_last = end_next;
			continue;
		}
		if (print_conflict(out, a,b,c, c1,c2, start_last, end_last, words))
			rv.conflicts++;

		print_merger(out, a,c, c1,c2, end_last, start_next);
		start_last = start_next;
		end_last = end_next;
	}
	if (print_conflict(out,a,b,c, c1,c2, start_last, end_last, words))
		rv.conflicts++;
	return rv;
}
