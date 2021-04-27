#!/bin/sh
#
#  A small test to measure how long time it takes (real time) to boot
#  OpenBSD/luna88k up to the login prompt.
#
#  Start with:
#
#	test/test_openbsd_luna88k_bootup.sh
#
#  after making sure that you have 'liveimage-luna88k-raw-XXXXXXXX.img' and
#  'boot' in the current directory.
#

time test/test_openbsd_luna88k_bootup.expect 2> /tmp/gxemul_result

echo "-------------------------------------------------"
echo
cat /tmp/gxemul_result

