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
 *    along with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
 * and it will extract the pieces of the two other files that it needs from
 * the patch.
 *
 * Wiggle performs one of three core function:
 *   --extract -x    extract part of a patch or merge file
 *   --diff -d       report differences between two files
 *   --merge -m      merge the changes between two files into a third file
 *
 * This is also a --browse (-B) mode which provides interactive access
 * to the merger.
 *
 * To perform these, wiggle requires 1, 2, or 3 input streams respectively.
 * I can get these from individual files, from a diff (unified or context) or
 * from a merge file.
 *
 * For merge:
 *    If one file is given, it is a merge file (output of 'merge').
 *    If two files are given, the second is assumed to be a patch,
 *         the first is a normal file.
 *    If three files are given, they are taken to be normal files.
 *
 * For diff:
 *    If one file is given, it is a patch
 *    If two files are given, they are normal files.
 *
 * For extract:
 *    Only one file can be given. -p indicates it is a patch,
 *        otherwise it is a merge.
 *    One of the flags -1 -2 or -3 must also be given and they indicate which
 *    part of the patch or merge to extract.
 *
 * Difference calculation and merging is performed on lines (-l) or words (-w).
 * Each 'word' is either 1/all alphnumeric (or '_'), 2/ all space or tab,
 * or 3/ any other single character.
 *
 * In the case of -w, an initial diff is computed based on non-trivial words
 * which includes alhpanumeric words and newlines.
 *
 * This diff is computed from the ends of the file and is used to find
 * a suitable starting point and range.  Then a more precise diff is
 * computed over that restricted range
 *
 * Other options available are:
 *   --replace -r   replace first file with  result of merge.
 *   --help -h      provide help
 *   --version -v   version
 *
 * Defaults are --merge --words
 *
 */
#define _GNU_SOURCE
#include	"wiggle.h"
#include	<errno.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<ctype.h>
#include	<sys/stat.h>

char *Cmd = "wiggle";
int do_trace = 0;

void die(char *reason)
{
	fprintf(stderr, "%s: fatal error: %s failure\n", Cmd, reason);
	exit(3);
}

void check_dir(char *name, int fd)
{
	struct stat st;
	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "%s: fatal: %s is strange\n", Cmd, name);
		exit(3);
	}
	if (S_ISDIR(st.st_mode)) {
		fprintf(stderr, "%s: %s is a directory\n", Cmd, name);
		exit(3);
	}
}

void *xmalloc(int size)
{
	void *rv = malloc(size);
	if (size && !rv) {
		char *msg = "Failed to allocate memory - aborting\n";
		write(2, msg, strlen(msg));
		exit(3);
	}
	return rv;
}

void printword(FILE *f, struct elmnt e)
{
	if (e.start[0])
		fprintf(f, "%.*s", e.plen + e.prefix,
			e.start - e.prefix);
	else {
		int a, b, c;
		sscanf(e.start+1, "%d %d %d", &a, &b, &c);
		fprintf(f, "*** %d,%d **** %d%s", b, c, a, e.start+18);
	}
}

static void printsep(struct elmnt e1, struct elmnt e2)
{
	int a, b, c, d, e, f;
	sscanf(e1.start+1, "%d %d %d", &a, &b, &c);
	sscanf(e2.start+1, "%d %d %d", &d, &e, &f);
	printf("@@ -%d,%d +%d,%d @@%s", b, c, e, f, e1.start+18);
}

static int extract(int argc, char *argv[], int ispatch, int which)
{
	/* extract a branch of a diff or diff3 or merge output
	 * We need one file
	 */
	struct stream f, flist[3];

	if (argc == 0) {
		fprintf(stderr,
			"%s: no file given for --extract\n", Cmd);
		return 2;
	}
	if (argc > 1) {
		fprintf(stderr,
			"%s: only give one file for --extract\n", Cmd);
		return 2;
	}
	f = load_file(argv[0]);
	if (f.body == NULL) {
		fprintf(stderr,
			"%s: cannot load file '%s' - %s\n", Cmd,
			argv[0], strerror(errno));
		return 2;
	}
	if (ispatch) {
		if (split_patch(f, &flist[0], &flist[1]) == 0) {
			fprintf(stderr,
				"%s: No chunk found in patch: %s\n", Cmd,
				argv[0]);
			return 0;
		}
	} else {
		if (!split_merge(f, &flist[0], &flist[1], &flist[2])) {
			fprintf(stderr,
				"%s: merge file %s looks bad.\n", Cmd,
				argv[0]);
			return 2;
		}
	}
	if (flist[which-'1'].body == NULL) {
		fprintf(stderr,
			"%s: %s has no -%c component.\n", Cmd,
			argv[0], which);
		return 2;
	} else {
		if (write(1, flist[which-'1'].body,
			  flist[which-'1'].len)
		    != flist[which-'1'].len)
			return 2;
	}
	return 0;
}

