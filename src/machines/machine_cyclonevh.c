/*
 *  Copyright (C) 2018-2021  Anders Gavare.  All rights reserved.
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
 *  COMMENT: Cyclone VH machine
 *
 *  For experiments with uClinux/i960.
 *
 *  A binary (vmlinux) can be found on this page:
 *  https://web.archive.org/web/20010417034914/http://www.cse.ogi.edu/~kma/uClinux.html
 *
 *  NOTE!!! The binary at http://www.uclinux.org/pub/uClinux/ports/i960/ is corrupt;
 *          it seems to have been uploaded/encoded with the wrong character encoding.
 *          (At least it is broken as of 2016-04-18.)
 *
 *
 *	gxemul -vvvKi -E cyclonevh 0xa3c08000:0xb8:0xa3c08020:vmlinux
 *
 *
 *  See the following link for details about the Cyclone VH board:
 *  
 *  http://www.nj7p.org/Manuals/PDFs/Intel/273194-003.PDF
 *  "EVAL80960VH Evaluation Platform Board Manual, December 1998"
 *
 *  and for the CPU:
 *
 *  http://www.nj7p.info/Manuals/PDFs/Intel/273173-001.PDF
 *  "i960 VH Processor Developer's Manual, October 1998"
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


MACHINE_SETUP(cyclonevh)
{
	machine->machine_name = strdup("Cyclone VH");
	machine->cpus[0]->byte_order = EMUL_LITTLE_ENDIAN;

	// DRAM is at a3c0 0000 - a3ff ffff according to
	// https://groups.google.com/forum/#!topic/intel.microprocessors.i960/tgpjDcW5Dxc
	dev_ram_init(machine, 0xa3c00000, 0x400000, DEV_RAM_RAM, 0, "dram");

	machine_add_devices_as_symbols(machine, 0);
}


MACHINE_DEFAULT_CPU(cyclonevh)
{
	// It's a "i960VH", but has a "i960Jx" core?
	machine->cpu_name = strdup("i960Jx");
}


MACHINE_DEFAULT_RAM(cyclonevh)
{
	// TODO. Just 1 KB at offset 0. The "base" and "expanded" RAM are
	// at higher addresses.
	machine->physical_ram_in_mb = 1;
}


MACHINE_REGISTER(cyclonevh)
{
	MR_DEFAULT(cyclonevh, "cyclonevh", MACHINE_CYCLONEVH);
	machine_entry_add_alias(me, "cyclonevh");
	me->set_default_ram = machine_default_ram_cyclonevh;
}

