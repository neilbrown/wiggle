#!/bin/bash

# patch management
#
# Copyright (C) 2003 Neil Brown <neilb@cse.unsw.edu.au>
#
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
#    Author: Neil Brown
#    Email: <neilb@cse.unsw.edu.au>
#    Paper: Neil Brown
#           School of Computer Science and Engineering
#           The University of New South Wales
#           Sydney, 2052
#           Australia


# metadata is in .patches
# there is:
#   files:  list of all files checked out
#   name:   name of current patch
#   status: status of current patch
#   notes:  notes on current patch
#   applied/  patches applied nnn-name
#   removed/  patches removed nnn-name
#   included/ patches that have been included upstream
#   patch:  a recent copy of the 'current' patch
#   get-version: a script which will report the version number of the base dist
#   dest/   symlink to directory to publish snapshots to
#   mail/   composed mail messages ready for sending
#   maintainer  who to email patches to (Linus etc)
#   cc        who to CC patches to: prefix address
#
# the nnn in names in applied and removed are sequence numbers
# whenever we add a file we choose one more than the highest used number
# patch files contain then name implicitly and start with
#   Status: status
# then a blank line, normally a one line description, another blank, and more detail.
#

#
# Todo - auto bk pull:
#    bk pull
#    bk export -t patch -r DEVEL, > /tmp/apatch
#    bk tag DEVEL
#    while p open last && p discard ; do : ; done
#    p clean
#    patch -p1 -f < /tmp/apatch

