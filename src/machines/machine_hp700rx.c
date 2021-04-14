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
 *  COMMENT: HP 700/RX machine
 *
 *  This is for experiments with the original firmware from my HP 700/RX
 *  X-terminal, a diskless i960CA-based machine.
 *
 *  My machine says:
 *
 *  2048 KB Base RAM       at 0x30000000 .. 0x3fffffff (repeated)
 *  8192 KB Expansion RAM  at 0x40000000 .. 0x40ffffff (repeated)
 *  2048 KB Video RAM      at 0x41000000?
 *
 *  The ROM seems to be 512 KB at 0xfff80000.
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


MACHINE_SETUP(hp700rx)
{
	machine->machine_name = strdup("HP 700/RX");
	machine->cpus[0]->byte_order = EMUL_LITTLE_ENDIAN;

	dev_ram_init(machine, 0x30000000, 2 * 1048576, DEV_RAM_RAM, 0, NULL);

	// TODO: repeats of Base RAM

	dev_ram_init(machine, 0x40000000, 8 * 1048576, DEV_RAM_RAM, 0, NULL);

	// TODO: repeats of Expansion RAM

	// TODO: This is actually framebuffer memory.
	dev_ram_init(machine, 0x41000000, 2 * 1048576, DEV_RAM_RAM, 0, NULL);

	// TODO: Not RAM, but ROM.
	dev_ram_init(machine, 0xfff80000, 512 * 1024, DEV_RAM_RAM, 0, NULL);

	machine_add_devices_as_symbols(machine, 0);
}


MACHINE_DEFAULT_CPU(hp700rx)
{
	machine->cpu_name = strdup("i960CA");
}


MACHINE_DEFAULT_RAM(hp700rx)
{
	// TODO.
	machine->physical_ram_in_mb = 1;
}


MACHINE_REGISTER(hp700rx)
{
	MR_DEFAULT(hp700rx, "hp700rx", MACHINE_HP700RX);
	machine_entry_add_alias(me, "hp700rx");
	me->set_default_ram = machine_default_ram_hp700rx;
}