static int do_diff_lines(struct file fl[2], struct csl *csl)
{
	int a, b;
	int exit_status = 0;
	a = b = 0;
	while (a < fl[0].elcnt || b < fl[1].elcnt) {
		if (a < csl->a) {
			if (fl[0].list[a].start[0]) {
				printf("-");
				printword(stdout,
					  fl[0].list[a]);
			}
			a++;
			exit_status++;
		} else if (b < csl->b) {
			if (fl[1].list[b].start[0]) {
				printf("+");
				printword(stdout,
					  fl[1].list[b]);
			}
			b++;
			exit_status++;
		} else {
			if (fl[0].list[a].start[0] == '\0')
				printsep(fl[0].list[a],
					 fl[1].list[b]);
			else {
				printf(" ");
				printword(stdout,
					  fl[0].list[a]);
			}
			a++;
			b++;
			if (a >= csl->a+csl->len)
				csl++;
		}
	}
	return exit_status;
}

static int do_diff_words(struct file fl[2], struct csl *csl)
{
	int a, b;
	int exit_status  = 0;
	int sol = 1; /* start of line */
	a = b = 0;
	while (a < fl[0].elcnt || b < fl[1].elcnt) {
		if (a < csl->a) {
			exit_status++;
			if (sol) {
				int a1;
				/* If we remove a
				 * whole line, output
				 * +line else clear
				 * sol and retry */
				sol = 0;
				for (a1 = a; a1 < csl->a ; a1++)
					if (ends_line(fl[0].list[a1])) {
						sol = 1;
						break;
					}
				if (sol) {
					printf("-");
					for (; a < csl->a ; a++) {
						printword(stdout, fl[0].list[a]);
						if (ends_line(fl[0].list[a])) {
							a++;
							break;
						}
					}
				} else
					printf("|");
			}
			if (!sol) {
				printf("<<<--");
				do {
					if (sol)
						printf("|");
					printword(stdout, fl[0].list[a]);
					sol = ends_line(fl[0].list[a]);
					a++;
				} while (a < csl->a);
				printf("%s-->>>", sol ? "|" : "");
				sol = 0;
			}
		} else if (b < csl->b) {
			exit_status++;
			if (sol) {
				int b1;
				sol = 0;
				for (b1 = b; b1 < csl->b; b1++)
					if (ends_line(fl[1].list[b1])) {
						sol = 1;
						break;
					}
				if (sol) {
					printf("+");
					for (; b < csl->b ; b++) {
						printword(stdout, fl[1].list[b]);
						if (ends_line(fl[1].list[b])) {
							b++;
							break;
						}
					}
				} else
					printf("|");
			}
			if (!sol) {
				printf("<<<++");
				do {
					if (sol)
						printf("|");
					printword(stdout, fl[1].list[b]);
					sol = ends_line(fl[1].list[b]);
					b++;
				} while (b < csl->b);
				printf("%s++>>>", sol ? "|" : "");
				sol = 0;
			}
		} else {
			if (sol) {
				int a1;
				sol = 0;
				for (a1 = a; a1 < csl->a+csl->len; a1++)
					if (ends_line(fl[0].list[a1]))
						sol = 1;
				if (sol) {
					if (fl[0].list[a].start[0]) {
						printf(" ");
						for (; a < csl->a+csl->len; a++, b++) {
							printword(stdout, fl[0].list[a]);
							if (ends_line(fl[0].list[a])) {
								a++, b++;
								break;
							}
						}
					} else {
						printsep(fl[0].list[a], fl[1].list[b]);
						a++; b++;
					}
				} else
					printf("|");
			}
			if (!sol) {
				printword(stdout, fl[0].list[a]);
				if (ends_line(fl[0].list[a]))
					sol = 1;
				a++;
				b++;
			}
			if (a >= csl->a+csl->len)
				csl++;
		}
	}
	return exit_status;
}

