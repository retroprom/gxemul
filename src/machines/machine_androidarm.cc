/*
 *  Copyright (C) 2018  Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE   
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *   
 *
 *  COMMENT: ARM-based "Android" machines
 *
 *  TODO. This is bogus so far.
 *
 *  gxemul -e sony-xperia-mini -tvvK boot.img 
 *  gxemul -e finow-x5-air -tvvK boot.img 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"


MACHINE_SETUP(androidarm)
{
	cpu->byte_order = EMUL_LITTLE_ENDIAN;

	// Odd register values, for debugging the startup procedure.
	// See "start:" at https://android.googlesource.com/kernel/msm/+/42bad328ba45ee4fe93216e7e99fe79a782d9155/arch/arm/boot/compressed/head.S
	cpu->cd.arm.r[1] = 0x99911111;	// architecture ID?
	cpu->cd.arm.r[2] = 0x99921111;	// atags struct?

	// Invalid link return:
	cpu->cd.arm.r[14] = 0x00000006;

	switch (machine->machine_subtype) {

	case MACHINE_ANDROIDARM_FINOWX5AIR:
		machine->machine_name = strdup("Finow X5 Air");

		dev_ram_init(machine, 0x80000000, 0x40000000, DEV_RAM_MIRROR, 0x0);

		dev_fb_init(machine, machine->memory,
			0x23450000 /*  TODO addr  */, 
			VFB_GENERIC,
			400, 400,
			400, 400,
			24, machine->machine_name);

		break;

	case MACHINE_ANDROIDARM_SONYXPERIAMINI:
		machine->machine_name = strdup("Sony Xperia Mini");

		// Perhaps https://talk.sonymobile.com/t5/Xperia-mini-pro/Sk17i-in-continous-boot-loop/td-p/281671#gref
		// could be of some help.

		dev_fb_init(machine, machine->memory,
			0x23450000 /*  TODO addr  */, 
			VFB_GENERIC,
			320, 480,
			320, 480,
			24, machine->machine_name);

		break;

	default:printf("Unimplemented android-arm machine number.\n");
		exit(1);
	}
}


MACHINE_DEFAULT_CPU(androidarm)
{
	switch (machine->machine_subtype) {

	case MACHINE_ANDROIDARM_FINOWX5AIR:
		// Cortex-A7?  MTK6580 at 1.3 GHz? Quadcore?
		machine->cpu_name = strdup("CORTEX-A5");
		break;

	case MACHINE_ANDROIDARM_SONYXPERIAMINI:
		// "Qualcomm MSM8255 Snapdragon S2"
		// My real Xperia Mini says the following when booting the Linux kernel:
		// [    0.000000] CPU: ARMv7 Processor [511f00f2] revision 2 (ARMv7), cr=10c53c7d
		machine->cpu_name = strdup("SnapdragonS2");
		break;

	default:
		machine->cpu_name = strdup("CORTEX-A5");
	}
}


MACHINE_DEFAULT_RAM(androidarm)
{
	switch (machine->machine_subtype) {

	case MACHINE_ANDROIDARM_FINOWX5AIR:
		machine->physical_ram_in_mb = 2048;
		break;

	case MACHINE_ANDROIDARM_SONYXPERIAMINI:
		machine->physical_ram_in_mb = 512;
		break;

	default:
		machine->physical_ram_in_mb = 512;
	}
}


MACHINE_REGISTER(androidarm)
{
	MR_DEFAULT(androidarm, "ARM-based \"Android\" machines", ARCH_ARM, MACHINE_ANDROIDARM);

	machine_entry_add_alias(me, "android-arm");

	machine_entry_add_subtype(me, "Finow X5 Air", MACHINE_ANDROIDARM_FINOWX5AIR,
	    "finow-x5-air", NULL);

	machine_entry_add_subtype(me, "Sony Xperia Mini", MACHINE_ANDROIDARM_SONYXPERIAMINI,
	    "sony-xperia-mini", NULL);

	me->set_default_ram = machine_default_ram_androidarm;
}
