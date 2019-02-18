/*
 *  Copyright (C) 2018-2019  Anders Gavare.  All rights reserved.
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
 *  TODO. This is bogus so far, only enough to see the Linux kernel start
 *  executing instructions.
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


uint32_t swap_if_little_endian(uint32_t v, struct cpu* cpu)
{
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		return v;

	return (v >> 24) | ((v & 0x00ff0000) >> 8)
		| ((v & 0x0000ff00) << 8) | ((v & 0xff) << 24);
}


MACHINE_SETUP(androidarm)
{
	char tmpstr[1000];
	bool use_atags = false;

	cpu->byte_order = EMUL_LITTLE_ENDIAN;

	switch (machine->machine_subtype) {

	case MACHINE_ANDROIDARM_FINOWX5AIR:
		machine->machine_name = strdup("Finow X5 Air");

		// 2 GB ram at 0x80000000? Mirrored at 0 maybe? (But in GXemul's legacy
		// framework, we have to make the mirror the other way around.)
		dev_ram_init(machine, 0x80000000, 0x10000000, DEV_RAM_MIRROR, 0x0);
		dev_ram_init(machine, 0x90000000, 0x70000000, DEV_RAM_RAM, 0x0);

		// Yet another mirror at 0x40000000?
		dev_ram_init(machine, 0x40000000, 0x10000000, DEV_RAM_MIRROR, 0x0);

		// See https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/mt6580.dtsi
		//	timer: timer@10008000
		//	sysirq: interrupt-controller@10200100
		//	gic: interrupt-controller@10211000
		//	uart0: serial@11005000
		//	uart1: serial@11006000

		// "gic" interrupt controller:
		// See https://github.com/torvalds/linux/blob/master/include/dt-bindings/interrupt-controller/arm-gic.h

		// TODO: interrupt "GIC_SPI 44"
		snprintf(tmpstr, sizeof(tmpstr), "ns16550 irq=%s.cpu[%i].irq"
		    " addr=0x11005000 addr_mult=4 in_use=%i",
		    machine->path, machine->bootstrap_cpu, !machine->x11_md.in_use);
		machine->main_console_handle = (size_t)
		    device_add(machine, tmpstr);

		dev_fb_init(machine, machine->memory,
			0x12340000 /*  TODO addr  */, 
			VFB_GENERIC,
			400, 400,
			400, 400,
			24, machine->machine_name);

		break;

	case MACHINE_ANDROIDARM_SONYXPERIAMINI:
		machine->machine_name = strdup("Sony Xperia Mini");

		// Mirror at 0x40000000?
		dev_ram_init(machine, 0x40000000, 0x20000000, DEV_RAM_MIRROR, 0x0);

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

	// See https://www.kernel.org/doc/Documentation/arm/Booting
	cpu->cd.arm.r[0] = 0x00000000;
	cpu->cd.arm.r[1] = 0xffffffff;	// architecture ID. 0xffffffff = use DT (device tree)?

	if (use_atags) {
		// ATAG list: TODO.
		cpu->cd.arm.r[2] = 0x00002000;	// ATAGs at 8KB from start of low RAM.
	} else {
		// Device Tree:
		// https://www.kernel.org/doc/Documentation/devicetree/booting-without-of.txt
		cpu->cd.arm.r[2] = 0x08000000;	// Device Tree at 128 MB from start of RAM.
		
		store_32bit_word(cpu, cpu->cd.arm.r[2], swap_if_little_endian(0xd00dfeed, cpu));
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
		// Ram is at 0x80000000?
		machine->physical_ram_in_mb = 256;
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