static int do_diff(int argc, char *argv[], int obj, int ispatch,
		   int which, int reverse)
{
	/* create a diff (line or char) of two streams */
	struct stream f, flist[3];
	int chunks1 = 0, chunks2 = 0, chunks3 = 0;
	int exit_status = 0;
	struct file fl[2];
	struct csl *csl;

	switch (argc) {
	case 0:
		fprintf(stderr, "%s: no file given for --diff\n", Cmd);
		return 2;
	case 1:
		f = load_file(argv[0]);
		if (f.body == NULL) {
			fprintf(stderr,
				"%s: cannot load file '%s' - %s\n", Cmd,
				argv[0], strerror(errno));
			return 2;
		}
		chunks1 = chunks2 =
			split_patch(f, &flist[0], &flist[1]);
		if (!flist[0].body || !flist[1].body) {
			fprintf(stderr,
				"%s: couldn't parse patch %s\n", Cmd,
				argv[0]);
			return 2;
		}
		break;
	case 2:
		flist[0] = load_file(argv[0]);
		if (flist[0].body == NULL) {
			fprintf(stderr,
				"%s: cannot load file '%s' - %s\n", Cmd,
				argv[0], strerror(errno));
			return 2;
		}
		if (ispatch) {
			f = load_file(argv[1]);
			if (f.body == NULL) {
				fprintf(stderr,
					"%s: cannot load patch '%s' - %s\n", Cmd,
					argv[1], strerror(errno));
				return 2;
			}
			if (which == '2')
				chunks2 = chunks3 =
					split_patch(f, &flist[2],
						    &flist[1]);
			else
				chunks2 = chunks3 =
					split_patch(f, &flist[1],
						    &flist[2]);

		} else
			flist[1] = load_file(argv[1]);
		if (flist[1].body == NULL) {
			fprintf(stderr,
				"%s: cannot load file '%s' - %s\n", Cmd,
				argv[1], strerror(errno));
			return 2;
		}
		break;
	default:
		fprintf(stderr,
			"%s: too many files given for --diff\n", Cmd);
		return 2;
	}
	if (reverse) {
		f = flist[1];
		flist[1] = flist[2];
		flist[2] = f;
	}
	fl[0] = split_stream(flist[0], obj);
	fl[1] = split_stream(flist[1], obj);
	if (chunks2 && !chunks1)
		csl = pdiff(fl[0], fl[1], chunks2);
	else
		csl = diff_patch(fl[0], fl[1]);
	if ((obj & ByMask) == ByLine) {
		if (!chunks1)
			printf("@@ -1,%d +1,%d @@\n",
			       fl[0].elcnt, fl[1].elcnt);
		exit_status = do_diff_lines(fl, csl);
	} else {
		if (!chunks1) {
			/* count lines in each file */
			int l1, l2, i;
			l1 = l2 = 0;
			for (i = 0 ; i < fl[0].elcnt ; i++)
				if (ends_line(fl[0].list[i]))
					l1++;
			for (i = 0 ; i < fl[1].elcnt ; i++)
				if (ends_line(fl[1].list[i]))
					l2++;
			printf("@@ -1,%d +1,%d @@\n", l1, l2);
		}
		exit_status = do_diff_words(fl, csl);
	}
	return exit_status;
}

