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

forget_one()
{
	if cmp -s "$1" "$1~current~" && cmp -s "$1" "$1~orig~"
	then
            rm -f "$1~current~" "$1~orig~"
	    chmod -w "$1"
	else
	    echo >&2 "ERROR $1 doesn't match original"
	fi
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
	    if [ ! -f "$file~orig~" ] ; then
		mv "$file" "$file~orig~"
		cp "$file~orig~" "$file"
		echo $file >> .patches/files
		sort -o .patches/files .patches/files
		chmod u+w "$file"
	    fi
	    if [ ! -f "$file~current~" ] ; then
		mv "$file" "$file~current~"
		cp "$file~current~" "$file"
	    fi
	else
	    echo >&2 Cannot checkout $file
	fi
}

all_files()
{
        >> .patches/files
	while read file
	do eval $1 $file
	done < .patches/files
}

diff_one()
{
	if cmp -s "$1~current~" "$1"
	then :
	else
		echo
		echo "diff ./$1~current~ ./$1"
		diff --show-c-function -u ./$1~current~ ./$1
	fi
}

diff_one_orig()
{
	if cmp -s "$1~orig~" "$1"
	then :
	else
		echo
		echo "diff ./$1~orig~ ./$1"
		diff --show-c-function -u ./$1~orig~ ./$1
	fi
}

commit_one()
{
	rm -f "$1~current~"
	mv "$1" "$1~current~"
	cp "$1~current~" $1
	chmod u+w $1
}

discard_one()
{
	rm -f "$1"
	cp "$1~current~" $1
	chmod u+w $1
}

swap_one()
{
	mv "$1" "$1.tmp"
	mv "$1~current~" "$1"
	mv "$1.tmp" "$1~current~"
}

make_diff()
{
   {
	[ -s .patches/status ] && echo "Status: `cat .patches/status`"
	echo
	[ -s .patches/notes ] && { cat .patches/notes ; echo; }
	all_files diff_one > .patches/tmp
	echo " ----------- Diffstat output ------------"
	diffstat -p0 2> /dev/null < .patches/tmp
	cat .patches/tmp
	[ -s .patches/tmp ] || rm .patches/patch
	rm .patches/tmp
   } > .patches/patch
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
	file=`lsdiff $1 | head -1`
	orig=$file
	prefix=0
	while [ -n "$file" -a ! -f "$file" ]
	do
	    file=`expr "$file" : '[^/]*/\(.*\)'`
	    prefix=`expr $prefix + 1`
	done
	if [ -z "$file" ]
	then echo "Cannot find $orig" >&2 ; exit 1;
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
	${PAGER-less} $pfile;
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
	if [ -s .patches/to-resolv ]
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
	if patch -s --fuzz=0 --dry-run -f -p0 -R < "$pfile"
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
  list )
	    echo "Applied patches are:"
	    ls .patches/applied

	    echo "Unapplied patches are:"
	    ls .patches/removed
	    exit 0
	    ;;
 apply )
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
	    echo "Saving original at .patches/last-applied"
	    cp $pfile .patches/last-applied
	else echo >&2 "Sorry, patch doesn't apply"; exit 1
	fi
	# lets go for it ...
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

  publish )
	name=`date -u +%Y-%m-%d:%H`
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
        while p open last && p discard ; do : ; done
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
  pull )
        cd .patches/SOURCE && bk pull
	;;
  update )
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
	if [ ! -s .patches/maintainer ] ; then
	    echo "No maintainer - please add one"
	    exit 1;
	fi
	if [ ! -s .patches/owner ] ; then
	    echo "Your address and other headers must be in .patches/owner"
	    exit 1;
	fi
	cnt=$(ls .patches/applied/???${1}* | wc -l)
	cnt=$(echo $cnt)  # discard spaces
	this=1
	for patch in .patches/applied/???${1}*
	do
	  {
	      sprefix=
	      cat .patches/owner
	      echo "To: `cat .patches/maintainer`"
	      if [ -s .patches/cc ] ; then
		  while read word prefix addr
		    do if [ " $word" = " $1" ] ; then
			echo "Cc: $addr" 
			sprefix="$prefix - "
		    fi
		  done < .patches/cc
	      fi
	      head=`sed -e '/^Status/d' -e '/^$/d' -e q $patch`
	      if [ $cnt = 1 ]
		  then
		  echo "Subject: [PATCH] $sprefix $head"
		  else
		  echo "Subject: [PATCH] $sprefix$this of $cnt - $head"
	      fi
	      echo
	      echo '### Comments for ChangeSet'
	      sed -e '1,/^[^S]/d' $patch
	  } > .patches/mail/${patch#.patches/applied/}
	  this=$(expr $this + 1)
	done
	ls .patches/mail
	;;

    nomail )
	  echo "Removing .patches/mail directory"
	  rm -rf .patches/mail
	  ;;

     email )
     	PATH=/usr/lib:/usr/sbin:$PATH
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
