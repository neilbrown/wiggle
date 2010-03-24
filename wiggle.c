/*
 * wiggle - apply rejected patches
 *
 * Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
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

/*
 * Wiggle is a tool for working with patches that don't quite apply properly.
 * It provides functionality similar to 'diff' and 'merge' but can
 * work at the level of individual words thus allowing the merging of
 * two changes that affect the same line, but not the same parts of that line.
 *
 * Wiggle can also read patch and merge files.  Unlike 'merge' it does not
 * need to be given three separate files, but can be given a file and a patch
 * and it will extract the pieces of the two two other files that it needs from
 * the patch.
 *
 * Wiggle performs one of three core function:
 *   --extract -x    extract part of a patch or merge file
 *   --diff -d       report differences between two files
 *   --merge -m      merge the changes between two files into a third file
 *
 * To perform these, wiggle requires 1, 2, or 3 input streams respectively.
 * I can get there from individual files, from a diff (unified or context) or
 * from a merge file.
 *
 * For merge:
 *    If one file is given, it is a merge file (output of 'merge').
 *    If two files are given, the second is assumed to be a patch, the first is a normal file.
 *    If three files are given, they are taken to be normal files.
 *
 * For diff:
 *    If one file is given, it is a patch
 *    If two files are given, they are normal files.
 *
 * For extract:
 *    Only one file can be given. -p indicates it is a patch, otherwise it is a merge.
 *    One of the flags -1 -2 or -3 must also be given and they indicate which
 *    part of the patch or merge to extract.
 *
 * Difference calculate and merging is performed on lines (-l) or words (-w).
 * In the case of -w, an initial diff is computed based on non-trivial words.
 *  i.e. spaces are ignored
 * This diff is computed from the ends of the file and is used to find a suitable
 * starting point and range.  Then a more precise diff is computed over that
 *  restricted range
 *
 * Other options available are:
 *   --replace -r   replace first file with  result of merge.
 *   --help -h      provide help
 *   --version -v   version
 *
 * Defaults are --merge --words
 *
 */

#include	"wiggle.h"
#include	<errno.h>
#include	<string.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<ctype.h>

char *Cmd = "wiggle";

void die()
{
	fprintf(stderr,"wiggle: fatal error\n");
	abort();
	exit(3);
}

void printword(FILE *f, struct elmnt e)
{
	if (e.start[0])
		fprintf(f, "%.*s", e.len, e.start);
	else {
		int a,b,c;
		sscanf(e.start+1, "%d %d %d", &a, &b, &c);
		fprintf(f, "*** %d,%d **** %d\n", b,c,a);
	}
}

static void printsep(struct elmnt e1, struct elmnt e2)
{
	int a,b,c,d,e,f;
	sscanf(e1.start+1, "%d %d %d", &a, &b, &c);
	sscanf(e2.start+1, "%d %d %d", &d, &e, &f);
	printf("@@ -%d,%d +%d,%d @@\n", b,c,e,f);
}


/* Remove any entries from the common-sublist that are
 * just spaces, tabs, or newlines
 */
void cleanlist(struct file a, struct file b, struct csl *list)
{
	struct csl *new = list;

	while (list->len) {
		int i;
		int ap;
		for( ap = list->a; ap< list->a+list->len; ap++) {
			for (i=0; i<a.list[ap].len; i++) {
				char c = a.list[ap].start[i];
				if (isalnum(c))
					break;
			}
			if (i != a.list[ap].len)
				break;
		}
		if (ap == list->a+list->len)
			list++;
		else
			*new++ = *list++;
	}
	*new = *list;
}

