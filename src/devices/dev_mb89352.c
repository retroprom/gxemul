/*
 *  Copyright (C) 2021  Anders Gavare.  All rights reserved.
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
 *  COMMENT: Fujitsu MB89352 SCSI Protocol Controller (SPC)
 *
 *  TODO: Everything. :-)
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/mb89352reg.h"

#define	TICK_STEPS_SHIFT	14

const int MB89352_NREGS = 0x10;
const int MB89352_REGISTERS_LENGTH = MB89352_NREGS * 4;

const bool mb89352_abort_on_unimplemented_stuff = false;


struct mb89352_data {
	struct interrupt	irq;
	uint8_t			reg[MB89352_NREGS];
};


static void reassert_interrupts(struct mb89352_data *d)
{
	// TODO. Check INTS register etc?
	if (false)
		INTERRUPT_ASSERT(d->irq);
	else
		INTERRUPT_DEASSERT(d->irq);
}


DEVICE_TICK(mb89352)
{
	struct mb89352_data *d = (struct mb89352_data *) extra;
	reassert_interrupts(d);
}


DEVICE_ACCESS(mb89352)
{
	struct mb89352_data *d = (struct mb89352_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (len != 1) {
		debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
		    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
	    	    "unimplemented LEN: %i-bit access, address 0x%x: 0x%x",
	    	    len * 8,
	    	    (int) relative_addr);

		if (mb89352_abort_on_unimplemented_stuff) {
			cpu->running = 0;
			return 0;
		}
	}

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);
	else
		odata = d->reg[relative_addr / 4];

	switch (relative_addr / 4) {

	case INTS:
		if (writeflag == MEM_WRITE)
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
			    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
		    	    "TODO: write to INTS: 0x%x",
		    	    (int) idata);
		break;	

	default:
		if (writeflag == MEM_WRITE)
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
			    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
		    	    "unimplemented %i-bit WRITE to address 0x%x: 0x%x",
		    	    len * 8,
			    (int) relative_addr,
			    (int) idata);
		else
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
			    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
		    	    "unimplemented %i-bit READ from address 0x%x",
		    	    len * 8,
			    (int) relative_addr);

		if (mb89352_abort_on_unimplemented_stuff) {
			cpu->running = 0;
			return 0;
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(mb89352)
{
	struct mb89352_data *d;

	CHECK_ALLOCATION(d = (struct mb89352_data *) malloc(sizeof(struct mb89352_data)));
	memset(d, 0, sizeof(struct mb89352_data));

	memory_device_register(devinit->machine->memory, devinit->name,
	    devinit->addr, MB89352_REGISTERS_LENGTH, dev_mb89352_access, (void *)d,
	    DM_DEFAULT, NULL);

	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	machine_add_tickfunction(devinit->machine, dev_mb89352_tick, d, TICK_STEPS_SHIFT);

	d->reg[INTS] = 0xff;

	return 1;
}

