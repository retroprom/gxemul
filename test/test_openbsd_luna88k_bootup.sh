#!/bin/sh
#
#  A small test to measure how long time it takes (real time) to boot
#  OpenBSD/luna88k up to the login prompt.
#
#  Start with:
#
#	test/test_openbsd_luna88k_bootup.sh
#
#  after making sure that you have 'liveimage-luna88k-raw-20201206.img' and
#  'boot' in the current directory.
#
#
#  2021-04-22 baseline:
#	71.10 seconds: ./configure gcc8 --debug
#	36.70 seconds: ./configure (clang 10.0.1)
#
#  M88K_MAX_VPH_TLB_ENTRIES:
#	 32	37.21 seconds
#	 64	36.76 seconds
#	128	36.70 seconds
#	192	36.67 seconds


time test/test_openbsd_luna88k_bootup.expect 2> /tmp/gxemul_result

echo "-------------------------------------------------"
echo
cat /tmp/gxemul_result