int main(int argc, char *argv[])
{
	int opt;
	int option_index;
	int mode = 0;
	int obj = 0;
	int replace = 0;
	char *replacename=NULL, *orignew=NULL;
	int which = 0;
	int ispatch = 0;
	int reverse = 0;
	int verbose=0, quiet=0;
	int i;
	int strip = -1;
	int chunks1=0, chunks2=0, chunks3=0;
	int exit_status = 0;
	int ignore = 1;
	FILE *outfile = stdout;
	char *helpmsg;
	char *base0;

	struct stream f, flist[3];
	struct file fl[3];
	struct csl *csl1, *csl2;

	base0 = strrchr(argv[0], '/');
	if (base0) base0++; else base0=argv[0];
#if 0
	/* The name 'vpatch' seems to be used elsewhere */
	if (strcmp(base0, "vpatch")==0) {
		Cmd = base0;
		mode = 'B';
	}
#endif
	while ((opt = getopt_long(argc, argv,
				  short_options(mode), long_options,
				  &option_index)) != -1)
		switch(opt) {
		case 'h':
			helpmsg = Help;
			switch(mode) {
			case 'x': helpmsg = HelpExtract; break;
			case 'd': helpmsg = HelpDiff; break;
			case 'm': helpmsg = HelpMerge; break;
			case 'B': helpmsg = HelpBrowse; break;
			}
			fputs(helpmsg, stderr);
			exit(0);

		case 'V':
			fputs(Version, stderr);
			exit(0);
		case ':':
		case '?':
		default:
			fputs(Usage, stderr);
			exit(2);

		case 'B':
		case 'x':
		case 'd':
		case 'm':
			if (mode ==0){
				mode = opt;
				continue;
			}
			fprintf(stderr, "wiggle: mode is '%c' - cannot set to '%c'\n",
				mode, opt);
			exit(2);

		case 'w':
		case 'l':
			if (obj == 0 || obj == opt) {
				obj = opt;
				continue;
			}
			fprintf(stderr, "wiggle: cannot select both words and lines.\n");
			exit(2);

		case 'r':
			replace = 1;
			continue;
		case 'R':
			reverse = 1;
			continue;

		case 'i':
			ignore = 0;
			continue;

		case '1':
		case '2':
		case '3':
			if (which == 0 || which == opt) {
				which = opt;
				continue;
			}
			fprintf(stderr, "wiggle: can only select one of -1, -2, -3\n");
			exit(2);

		case 'p':
			if (mode == 'B')
				strip = atol(optarg?optarg:"0");
			else if (optarg) {
				fprintf(stderr, "wiggle: SORRY, PARSE ERROR\n");
				exit(2);
			} else
				ispatch = 1;
			continue;

		case 'v': verbose++; continue;
		case 'q': quiet=1 ; continue;
		}
	if (!mode)
		mode = 'm';

	if (mode == 'B') {
		vpatch(argc-optind, argv+optind, strip, reverse, replace);
		/* should not return */
		exit(1);
	}

	if (obj && mode == 'x') {
		fprintf(stderr,"wiggle: cannot specify --line or --word with --extract\n");
		exit(2);
	}
	if (mode != 'm' && !obj) obj = 'w';
	if (replace && mode != 'm') {
		fprintf(stderr, "wiggle: --replace only allowed with --merge\n");
		exit(2);
	}
	if (mode == 'x' && !which) {
		fprintf(stderr, "wiggle: must specify -1, -2 or -3 with --extract\n");
		exit(2);
	}
	if (mode != 'x' && mode != 'd' && which) {
		fprintf(stderr, "wiggle: -1, -2 or -3 only allowed with --extract or --diff\n");
		exit(2);
	}
	if (ispatch && (mode != 'x' && mode != 'd')) {
		fprintf(stderr, "wiggle: --patch only allowed with --extract or --diff\n");
		exit(2);
	}
	if (ispatch && which == '3') {
		fprintf(stderr, "wiggle: cannot extract -3 from a patch.\n");
		exit(2);
	}

	switch(mode) {
	case 'x':
		/* extract a branch of a diff or diff3 or merge output
		 * We need one file
		 */
		if (optind == argc) {
			fprintf(stderr, "wiggle: no file given for --extract\n");
			exit(2);
		}
		if (optind < argc-1) {
			fprintf(stderr, "wiggle: only give one file for --extract\n");
			exit(2);
		}
		f = load_file(argv[optind]);
		if (f.body==NULL) {
			fprintf(stderr, "wiggle: cannot load file '%s' - %s\n",
				argv[optind], strerror(errno));
			exit(2);
		}
		if (ispatch)
			chunks1 = chunks2 = split_patch(f, &flist[0], &flist[1]);
		else {
			if (!split_merge(f, &flist[0], &flist[1], &flist[2])) {
				fprintf(stderr, "wiggle: merge file %s looks bad.\n",
					argv[optind]);
				exit(2);
			}
		}
		if (flist[which-'1'].body == NULL) {
			fprintf(stderr, "wiggle: %s has no -%c component.\n",
				argv[optind], which);
			exit(2);
		} else {
			write(1, flist[which-'1'].body, flist[which-'1'].len);
		}

		break;
	case 'd':
		/* create a diff (line or char) of two streams */
		switch (argc-optind) {
		case 0:
			fprintf(stderr, "wiggle: no file given for --diff\n");
			exit(2);
		case 1:
			f = load_file(argv[optind]);
			if (f.body == NULL) {
				fprintf(stderr, "wiggle: cannot load file '%s' - %s\n",
					argv[optind], strerror(errno));
				exit(2);
			}
			chunks1 = chunks2 = split_patch(f, &flist[0], &flist[1]);
			if (!flist[0].body || !flist[1].body) {
				fprintf(stderr, "wiggle: couldn't parse patch %s\n",
					argv[optind]);
				exit(2);
			}
			break;
		case 2:
			flist[0] = load_file(argv[optind]);
			if (flist[0].body == NULL) {
				fprintf(stderr, "wiggle: cannot load file '%s' - %s\n",
					argv[optind], strerror(errno));
				exit(2);
			}
			if (ispatch) {
				f = load_file(argv[optind+1]);
				if (f.body == NULL) {
					fprintf(stderr, "wiggle: cannot load patch '%s' - %s\n",
						argv[optind], strerror(errno));
					exit(2);
				}
				if (which == '2')
					chunks2 = chunks3 = split_patch(f, &flist[2], &flist[1]);
				else
					chunks2 = chunks3 = split_patch(f, &flist[1], &flist[2]);

			} else
				flist[1] = load_file(argv[optind+1]);
			if (flist[1].body == NULL) {
				fprintf(stderr, "wiggle: cannot load file '%s' - %s\n",
					argv[optind+1], strerror(errno));
				exit(2);
			}
			break;
		default:
			fprintf(stderr, "wiggle: too many files given for --diff\n");
			exit(2);
		}
		if (reverse) {
			f=flist[1];
			flist[1] = flist[2];
			flist[2]= f;
		}
		if (obj == 'l') {
			int a,b;
			fl[0] = split_stream(flist[0], ByLine, 0);
			fl[1] = split_stream(flist[1], ByLine, 0);
			if (chunks2 && ! chunks1)
				csl1 = pdiff(fl[0], fl[1], chunks2);
			else
				csl1 = diff(fl[0], fl[1]);

			if (!chunks1)
				printf("@@ -1,%d +1,%d @@\n", fl[0].elcnt, fl[1].elcnt);
			a = b = 0;
			while (a<fl[0].elcnt || b < fl[1].elcnt) {
				if (a < csl1->a) {
					if (fl[0].list[a].start[0]) {
						printf("-");
						printword(stdout, fl[0].list[a]);
					}
					a++;
					exit_status++;
				} else if (b < csl1->b) {
					if (fl[1].list[b].start[0]) {
						printf("+");
						printword(stdout, fl[1].list[b]);
					}
					b++;
					exit_status++;
				} else {
					if (fl[0].list[a].start[0] == '\0')
						printsep(fl[0].list[a], fl[1].list[b]);
					else {
						printf(" ");
						printword(stdout, fl[0].list[a]);
					}
					a++;
					b++;
					if (a >= csl1->a+csl1->len)
						csl1++;
				}
			}
		} else {
			int a,b;
			int sol = 1; /* start of line */
			fl[0] = split_stream(flist[0], ByWord, 0);
			fl[1] = split_stream(flist[1], ByWord, 0);
			if (chunks2 && !chunks1)
				csl1 = pdiff(fl[0], fl[1], chunks2);
			else
				csl1 = diff(fl[0], fl[1]);

			if (!chunks1) {
				/* count lines in each file */
				int l1, l2, i;
				l1=l2=0;
				for (i=0;i<fl[0].elcnt;i++)
					if (ends_line(fl[0].list[i]))
						l1++;
				for (i=0;i<fl[1].elcnt;i++)
					if (ends_line(fl[1].list[i]))
						l2++;
				printf("@@ -1,%d +1,%d @@\n", l1,l2);
			}
			a = b = 0;
			while (a < fl[0].elcnt || b < fl[1].elcnt) {
				if (a < csl1->a) {
					exit_status++;
					if (sol) {
						int a1;
						/* If we remove a whole line, output +line
						 * else clear sol and retry */
						sol = 0;
						for (a1=a; a1<csl1->a;a1++)
							if (ends_line(fl[0].list[a1])) {
								sol=1;
								break;
							}
						if (sol) {
							printf("-");
							for (; a<csl1->a; a++) {
								printword(stdout, fl[0].list[a]);
								if (ends_line(fl[0].list[a])) {
									a++;
									break;
								}
							}
						} else printf("|");
					}
					if (!sol) {
						printf("<<<--");
						do {
							if (sol) printf("|");
							printword(stdout, fl[0].list[a]);
							sol = ends_line(fl[0].list[a]);
							a++;
						} while (a < csl1->a);
						printf("%s-->>>", sol?"|":"");
						sol=0;
					}
				} else if (b < csl1->b) {
					exit_status++;
					if (sol) {
						int b1;
						sol = 0;
						for (b1=b; b1<csl1->b;b1++)
							if(ends_line(fl[1].list[b1])) {
								sol=1;
								break;
							}
						if (sol) {
							printf("+");
							for(; b<csl1->b ; b++) {
								printword(stdout, fl[1].list[b]);
								if(ends_line(fl[1].list[b])) {
									b++;
									break;
								}
							}
						} else printf("|");
					}
					if (!sol) {
						printf("<<<++");
						do {
							if (sol) printf("|");
							printword(stdout, fl[1].list[b]);
							sol = ends_line(fl[1].list[b]);
							b++;
						} while (b < csl1->b);
						printf("%s++>>>",sol?"|":"");
						sol=0;
					}
				} else {
					if (sol) {
						int a1;
						sol = 0;
						for (a1=a; a1<csl1->a+csl1->len; a1++)
							if (ends_line(fl[0].list[a1]))
								sol=1;
						if (sol) {
							if (fl[0].list[a].start[0]) {
								printf(" ");
								for(; a<csl1->a+csl1->len; a++,b++) {
									printword(stdout, fl[0].list[a]);
									if (ends_line(fl[0].list[a])) {
										a++,b++;
										break;
									}
								}
							} else {
								printsep(fl[0].list[a], fl[1].list[b]);
								a++; b++;
							}
						}
						else printf("|");
					}
					if (!sol) {
						printword(stdout, fl[0].list[a]);
						if (ends_line(fl[0].list[a]))
							sol=1;
						a++;
						b++;
					}
					if (a >= csl1->a+csl1->len)
						csl1++;
				}
			}

		}
		break;
	case 'm':
		/* merge three files, A B C, so changed between B and C get made to A
		 */
		switch (argc-optind) {
		case 0:
			fprintf(stderr, "wiggle: no files given for --merge\n");
			exit(2);
		case 3:
		case 2:
		case 1:
			for (i=0; i< argc-optind; i++) {
				flist[i] = load_file(argv[optind+i]);
				if (flist[i].body == NULL) {
					fprintf(stderr, "wiggle: cannot load file '%s' - %s\n",
						argv[optind+i], strerror(errno));
					exit(2);
				}
			}
			break;
		default:
			fprintf(stderr, "wiggle: too many files given for --merge\n");
			exit(2);
		}
		switch(argc-optind) {
		case 1: /* a merge file */
			f = flist[0];
			if (!split_merge(f, &flist[0], &flist[1], &flist[2])) {
				fprintf(stderr,"wiggle: merge file %s looks bad.\n",
					argv[optind]);
				exit(2);
			}
			break;
		case 2: /* a file and a patch */
			f = flist[1];
			chunks2 = chunks3 = split_patch(f, &flist[1], &flist[2]);
			break;
		case 3: /* three separate files */
			break;
		}
		if (reverse) {
			f=flist[1];
			flist[1] = flist[2];
			flist[2]= f;
		}

		for (i=0; i<3; i++) {
			if (flist[i].body==NULL) {
				fprintf(stderr, "wiggle: file %d missing\n", i);
				exit(2);
			}
		}
		if (replace) {
			int fd;
			replacename = malloc(strlen(argv[optind])+ 20);
			if (!replacename) die();
			orignew = malloc(strlen(argv[optind])+20);
			if (!orignew) die();
			strcpy(replacename, argv[optind]);
			strcpy(orignew, argv[optind]);
			strcat(orignew, ".porig");
			if (open(orignew, O_RDONLY) >= 0 ||
			    errno != ENOENT) {
				fprintf(stderr,"wiggle: %s already exists\n",
					orignew);
				exit(2);
			}
			strcat(replacename,"XXXXXX");
			fd = mkstemp(replacename);
			if (fd == -1) {
				fprintf(stderr,"wiggle: could not create temporary file for %s\n",
					replacename);
				exit(2);
			}
			outfile = fdopen(fd, "w");

		}

		if (obj == 'l') {
			fl[0] = split_stream(flist[0], ByLine, 0);
			fl[1] = split_stream(flist[1], ByLine, 0);
			fl[2] = split_stream(flist[2], ByLine, 0);
		} else {
			fl[0] = split_stream(flist[0], ByWord, 0);
			fl[1] = split_stream(flist[1], ByWord, 0);
			fl[2] = split_stream(flist[2], ByWord, 0);
		}
		if (chunks2 && !chunks1)
			csl1 = pdiff(fl[0], fl[1], chunks2);
		else
			csl1 = diff(fl[0], fl[1]);
		csl2 = diff(fl[1], fl[2]);

#if 0
		cleanlist(fl[0],fl[1],csl1);
		cleanlist(fl[1],fl[2],csl2);
#endif

		{
			struct ci ci;

			ci = print_merge2(outfile, &fl[0], &fl[1], &fl[2],
					  csl1, csl2, obj=='w',
					  ignore);
			if (!quiet && ci.conflicts)
				fprintf(stderr, "%d unresolved conflict%s found\n", ci.conflicts, ci.conflicts==1?"":"s");
			if (!quiet && ci.ignored)
				fprintf(stderr, "%d already-applied change%s ignored\n", ci.ignored, ci.ignored==1?"":"s");
			exit_status = (ci.conflicts > 0);
		}
		if (replace) {
			fclose(outfile);
			if (rename(argv[optind], orignew) ==0 &&
			    rename(replacename, argv[optind]) ==0)
				/* all ok */;
			else {
				fprintf(stderr, "wiggle: failed to move new file into place.\n");
				exit(2);
			}
		}
		break;

	}
	exit(exit_status);
}
