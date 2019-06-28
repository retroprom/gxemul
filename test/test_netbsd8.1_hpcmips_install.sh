#!/bin/sh
#
#  Regression test:
#
#  Automated full install of NetBSD/hpcmips, using serial console.
#
#
#  1. Place the iso here:
#
#	../../emul/mips/netbsd-hpcmips-8.1/NetBSD-8.1-hpcmips.iso
#
#  2. Start the regression test with:
#
#	test/test_netbsd8.1_hpcmips_install.sh
#

rm -f nbsd8.1_hpcmips.img
dd if=/dev/zero of=nbsd8.1_hpcmips.img bs=1024 count=1 seek=5000000

time test/test_netbsd8.1_hpcmips_install.expect 2> /tmp/gxemul_result

echo
echo
echo
cat /tmp/gxemul_result

