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
 * Options and help text for wiggle
 */

#include "wiggle.h"

char Version[] = "wiggle 0.8 2010-03-24 GPL-2+ http://neil.brown.name/wiggle/\n";

char short_options1[] = "xdmwlrhi123pVRvqB"; /* not mode B */
char short_options2[] = "xdmwlrhi123p::VRvqB"; /* mode B */


struct option long_options[] = {
	{"browse",	0, 0, 'B'},
	{"extract",	0, 0, 'x'},
	{"diff",	0, 0, 'd'},
	{"merge",	0, 0, 'm'},
	{"words",	0, 0, 'w'},
	{"lines",	0, 0, 'l'},
	{"patch",	0, 0, 'p'},
	{"replace",	0, 0, 'r'},
	{"help",	0, 0, 'h'},
	{"version",	0, 0, 'V'},
	{"reverse",	0, 0, 'R'},
	{"verbose",	0, 0, 'v'},
	{"quiet",	0, 0, 'q'},
	{"strip",	1, 0, 'p'},
	{"no-ignore",	0, 0, 'i'},
	{0, 0, 0, 0}
};

char Usage[] =
"Usage: wiggle --diff|--extract|--merge|--browse --lines|--words [--replace] files...\n";

char Help[] =  "\n"
"Wiggle - apply patches that 'patch' rejects.\n"
"\n"
"Wiggle provides four distinct but related functions:\n"
"merge, diff, extract, and browse.\n"
"To get more detailed help on a function, select the function\n"
"before requesting help.  e.g.\n"
"    wiggle --diff --help\n"
"\n"
"Options:\n"
"   --extract   -x    : select 'extract' function.\n"
"   --diff      -d    : select 'diff' function.\n"
"   --merge     -m    : select 'merge' function (default).\n"
"   --browse    -B    : select 'browse' function.\n"
"\n"
"   --words     -w    : word-wise diff and merge.\n"
"   --lines     -l    : line-wise diff and merge.\n"
"\n"
"   --patch     -p    : treat last file as a patch file.\n"
"   -1  -2  -3        : select which component of patch or merge to use.\n"
"   --reverse   -R    : swap 'before' and 'after' for diff function.\n"
"\n"
"   --help      -h    : get help.\n"
"   --version   -V    : get version of wiggle.\n"
"   --verbose   -v    : (potentially) be more verbose.\n"
"   --quiet     -q    : don't print un-necessary messages.\n"
"\n"
"   --replace   -r    : replace first file with result of merger.\n"
"\n"
"   --strip=    -p    : number of path components to strip from file names.\n"
"\n"
"Wiggle needs to be given 1, 2, or 3 files.  Any one of these can\n"
"be given as '-' to signify standard input.\n"
"\n";

char HelpExtract[] = "\n"
"wiggle --extract -[123] [--patch]  merge-or-patch\n"
"\n"
"The extract function allows one branch of a patch or merge file\n"
"to be extracted.  A 'patch' is the output of 'diff -c' or 'diff -u'.\n"
"Either the before (-1) or after (-2) branch can be extracted.\n"
"\n"
"A 'merge' is the output of 'diff3 -m' or 'merge -A'.  Either the\n"
"first, second, or third branch can be extracted.\n"
"\n"
"A 'merge' file is assumed unless --patch is given.\n"
"\n";

char HelpDiff[] = "\n"
"wiggle --diff [-wl] [-p12] [-R]  file-or-patch [file-or-patch]\n"
"\n"
"The diff function will report the differencs and similarities between\n"
"two files in a format similar to 'diff -u'.  With --word mode\n"
"(the default) word-wise differences are displayed on lines starting\n"
"with a '|'.  With --line mode, only whole lines are considered\n"
"much like normal diff.\n"
"\n"
"If one file is given is it assumed to be a patch, and the two\n"
"branches of the patch are extracted and compared.  If two files\n"
"are given they are normally assumed to be whole files and are compared.\n"
"However if the --patch option is given with two files, then the\n"
"second is treated as a patch and the first or (with -2) second branch\n"
"is extracted and compared against the first file.\n"
"\n"
"--reverse (-R) with cause diff to swap the two files before comparing\n"
"them.\n"
"\n";

char HelpMerge[] = "\n"
"wiggle --merge [-wl] [--replace]  file-or-merge [file-or-patch [file]]\n"
"\n"
"The merge function is the primary function of wiggle and is assumed\n"
"if no function is explicitly chosen.\n"
"\n"
"Normally wiggle will compare three files on a word-by-word basis and\n"
"output unresolvable conflicts in the resulting merge by showing\n"
"whole-line differences.\n"
"With the --lines option, the files are compared line-wise much\n"
"like 'merge'.  With the (default) --words option, files are compared\n"
"word-wise and unresolvable conflicts are reported word-wise.\n"
"\n"
"If --merge is given one file, it is treated as a merge (merge -A\n"
"output) and the three needed streams are extracted from it.\n"
"If --merge is given two files, the second is treated as a patch\n"
"file and the first is the original file.\n"
"If --merge is given three files, they are each treated as whole files\n"
"and differences between the second and third are merged into the first.\n"
"This usage is much like 'merge'.\n"
"\n";

char HelpBrowse[] = "\n"
"wiggle --browse [-R] [--strip=n] [-p] [files]\n"
"\n"
"The 'browse' function provides an interactive mode for browsing a\n"
"patch or set of patches.  It allows the application of a patch to each\n"
"file to be inspected and will eventually allow limited editing to correct\n"
"mis-application of patches where wiggling was required, and where conflicts\n"
"occurred.\n"
"If no files are given, a patch file is read from stdin\n"
"If one file is given with -p, it is treated as a patch file\n"
"If one file is given with a name ending .rej, it is treated as a reject for\n"
"     a file with matching basename\n"
"Otherwise a single file is assumed to be a merge output with conflicts.\n"
"If two files are given, the second is a patch to apply to the first.\n"
"If three files are given then the difference between 2nd and 3rd is applied\n"
"     to the first\n"
"\n";
