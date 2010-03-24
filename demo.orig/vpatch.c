
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

#define assert(x) do { if (!(x)) abort(); } while (0)

struct plist {
	char *file;
	unsigned int start, end;
	int parent;
	int next, prev, last;
	int open;
	int chunks, wiggles, conflicts;
};

struct plist *patch_add_file(struct plist *pl, int *np, char *file, 
	       unsigned int start, unsigned int end)
{
	/* size of pl is 0, 16, n^2 */
	int n = *np;
	int asize;

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
	pl[n].chunks = pl[n].wiggles = pl[n].conflicts = 0;
	pl[n].open = 1;
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
		if (pos > target2) {
			end = of ? ftell(of) : ftell(f);
			end -= (pos - target2) - 1;
			plist = patch_add_file(plist, np,
					       strdup(name), start, end);
		}
	}
	return plist;
}
void die()
{
	fprintf(stderr,"vpatch: fatal error\n");
	abort();
	exit(3);
}


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
		while (*file == '/') *file++;
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

void draw_one(int row, struct plist *pl)
{
	char hdr[10];
	hdr[0] = 0;

	if (pl == NULL) {
		move(row,0);
		clrtoeol();
		return;
	}
	if (pl->chunks > 99)
		strcpy(hdr, "XX");
	else sprintf(hdr, "%02d", pl->chunks);
	if (pl->wiggles > 99)
		strcpy(hdr, " XX");
	else sprintf(hdr+2, " %02d", pl->wiggles);
	if (pl->conflicts > 99)
		strcpy(hdr, " XX");
	else sprintf(hdr+5, " %02d ", pl->conflicts);
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

void diff_window(struct plist *p, FILE *f)
{
	/*
	 * I wonder what to display here ....
	 */
	struct stream s;
	struct stream  s1, s2;
	struct file f1, f2;
	struct csl *csl;
	char buf[100];
	int ch;
	s = load_segment(f, p->start, p->end);
	ch = split_patch(s, &s1, &s2);

	clear();
	sprintf(buf, "Chunk count: %d\n", ch);
	mvaddstr(1,1,buf); clrtoeol();


	f1 = split_stream(s1, ByWord, 0);
	f2 = split_stream(s2, ByWord, 0);

	csl = diff(f1, f2);

	/* now try to display the diff highlighted */
	int sol = 1;
	int a=0, b=0;

	while(a<f1.elcnt || b < f2.elcnt) {
		if (a < csl->a) {
			if (sol) {
				int a1;
				/* if we remove a whole line, output +line,
				 * else clear sol and retry 
				 */
				sol = 0;
				for (a1=a; a1<csl->a; a1++)
					if (f1.list[a1].start[0] == '\n') {
						sol = 1;
						break;
					}
				if (sol) {
					addch('-');
					attron(A_UNDERLINE);
					for (; a<csl->a; a++) {
						addword(f1.list[a]);
						if (f1.list[a].start[0] == '\n') {
							a++;
							break;
						}
					}
					attroff(A_UNDERLINE);
				} else addch('|');
			}
			if (!sol) {
				attron(A_UNDERLINE);
				do {
					if (sol) {
						attroff(A_UNDERLINE);
						addch('|');
						attron(A_UNDERLINE);
					}
					addword(f1.list[a]);
					sol = (f1.list[a].start[0] == '\n');
					a++;
				} while (a < csl->a);
				attroff(A_UNDERLINE);
				if (sol) addch('|');
				sol = 0;
			}
		} else if (b < csl->b) {
			if (sol) {
				int b1;
				sol = 0;
				for (b1=b; b1<csl->b; b1++)
					if (f2.list[b1].start[0] == '\n') {
						sol = 1;
						break;
					}
				if (sol) {
					addch('+');
					attron(A_BOLD);
					for (; b<csl->b; b++) {
						addword(f2.list[b]);
						if (f2.list[b].start[0] == '\n') {
							b++;
							break;
						}
					}
					attroff(A_BOLD);
				} else addch('|');
			}
			if (!sol) {
				attron(A_BOLD);	
				do {
					if (sol) {
						attroff(A_BOLD);
						addch('|');
						attron(A_BOLD);
					}
					addword(f2.list[b]);
					sol = (f2.list[b].start[0] == '\n');
					b++;
				} while (b < csl->b);
				attroff(A_BOLD);
				if (sol) addch('|');
				sol = 0;
			}
		} else { 
			if (sol) {
				int a1;
				sol = 0;
				for (a1=a; a1<csl->a+csl->len; a1++)
					if (f1.list[a1].start[0] == '\n')
						sol = 1;
				if (sol) {
					if (f1.list[a].start[0]) {
						addch(' ');
						for (; a< csl->a+csl->len; a++,b++) {
							addword(f1.list[a]);
							if (f1.list[a].start[0]=='\n') {
								a++,b++;
								break;
							}
						}
					} else {
						addstr("SEP\n");
						a++; b++;
					}
				} else addch('|');
			}
			if (!sol) {
				addword(f1.list[a]);
				if (f1.list[a].start[0] == '\n')
					sol = 1;
				a++;
				b++;
			}
			if (a >= csl->a+csl->len)
				csl++;
		}
	} 


	getch();

	free(s1.body);
	free(s2.body);
	free(f1.list);
	free(f2.list);
}

void main_window(struct plist *pl, int n, FILE *f)
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
	int c;

	while(1) {
		if (refresh == 2) {
			clear();
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
				draw_one(i, &pl[tpos]);
				tpos = get_prev(tpos, pl, n);
			}
			tpos = pos;
			for (i=row+1; i<rows; i++) {
				tpos = get_next(tpos, pl, n);
				if (tpos >= 0)
					draw_one(i, &pl[tpos]);
				else
					draw_one(i, NULL);
			}
		}
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
				diff_window(&pl[pos], f);
				refresh = 2;
			}
			break;
		case 27: /* escape */
		case 'q':
			return;
		}
	}
}


int main(int argc, char *argv[])
{
	int n = 0;
	FILE *f = NULL;
	FILE *in = stdin;
	struct plist *pl;

	if (argc == 3)
		f = fopen(argv[argc-1], "w+");
	if (argc >=2)
		in = fopen(argv[1], "r");
	else {
		printf("no arg...\n");
		exit(2);
	}

	pl = parse_patch(in, f, &n);
	pl = sort_patches(pl, &n);

	if (f) {
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
	nonl(); intrflush(stdscr, FALSE); keypad(stdscr, TRUE);
	mousemask(ALL_MOUSE_EVENTS, NULL);

	main_window(pl, n, in);

	nocbreak();nl();endwin();
	return 0;
}
