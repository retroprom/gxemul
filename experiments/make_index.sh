#!/bin/sh
#
#  Updates the .index file.
#

rm -f .index
for a in *.cc; do
	B=`grep COMMENT $a`
	if [ z"$B" != z ]; then
		printf "$a " >> .index
		echo "$B"|cut -d : -f 2- >> .index
	fi
done