static int do_merge(int argc, char *argv[], int obj, int blanks,
		    int reverse, int replace, int ignore, int show_wiggles,
		    int quiet)
{
	/* merge three files, A B C, so changed between B and C get made to A
	 */
	struct stream f, flist[3];
	struct file fl[3];
	int i;
	int chunks1 = 0, chunks2 = 0, chunks3 = 0;
	char *replacename = NULL, *orignew = NULL;
	struct csl *csl1, *csl2;
	struct ci ci;
	FILE *outfile = stdout;

	switch (argc) {
	case 0:
		fprintf(stderr, "%s: no files given for --merge\n", Cmd);
		return 2;
	case 3:
	case 2:
	case 1:
		for (i = 0; i < argc; i++) {
			flist[i] = load_file(argv[i]);
			if (flist[i].body == NULL) {
				fprintf(stderr, "%s: cannot load file '%s' - %s\n",
					Cmd,
					argv[i], strerror(errno));
				return 2;
			}
		}
		break;
	default:
		fprintf(stderr, "%s: too many files given for --merge\n",
			Cmd);
		return 2;
	}
	switch (argc) {
	case 1: /* a merge file */
		f = flist[0];
		if (!split_merge(f, &flist[0], &flist[1], &flist[2])) {
			fprintf(stderr, "%s: merge file %s looks bad.\n",
				Cmd,
				argv[0]);
			return 2;
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
		f = flist[1];
		flist[1] = flist[2];
		flist[2] = f;
	}

	for (i = 0; i < 3; i++) {
		if (flist[i].body == NULL) {
			fprintf(stderr, "%s: file %d missing\n", Cmd, i);
			return 2;
		}
	}
	if (replace) {
		int fd;
		replacename = xmalloc(strlen(argv[0]) + 20);
		orignew = xmalloc(strlen(argv[0]) + 20);
		strcpy(replacename, argv[0]);
		strcpy(orignew, argv[0]);
		strcat(orignew, ".porig");
		if (open(orignew, O_RDONLY) >= 0 ||
		    errno != ENOENT) {
			fprintf(stderr, "%s: %s already exists\n",
				Cmd,
				orignew);
			return 2;
		}
		strcat(replacename, "XXXXXX");
		fd = mkstemp(replacename);
		if (fd == -1) {
			fprintf(stderr,
				"%s: could not create temporary file for %s\n",
				Cmd,
				replacename);
			return 2;
		}
		outfile = fdopen(fd, "w");
	}

	if (obj == 'l')
		blanks |= ByLine;
	else
		blanks |= ByWord;
	fl[0] = split_stream(flist[0], blanks);
	fl[1] = split_stream(flist[1], blanks);
	fl[2] = split_stream(flist[2], blanks);

	if (chunks2 && !chunks1)
		csl1 = pdiff(fl[0], fl[1], chunks2);
	else
		csl1 = diff(fl[0], fl[1]);
	csl2 = diff_patch(fl[1], fl[2]);

	ci = make_merger(fl[0], fl[1], fl[2], csl1, csl2,
			 obj == 'w', ignore, show_wiggles);
	print_merge(outfile, &fl[0], &fl[1], &fl[2],
		    obj == 'w', ci.merger);
	if (!quiet && ci.conflicts)
		fprintf(stderr,
			"%d unresolved conflict%s found\n",
			ci.conflicts,
			ci.conflicts == 1 ? "" : "s");
	if (!quiet && ci.ignored)
		fprintf(stderr,
			"%d already-applied change%s ignored\n",
			ci.ignored,
			ci.ignored == 1 ? "" : "s");

	if (replace) {
		fclose(outfile);
		if (rename(argv[0], orignew) == 0 &&
		    rename(replacename, argv[0]) == 0)
			/* all ok */;
		else {
			fprintf(stderr,
				"%s: failed to move new file into place.\n",
				Cmd);
			return 2;
		}
	}
	return (ci.conflicts > 0);
}

static int multi_merge(int argc, char *argv[], int obj, int blanks,
		       int reverse, int ignore, int show_wiggles,
		       int replace, int strip,
		       int quiet)
{
	FILE *f;
	char *filename;
	struct plist *pl;
	int num_patches;
	int rv = 0;
	int i;

	if (!replace) {
		fprintf(stderr,
			"%s: -p in merge mode requires -r\n",
			Cmd);
		return 2;
	}
	if (argc != 1) {
		fprintf(stderr,
			"%s: -p in merge mode requires exactly one file\n",
			Cmd);
		return 2;
	}
	filename = argv[0];
	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "%s: cannot open %s\n",
			Cmd, filename);
		return 2;
	}
	check_dir(filename, fileno(f));
	pl = parse_patch(f, NULL, &num_patches);
	fclose(f);
	if (set_prefix(pl, num_patches, strip) == 0) {
		fprintf(stderr, "%s: aborting\n", Cmd);
		return 2;
	}
	for (i = 0; i < num_patches; i++) {
		char *name;
		char *av[2];
		asprintf(&name, "_wiggle_:%d:%d:%s",
			 pl[i].start, pl[i].end, filename);
		av[0] = pl[i].file;
		av[1] = name;
		rv |= do_merge(2, av, obj, blanks, reverse, 1, ignore,
			 show_wiggles, quiet);
	}
	return rv;
}

