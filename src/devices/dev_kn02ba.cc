/*
 *  Copyright (C) 2003-2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: DEC KN02BA "3min" TurboChannel interrupt controller
 *
 *  Used in DECstation 5000/1xx "3MIN".  See include/dec_kmin.h for more info.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/dec_kmin.h"


#define	DEV_KN02BA_DUMMYADDR	0x100000000
#define	DEV_KN02BA_DUMMYLENGTH	0x1000


struct kn02ba_data {
	struct dec_ioasic_data *dec_ioasic;
	struct interrupt irq;
};


/*
 *  kn02ba_interrupt_assert(), kn02ba_interrupt_deassert():
 *
 *  Called whenever a kn02ba interrupt is asserted/deasserted.
 */
void kn02ba_interrupt_assert(struct interrupt *interrupt)
{
	struct kn02ba_data *d = (struct kn02ba_data *) interrupt->extra;
	struct dec_ioasic_data *r = (struct dec_ioasic_data *) d->dec_ioasic;
	r->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] |= interrupt->line;
	dec_ioasic_reassert(r);
}
void kn02ba_interrupt_deassert(struct interrupt *interrupt)
{
	struct kn02ba_data *d = (struct kn02ba_data *) interrupt->extra;
	struct dec_ioasic_data *r = (struct dec_ioasic_data *) d->dec_ioasic;
	r->reg[(IOASIC_INTR - IOASIC_SLOT_1_START) / 0x10] &= ~interrupt->line;
	dec_ioasic_reassert(r);
}


DEVICE_ACCESS(kn02ba)
{
	struct kn02ba_data *d = (struct kn02ba_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			fatal("[ kn02ba: read from 0x%08lx ]\n",
			    (long)relative_addr);
		} else {
			fatal("[ kn02ba: write to  0x%08lx: 0x%08x ]\n",
			    (long)relative_addr, (int)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(kn02ba)
{
	struct kn02ba_data *d;
	int i;

	CHECK_ALLOCATION(d = (struct kn02ba_data *) malloc(sizeof(struct kn02ba_data)));
	memset(d, 0, sizeof(struct kn02ba_data));

	/*  Connect the kn02ba to a specific MIPS CPU interrupt line:  */
	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	/*  Register the interrupts:  */
	for (i = 0; i < 32; i++) {
		struct interrupt templ;
		char tmpstr[300];
		snprintf(tmpstr, sizeof(tmpstr), "%s.kn02ba.0x%x",
		    devinit->interrupt_path, 1 << i);
		printf("registering '%s'\n", tmpstr);
		memset(&templ, 0, sizeof(templ));
		templ.line = 1 << i;
		templ.name = tmpstr;
		templ.extra = d;
		templ.interrupt_assert = kn02ba_interrupt_assert;
		templ.interrupt_deassert = kn02ba_interrupt_deassert;
		interrupt_handler_register(&templ);
	}

	memory_device_register(devinit->machine->memory, devinit->name,
	    DEV_KN02BA_DUMMYADDR, DEV_KN02BA_DUMMYLENGTH, dev_kn02ba_access, d,
	    DM_DEFAULT, NULL);

	d->dec_ioasic = dev_dec_ioasic_init(devinit->machine->cpus[0],
		devinit->machine->memory, KMIN_SYS_ASIC, 0, &d->irq);

	return 1;
}

