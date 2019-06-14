#!/bin/sh
#
#  Regression test:
#
#  Automated install of NetBSD/pmax using R3000 CPU.
#  It's a full install, and a 4000 MB /tftpboot partition is also
#  created during the installation.
#
#
#  1. Place the iso here:
#
#	../../emul/mips/NetBSD-8.0-pmax.iso
#
#  2. Start the regression test with:
#
#	test/test_netbsd8_pmax_install.sh
#

rm -f nbsd8_pmax.img
dd if=/dev/zero of=nbsd8_pmax.img bs=1024 count=1 seek=7900000

time test/test_netbsd8_pmax_install.expect 2> /tmp/gxemul_result

echo
echo
echo
cat /tmp/gxemul_result