find_home()
{
	# walk up directory tree until a .patches directory
	# is found.
	# set OrigDir to name of where we were .. not dots.
	OrigDir=
	dir=`pwd`
	while [ ! -d .patches -a " $dir"  != " /" ]
	do
		base=${dir##*/}
		base=${base#/}
		dir=${dir%/*}
		case $dir in
			"" ) dir=/
		esac
		OrigDir=$base/$OrigDir
		cd ..
	done
	test -d .patches
}

get_meta()
{
	name=`cat .patches/name 2> /dev/null`
	status=`cat .patches/status 2> /dev/null`
}

nl='
'
get_conf()
{
	_name=$1
	_context=$2
	_result=
	_active=yes
	_sep=
	[ -f .patches/config ] || >> .patches/config
	while read a b c
	do
	    case $a in
		    '[global]' ) _active=yes ;;
		    "[$_context]") _active=yes ;;
		    "["*"]" ) _active= ;;
		    * ) if [ " $b" == " =" -a " $a" = " $_name" -a -n "$_active" ];
			    then
			    _result="$_result$_sep$c"
			    _sep=$nl
			    fi
			    ;;
	    esac
	done < .patches/config
	_result=$(echo "$_result" | sed 's/^"//' )
	eval $_name=\"\$_result\"
}

upgrade_one()
{
	# move $1~current~ to .patches/current/$1 and same for orig
	fl=/$1
	for f in current orig
	do
	    if [ -f "$1~$f~" ]
		then
		    mkdir -p ".patches/$f${fl%/*}"
		    mv "$1~$f~" ".patches/$f/$1"
	    fi
	done
}


forget_one()
{
	if true # || cmp -s "$1" ".patches/curent/$1~" && cmp -s "$1" ".patches/orgi/$1"
	then
            rm -f "./patches/current/$1" ".patches/orig/$1"
	    chmod -w "$1"
	else
	    echo >&2 "ERROR $1 doesn't match original"
	fi
}

rebase_one()
{
    f="/$1"
    mkdir -p .patches/orig${f%/*}
    mkdir -p .patches/current${f%/*}
    rm -f .patches/orig$f .patches/current$f
    cp -p $1 .patches/orig$f
    cp -p $1 .patches/current$f
}

snap_one()
{
    cp "$1" "$1~snapshot~"
}

snap_diff()
{
    diff -u "$1" "$1~snapshot~"
}
snap_back()
{
    cp "$1~snapshot~" "$1"
}

check_out()
{
	file=$1
	file=${file#./}
	[ -f $file ] || >> $file
	if [ -f $file ]
	    then
	    if [ ! -f ".patches/orig/$file" ] ; then
		mv "$file" ".patches/orig/$file"
		cp ".patches/orig/$file" "$file"
		echo $file >> .patches/files
		sort -o .patches/files .patches/files
		chmod u+w "$file"
	    fi
	    if [ ! -f ".patches/current/$file" ] ; then
		mv "$file" ".patches/current/$file"
		cp ".patches/current/$file" "$file"
	    fi
	else
	    echo >&2 Cannot checkout $file
	fi
}

all_files()
{
        >> .patches/files
	while read file
	do eval $1 $file $2
	done < .patches/files
}

diff_one()
{
	if cmp -s ".patches/current/$1" "$1" || [ ! -f "$1" -a ! -f ".patches/current/$1" ]
	then :
	else
		echo
		echo "diff .prev/$1 ./$1"
		if [ " $2" = " -R" ]
		then
		  diff -N --show-c-function -u "./$1" "./.patches/current/$1"
		else
		  diff -N --show-c-function -u "./.patches/current/$1" "./$1"
		fi
	fi
}

diff_one_orig()
{
	if cmp -s ".patches/orig/$1" "$1"
	then :
	else
		echo
		echo "diff ./.patches/orig/$1 ./$1"
		diff --show-c-function -u "./.patches/orig/$1" "./$1"
	fi
}

commit_one()
{
    rm -f ".patches/current/$1"
    if [  -f "$1" ] ; then
	mv "$1" ".patches/current/$1"
	cp -p ".patches/current/$1" $1
	chmod u+w $1
    fi
}

discard_one()
{
	cmp -s ".patches/current/$1" $1 || { rm -f "$1" ; cp ".patches/current/$1" $1; }
	chmod u+w $1
}

swap_one()
{
	mv "$1" "$1.tmp"
	mv ".patches/current/$1" "$1"
	mv "$1.tmp" ".patches/current/$1"
}

make_diff()
{
	get_conf tagline
	upgrade_one "$1"
   { {
	[ -s .patches/status ] && echo "Status: `cat .patches/status`"
	[ -s .patches/notes ] && { echo; cat .patches/notes ; }
	if [ -z "$tagline" ] || grep -F "$tagline" .patches/notes > /dev/null 2>&1
	then :
        else echo "$tagline"
	fi
	echo
	all_files diff_one $1 > .patches/tmp
	echo "### Diffstat output"
	diffstat -p0 2> /dev/null < .patches/tmp
	cat .patches/tmp
	[ -s .patches/tmp ] || rm .patches/patch
	rm .patches/tmp
   } | sed 's,^--- ./.patches/current/,--- .prev/,' ; } > .patches/patch
}

save_patch()
{
	dir=.patches/$1
	name=$2
	# move .patches/patch to $dir/nnn$name
	#for some new nnn
	[ -d $dir ] || mkdir $dir || exit 1
	largest=`ls $dir | sed -n -e 's/^\([0-9][0-9][0-9]\).*/\1/p' | sort -n | tail -1`
	if [ "0$largest" -eq 999 ]
	then echo >&2 'ARRG - too many patches!' ; exit 1
	fi
	new=`expr "0$largest" + 1001`
	new=${new#1}
	mv .patches/patch $dir/$new$name
}

find_prefix()
{
	# set "prefix" to number for -pn by looking at first file in given patch.
	n=${2-1}
	file=`lsdiff $1 | head -$n | tail -1`
	orig=$file
	prefix=0
	while [ \( -n "$file" -a ! -f "$file" \) -o " $file" != " ${file#/}" ]
	do
	    file=`expr "$file" : '[^/]*/\(.*\)'`
	    prefix=`expr $prefix + 1`
	done
	if [ -z "$file" ]
	then echo "Cannot find $orig" >&2
	   if [ $n -gt 4 ]
	   then exit 2;
	   else find_prefix "$1" $[n+1]
	   fi
	fi
	if [ " $orig" != " $file" ]
	then
	    echo "Found $orig as $file - prefix $prefix"
	fi
}

extract_notes()
{
 # remove first line, Status: line, leading blanks,
 # everything from ' *---' and trailing blanks
 awk '
    BEGIN { head= 1; blanks=0 ; }
    head == 1 && ( $1 == "Status:" || $0 == "" ) {
	    next;
    }
    { head = 0; }
    $0 == "" { blanks++; next; }
    $0 ~ /^ *---/ { exit }
    $0 ~ /^###/ { exit }
    {   while (blanks > 0) {
	   blanks--; print "";
	}
	print $0;
    }
  ' $1
}


if [ $# -eq 0 ]
then
  echo >&2 'Usage: p [help|co|make|discard|commit|status|name|...] args'
  exit 1
fi
cmd=$1
shift

if [ " $cmd" = " help" ] || find_home
then :
else echo >&2 "p $cmd: cannot find .patches directory"
     exit 1
fi

case $cmd in
   co )
	if [ $# -ne 1 ] ; then
		echo >&2 Usage: p co file; exit 1
	fi
	file=$1
	if [ ! -f "$OrigDir$file" ]
	then
		echo >&2 "p co: file $file not found"; exit 1;
	fi
	check_out "$OrigDir$file"

	;;
  make | view )
	case $1 in
	    "" )
		make_diff
		if [ -s .patches/patch ] ; then
		    pfile=.patches/patch
		else
		    echo >&2 "No current patch" ; exit 1;
		fi
		;;

	    */* ) pfile=$1;;
	    * ) pfile=`echo .patches/[ra][ep][mp]*/*$1*`
	esac
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found: $pfile"; exit 1;
	fi
	if grep -s '^+.*[ 	]$' $pfile > /dev/null
	then
	    ${PAGER-less -p '^\+.*[ 	]$'} $pfile
	else
	    ${PAGER-less} $pfile
	fi
	;;

  all )
	all_files diff_one_orig
	;;
  status | name )
	case $# in
	 1 )
		get_meta
		if [ $cmd = name ] ; then
		  if [ -n "$name" ]; then
			echo "changing name from '$name' to '$1'"
		  else
			echo "Setting name to '$1'"
		  fi
		  echo "$1" > .patches/name
		fi
		if [ $cmd = status ] ; then
		  if [ -n "$status" ]; then
			echo "changing status from '$status' to '$1'"
		  else
			echo "Setting status to '$1'"
		  fi
		  echo "$1" > .patches/status
		fi
		;;
	  0 )
		get_meta
		echo -n "Name ($name)? " ; read name
		echo -n "Status ($status)?  " ; read status
		[ -n "$name" ] && { echo $name > .patches/name ; }
		[ -n "$status" ] && { echo $status > .patches/status ; }
		;;
	  * )
		echo "Usage: p $cmd [new-$cmd]"; exit 1;
	esac
	;;
  note* )
	>> .patches/notes
	${EDITOR:-vi} .patches/notes
	;;
  discard|commit )
	make_diff
	if [ -s .patches/patch ]
	then :
	else echo >&2 No patch to $cmd ; exit 1
	fi
	if grep -s '^+.*[ 	]$' .patches/patch > /dev/null
	then
	    echo >&2 remove trailing spaces/tabs first !!
#	    exit 1
	fi
	if [ -s .patches/to-resolve ]
	then echo "Please resolve outstanding conflicts first with 'p resolve'"
	    exit 1
	fi
	get_meta
	if [ -z "$name" ] ; then
		echo -n "Name? " ; read name
		if [ -z "$name" ] ; then
		   echo >&2 "No current name, please set with 'p name'"
		   exit 1;
		fi
		echo $name > .patches/name
	fi
	if [ -z "$status" ] ; then
		echo -n "Status? " ; read status
		if [ -z "$status" ] ; then
		    echo >&2 "No current status, please set with 'p status'"
		    exit 1;
		fi
		echo $status > .patches/status
	fi
	if [ -s .patches/notes ]
	then :
	else
	    { echo "Title...."
	      echo
	      echo "Description..."
	      echo
	      echo "====Do Not Remove===="
	      cat .patches/patch
	    } > .patches/notes
	    ${EDITOR-vi} .patches/notes
	    mv .patches/notes .patches/tmp
	    sed '/^====Do Not Remove====/,$d' .patches/tmp > .patches/notes
	    rm .patches/tmp
	fi
	make_diff
	
	if [ $cmd = commit ] ; then
	   save_patch applied "$name"
	   echo Saved as $new$name
	   all_files commit_one
	else
	   save_patch removed "$name"
	   echo Saved as $new$name
	   all_files discard_one
	fi
	rm -f .patches/name .patches/status .patches/notes
	;;

  purge )
        make_diff
	mv .patches/patch .patches/last-purge
	all_files discard_one
	rm -f .patches/name .patches/status .patches/notes
	;;
  open )
	make_diff
	get_meta
	if [ -s .patches/patch ]
	then
		echo >&2 Patch $name already open - please commit; exit 1;
	fi
	if [ $# -eq 0 ]
	then
		echo "Available patches are:"
		ls .patches/applied
		exit 0
	fi
	if [ $# -ne 1 ]
	then echo >&2 "Usage: p open patchname" ; exit 1
	fi
	if [ " $1" = " last" ]
	then
	    pfile=`ls -d .patches/applied/[0-9]* | tail -1`
        else
	    pfile=`echo .patches/applied/*$1*`
	fi
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found: $pfile"; exit 1
	fi
	# lets see if it applies cleanly
	if patch -s --fuzz=0 --dry-run -R -f -p0 < "$pfile"
	then echo Ok, it seems to apply
	else echo >&2 "Sorry, that patch doesn't apply" ; exit 1
	fi
	# lets go for it ...
	patch --fuzz=0 -R -f -p0 < "$pfile"
	all_files swap_one
	sed -n -e '2q' -e 's/^Status: *//p' $pfile > .patches/status
	base=${pfile##*/[0-9][0-9][0-9]}
	[ -s .patches/name ] || echo $base > .patches/name
	extract_notes $pfile >> .patches/notes
	mv $pfile .patches/patch

	;;
  included )
	force=
	if [ " $1" = " -f" ] ; then
	    force=yes; shift
	fi
	make_diff; get_meta
	if [ -s .patches/patch ]
	then
	    echo >&2 Patch $name already open, please commit; exit 1;
	fi
	if [ $# -eq 0 ]
	then
	    echo "Unapplied patches are:"
	    ls .patches/removed
	    exit 0;
	fi
	if [ $# -ne 1 ]
	then
	     echo >&2 "Usage: p included patchname"; exit 1
	fi
	case $1 in
	    last ) pfile=`ls -d .patches/removed/[0-9]* | tail -1` ;;
	    */* ) echo >&2 "Only local patches can have been included"; exit 1 ;;
	    *) pfile=`echo .patches/removed/*$1*`
	esac
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found $pfile"; exit 1
	fi
	echo "Using $pfile..."

	# make sure patch applies in reverse
	if patch -s --fuzz=2  -l --dry-run -f -p0 -R < "$pfile"
	then echo "Yep, that seems to be included"
	elif [ -n "$force" ]
	then echo "It doesn't apply reverse-out cleanly, but you asked for it..."
	else echo >&2 "Sorry, patch cannot be removed"; exit 1
	fi
	mv "$pfile" .patches/patch
	name=${pfile##*/[0-9][0-9][0-9]}
	save_patch included $name
	echo "Moved to $new$name"
	;;
  review )
	# there are some patches in .removed that may be included in the current source
	# we try to backout each one. If it backs out successfully, we move it to
	# .reviewed and continue, else  we abort
	# Once this has been done often enough, 'reviewed' should be run to
	# move stuff to 'included' and to revert those patches
	force=
	if [ " $1" = " -f" ] ; then
	    force=yes; shift
	fi
	make_diff; get_meta
	if [ -s .patches/patch ]
        then
	    echo >&2 Patch $name already open, please deal with it; exit 1;
	fi
	if [ -f .patches/in-review ]
	then :
	else
		applied=`ls .patches/applied`
		if [ -n "$applied" ]
		then
			echo >&2 Cannot review patches while any are applied.
			exit 1;
		fi
		> .patches/in-review
	fi
	if [ $# -eq 0 ]
	then
	    echo "Pending patches are:"
	    ls .patches/removed
	    exit 0;
	fi
	if [ $# -ne 1 ]
	then
	     echo >&2 "Usage: p review patchname"; exit 1
	fi
	case $1 in
	    */* ) echo >&2 "Only local patches can have been included"; exit 1 ;;
	    *) pfile=`echo .patches/removed/*$1*`
	esac
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found $pfile"; exit 1
	fi
	echo "Starting from $pfile..."
	found=
	for fl in .patches/removed/*
	do
	  if [ " $fl" = " $pfile" ]; then found=yes ; fi
          if [ -n "$found" ]; then
	      echo Checking $fl
	      find_prefix "$fl"
	      lsdiff --strip=$prefix "$fl" | grep -v 'file.*changed' | while read a b
	      do check_out $a
	      done
	      if patch -s --fuzz=0 --dry-run -f -p$prefix -R < "$fl"
	      then echo Looks good..
	      elif [ -n "$force" ]
	      then echo "It doesn't backout cleanly, but you asked for it..."
		  cp $fl .patches/last-backed
	      else echo "Patch won't back out, sorry"
		  exit 1
	      fi
	      patch --fuzz=0 -f -p$prefix -R < "$fl" | tee .patches/tmp
	      sed -n -e '2q' -e 's/^Status: *//p' $fl > .patches/status
	      base=${fl##*/}
	      base=${base##[0-9][0-9][0-9]}
	      base=${base##patch-?-}
	      [ -s .patches/name ] || echo $base > .patches/name
	      extract_notes $fl >> .patches/notes
	      rm -f .patches/wiggled
	sed -n -e 's/.*saving rejects to file \(.*\).rej/\1/p' .patches/tmp |
	while read file
	do  echo Wiggling $file.rej into place
	    rm -f $file.porig
	    > .patches/wiggled
	    wiggle --replace --merge $file $file.rej ||
	    echo $file >> .patches/to-resolve
	done

	      mv $fl .patches/patch
	      save_patch reviewed $base
	      if [ -f .patches/wiggled ]
	      then echo 'Some wiggling was needed. Please review and commit'
		  exit 0
	      fi
	      p commit || exit 1
	  fi
        done
	;;

  reviewed )
	      # all the currently applied patches are patches that have been
	      # reviewed as included.
	      # rip them out and stick them (reversed) into included.
	      if [ ! -f .patches/in-review ]
		  then
		      echo >&2 Not currently reviewing patches!
		      exit 1;
	      fi
	      while p open last
	      do
		make_diff -R
		get_meta
		save_patch included "$name"
		echo Saved as "$new$name"
		all_files discard_one
		rm -f .patches/name .patches/status .patches/notes
	      done
	      rm .patches/in-review
	      ;;
  list )
	    echo "Applied patches are:"
	    ls .patches/applied

	    echo "Unapplied patches are:"
	    ls .patches/removed
	    exit 0
	    ;;
 apply )
	if [ -f .patches/in-review ]
	then
		echo >&2 Cannot apply patches while reviewing other - use p reviewed
		exit 1
	fi
	force= append=
	if [ " $1" = " -f" ]; then
	    force=yes; shift
	fi
	if [ " $1" = " -a" ]; then
	    append=yes; shift
	fi
	make_diff
	get_meta
	if [ -s .patches/patch -a -z "$append" ]
	then
	    echo >&2 Patch $name already open - please commit ; exit 1;
	fi
	if [ $# -eq 0 ]
	then
	    echo "Unapplied patches are:"
	    ls .patches/removed
	    exit 0
	fi
	if [ $# -ne 1 ]
	then echo >&2 "Usage: p apply patchname"; exit 1
	fi
	case $1 in
	    last ) pfile=`ls -d .patches/removed/[0-9]* | tail -1` ; echo last is "$pfile";;
	    */* ) pfile=$1 ;;
	    * ) pfile=`echo .patches/removed/*$1*`
	esac
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found: $pfile"; exit 1
	fi
	find_prefix "$pfile"
	lsdiff --strip=$prefix "$pfile" | grep -v 'file.*changed' | while read a b
	do check_out $a
	done
	# lets see if it applies cleanly
	if patch -s --fuzz=0 --dry-run -f -p$prefix < "$pfile"
	then echo OK, it seems to apply
	elif [ -n "$force" ]
	then echo "It doesn't apply cleanly, but you asked for it...."
	    echo "Saving original at .patches/last-conflict"
	    cp $pfile .patches/last-conflict
	else echo >&2 "Sorry, patch doesn't apply"; exit 1
	fi
	# lets go for it ...
	cp $pfile .patches/last-applied
	patch --fuzz=0 -f -p$prefix < "$pfile" | tee .patches/tmp
	sed -n -e '2q' -e 's/^Status: *//p' $pfile > .patches/status
	base=${pfile##*/}
	base=${base##[0-9][0-9][0-9]}
	base=${base##patch-?-}
	[ -s .patches/name ] || echo $base > .patches/name
	extract_notes $pfile >> .patches/notes

	sed -n -e 's/.*saving rejects to file \(.*\).rej/\1/p' .patches/tmp |
	while read file
	do  echo Wiggling $file.rej into place
	    rm -f $file.porig
	    wiggle --replace --merge $file $file.rej ||
	    echo $file >> .patches/to-resolve
	done

	case $pfile in
	    .patches/removed/* )
		mv $pfile .patches/patch
        esac
	;;

  unapply )
	get_meta
	mv .patches/last-applied .patches/patch
	save_patch removed $name
	echo Restored to $new$name
	make_diff
	mv .patches/patch .patches/last-purge
	all_files discard_one
	rm -f .patches/name .patches/status .patches/notes
	;;
  publish )
	name=`date -u +%Y-%m-%d-%H`
	if [ -d .patches/dest ]
	then : good
	else echo >&2 No destination specified at .patches/dest ; exit 1;
	fi
	if [ -d .patches/dest/$name ]
	then
	    echo >&2 $name already exists ; exit 1
	fi
	target=.patches/dest/$name
	mkdir $target
	if [ -f .patches/get-version ] ;
	then ./.patches/get-version > $target/version
	fi
	[ -f .config ] && cp .config $target
	cp .patches/applied/* $target
	mkdir $target/misc
	cp 2> /dev/null .patches/removed/* $target/misc || rmdir $target/misc
	chmod -R a+rX $target
	all_files diff_one_orig > $target/patch-all-$name
	cd $target
	echo Published at `/bin/pwd`
	;;
  clean )
	all_files forget_one
	> .patches/files
	;;
  openall )
        while $0 open last && $0 discard ; do : ; done
	;;
  recommit )
	make_diff
	get_meta
	if [ -s .patches/patch ]
	then
	    echo >&2 Patch $name already open - please commit ; exit 1;
	fi
	if [ $# -eq 0 ]
	then
	    echo "Unapplied patches are:"
	    ls .patches/removed
	    exit 0
	fi
	if [ $# -ne 1 ]
	then echo >&2 "Usage: p recommit patchname"; exit 1
	fi
	case $1 in
	    last ) pfile=`ls -d .patches/removed/[0-9]* | tail -1` ; echo last is "$pfile";;
	    */* ) pfile=$1 ;;
	    * ) pfile=`echo .patches/removed/*$1*`
	esac
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found: $pfile"; exit 1
	fi
	while [ -s "$pfile" ]  &&
		$0 apply last && $0 commit ; do : ; done
	;;
  decommit )
	make_diff
	get_meta
	if [ -s .patches/patch ]
	then
	    echo >&2 Patch $name already open - please commit ; exit 1;
	fi
	if [ $# -eq 0 ]
	then
	    echo "Applied patches are:"
	    ls .patches/applied
	    exit 0
	fi
	if [ $# -ne 1 ]
	then echo >&2 "Usage: p decommit patchname"; exit 1
	fi
	case $1 in
	    last ) pfile=`ls -d .patches/applied/[0-9]* | tail -1` ; echo last is "$pfile";;
	    */* ) pfile=$1 ;;
	    * ) pfile=`echo .patches/applied/*$1*`
	esac
	if [ ! -f "$pfile" ]
	then echo >&2 "Cannot find unique patch '$1' - found: $pfile"; exit 1
	fi
	while [ -s "$pfile" ]  &&
	     $0 open last && $0 discard ; do : ; done
	;;

  rebase )
	# move all applied patches to included, and
	# copy current to orig and current
	make_diff
	if [ -s .patches/patch ]
	then
	    echo >&2 Patch already open - please commit; eixt 1;
	fi
	for p in `ls .patches/applied`
	do
	  name=${p##[0-9][0-9][0-9]}
	  mv .patches/applied/$p .patches/patch
	  save_patch included $name
	done
	all_files rebase_one
	;;
  snapshot )
	all_files snap_one
	;;
  snapdiff )
	all_files snap_diff
	;;
  snapback )
	all_files snap_back
	;;
  upgrade )
	all_files upgrade_one
	;;
  resolve )
        if [ ! -s .patches/resolving ]
	then sort -u .patches/to-resolve > .patches/resolving ; > .patches/to-resolve
        fi
        if [ ! -s .patches/resolving ]
	then echo "Nothing to resolve" ; exit 0;
	fi
	echo "Resolving: " ; cat .patches/resolving
	for file in `cat .patches/resolving`
	do
	  ${EDITOR:-vi} $file
	  rm -f $file.porig
	  wiggle --replace --merge $file ||
	    echo $file >> .patches/to-resolve
	done
	> .patches/resolving
	;;

  export )
	# there must be only one patch.  We
	# git commit, p commit, p rebase
	if [ -n "`ls .patches/applied`" ]
	then
	    echo 'Cannot export when there are applied patches'
	    exit 1;
	fi
	make_diff
	if [ -s .patches/patch ]
	then
	    # Ok, go for it.
	    git add `cat .patches/files`
	    git commit -a -F .patches/notes
	    $0 commit
	    $0 rebase
	fi
	;;
  pull )
        cd .patches/SOURCE && bk pull
	;;
  update )
	make_diff
	get_meta
	if [ -s .patches/patch ]
	then
		echo >&2 Patch $name already open - please commit; exit 1;
	fi
        p openall && p clean &&
	  (cd .patches/SOURCE ; bk export -tpatch -rLATEST, ) > .patches/imported-patch &&
	  patch --dry-run -f -p1 < .patches/imported-patch &&
	  patch -f -p1 < .patches/imported-patch &&
	  ( rm .patches/imported-patch ; cd .patches/SOURCE ; bk tag LATEST )
	;;

  premail )
  	# Convert some applied patches into email messages.
  	# Select patches that start with $1. Look in .patches/cc for who to Cc: to
  	rmdir .patches/mail 2>/dev/null
	if [ -d .patches/mail ] ; then
	    echo >&2 There is already some email - run "email" or "nomail"
	    ls .patches/mail
	    exit 1;
	fi
	mkdir .patches/mail

	get_conf author $1
	get_conf header $1
	if [ -n "$author" ]
	then
		headers="From: $author$nl$header$nl"
	elif [ -s .patches/owner ]; then
		headers=`cat .patches/owner`;
        else
		echo Please add add auther information to .patches/config
		exit 1
	fi
	get_conf maintainer $1
	if [ -z "$maintainer" -a -s .patches/maintainer ]
	    then
		mantainer=`cat .patches/maintainer`
	fi

	if [ -z "$maintainer" ] ; then
	    echo "No maintainer - please add one"
	    exit 1;
	fi

	messid="<`date +'%Y%m%d%H%M%S'`.$$.patches@`uname -n`>"
	cnt=0
	for patch in .patches/applied/???${1}*
	do
          n=${patch##*/}
	  n=${n:0:3}
	  if [ -n "$2" ] && [ $2 -gt $n ] ; then continue; fi
	  if [ -n "$3" ] && [ $3 -lt $n ] ; then continue; fi
	  cnt=$(expr $cnt + 1 )
	done
	get_conf cc $1
	get_conf tag $1
	this=1
	if [ $cnt -gt 1 ]
	then
	{
	    echo "$headers"
	    echo "To: $maintainer"

	    if [ -z "$cc" ]; then
		    echo "Cc: $cc"
	    fi
	    if [ -z "$tag" ]; then
		    sprefix="$tag: "
	    fi
	    if [ -s .patches/cc ] ; then
		while read word prefix addr
		  do if [ " $word" = " $1" ] ; then
			echo "Cc: $addr"
			sprefix="$prefix: "
		    fi
		  done < .patches/cc
	    fi
	    if [ $cnt = 1 ]
		  then
		  echo "Subject: [PATCH] ${sprefix}Intro"
		  else
		  echo "Subject: [PATCH 000 of $cnt] ${sprefix}Introduction"
	    fi
	    echo "Message-ID: $messid"
	    echo
	    echo PUT COMMENTS HERE
	} > .patches/mail/000Intro
	fi

	for patch in .patches/applied/???${1}*
	do
          n=${patch##*/}
	  n=${n:0:3}
	  if [ -n "$2" ] && [ $2 -gt $n ] ; then continue; fi
	  if [ -n "$3" ] && [ $3 -lt $n ] ; then continue; fi
	  {
	      sprefix=
	      echo "$headers"
	      echo "To: $maintainer"
	      if [ -z "$cc" ]; then
		    echo "Cc: $cc"
	      fi
	      if [ -z "$tag" ]; then
		    sprefix="$tag: "
	      fi
	      if [ -s .patches/cc ] ; then
		  while read word prefix addr
		    do if [ " $word" = " $1" ] ; then
			echo "Cc: $addr"
			sprefix="$prefix: "
		    fi
		  done < .patches/cc
	      fi
	      head=`sed -e '/^Status/d' -e '/^$/d' -e q $patch`
	      zerothis=$(expr $this + 1000)
	      if [ $cnt = 1 ]
		  then
		  echo "Subject: [PATCH] $sprefix$head"
		  else
		  echo "Subject: [PATCH ${zerothis#1} of $cnt] $sprefix$head"
	      fi
	      echo "References: $messid"
	      echo
	      if [ $cnt = 1 ] ; then
		  echo "### Comments for Changeset"
	      fi
	      sed -e '1,3d' $patch
	  } > .patches/mail/${patch#.patches/applied/}
	  this=$(expr $this + 1)
	done
	if [ -f .patches/mail/000Intro ]; then cat .patches/mail/* | sed -n -e 's/^Subject://p'  >> .patches/mail/000Intro ; fi
	ls .patches/mail
	;;

    nomail )
	  echo "Removing .patches/mail directory"
	  rm -rf .patches/mail
	  ;;

     email )
     	PATH=$HOME/bin:/usr/lib:/usr/sbin:$PATH
          for i in .patches/mail/*
	  do
	    if [ -f "$i" ]
		then
		echo Sending $i.
		sendmail -t < $i && rm $i
	    fi
	  done
	  ;;
   help )
	helpfile=$0.help
	if [ ! -f $helpfile ]
	then echo >&2 $helpfile not found: no help available ; exit 2;
	fi
        if [ -z "$1" ] ; then
	   echo
	   sed -n -e '/^ /p' -e '/^[^ ]/q' $helpfile
	   echo
	   echo "Available help topics are:"
	   sed -n '/^[^ ]/p' $helpfile | sort | column
	else
	    echo
	    awk '$0 ~ /^[^ ]/ && printed {doprint=0; printed=0}
      		doprint && $0 !~ /^[^ ]/ {print; printed=1;}
		$0 == "'$1'" {doprint=1; found=1}
		END { if (!found) print "No help available for '$1'"; }
		' $helpfile
	    echo
        fi
	;;
  * )
	echo >&2 "p $cmd - unknown command - try 'p help'"; exit 1;
esac
exit 0;