int main(int argc, char *argv[])
{
	int opt;
	int option_index;
	int mode = 0;
	int obj = 0;
	int replace = 0;
	int which = 0;
	int ispatch = 0;
	int reverse = 0;
	int verbose = 0, quiet = 0;
	int strip = -1;
	int exit_status = 0;
	int ignore = 1;
	int show_wiggles = 0;
	char *helpmsg;
	char *trace;
	int selftest = 0;
	int ignore_blanks = 0;

	trace = getenv("WIGGLE_TRACE");
	if (trace && *trace)
		do_trace = 1;

	while ((opt = getopt_long(argc, argv,
				  short_options, long_options,
				  &option_index)) != -1)
		switch (opt) {
		case 'h':
			helpmsg = Help;
			switch (mode) {
			case 'x':
				helpmsg = HelpExtract;
				break;
			case 'd':
				helpmsg = HelpDiff;
				break;
			case 'm':
				helpmsg = HelpMerge;
				break;
			case 'B':
				helpmsg = HelpBrowse;
				break;
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
			if (mode == 0) {
				mode = opt;
				continue;
			}
			fprintf(stderr,
				"%s: mode is '%c' - cannot set to '%c'\n",
				Cmd, mode, opt);
			exit(2);

		case 'w':
		case 'l':
			if (obj == 0 || obj == opt) {
				obj = opt;
				continue;
			}
			fprintf(stderr,
				"%s: cannot select both words and lines.\n", Cmd);
			exit(2);

		case 'r':
			replace = 1;
			continue;
		case 'R':
			reverse = 1;
			continue;

		case 'b':
			ignore_blanks = IgnoreBlanks;
			continue;

		case 'i':
			ignore = 0;
			continue;
		case 'W':
			show_wiggles = 1;
			ignore = 0;
			continue;

		case '1':
		case '2':
		case '3':
			if (which == 0 || which == opt) {
				which = opt;
				continue;
			}
			fprintf(stderr,
				"%s: can only select one of -1, -2, -3\n", Cmd);
			exit(2);

		case 'p': /* 'patch' or 'strip' */
			if (optarg)
				strip = atol(optarg);
			ispatch = 1;
			continue;

		case 'v':
			verbose++;
			continue;
		case 'q':
			quiet = 1;
			continue;

		case SELF_TEST:
			selftest = 1;
			continue;
		}
	if (!mode)
		mode = 'm';

	if (mode == 'B') {
		vpatch(argc-optind, argv+optind, ispatch,
		       strip, reverse, replace, selftest,
		       ignore_blanks);
		/* should not return */
		exit(1);
	}

	if (obj && mode == 'x') {
		fprintf(stderr,
			"%s: cannot specify --line or --word with --extract\n",
			Cmd);
		exit(2);
	}
	if (mode != 'm' && !obj)
		obj = 'w';
	if (replace && mode != 'm') {
		fprintf(stderr,
			"%s: --replace only allowed with --merge\n", Cmd);
		exit(2);
	}
	if (mode == 'x' && !which) {
		fprintf(stderr,
			"%s: must specify -1, -2 or -3 with --extract\n", Cmd);
		exit(2);
	}
	if (mode != 'x' && mode != 'd' && which) {
		fprintf(stderr,
			"%s: -1, -2 or -3 only allowed with --extract or --diff\n",
			Cmd);
		exit(2);
	}

	if (ispatch && which == '3') {
		fprintf(stderr,
			"%s: cannot extract -3 from a patch.\n", Cmd);
		exit(2);
	}

	switch (mode) {
	case 'x':
		exit_status = extract(argc-optind, argv+optind, ispatch, which);
		break;
	case 'd':
		exit_status = do_diff(argc-optind, argv+optind,
				      (obj == 'l' ? ByLine : ByWord)
				      | ignore_blanks,
				      ispatch, which, reverse);
		break;
	case 'm':
		if (ispatch)
			exit_status = multi_merge(argc-optind,
						  argv+optind, obj,
						  ignore_blanks,
						  reverse, ignore,
						  show_wiggles,
						  replace, strip,
						  quiet);
		else
			exit_status = do_merge(
				argc-optind, argv+optind,
				obj, ignore_blanks, reverse, replace,
				ignore, show_wiggles, quiet);
		break;
	}
	exit(exit_status);
}
