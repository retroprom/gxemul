/*
 *  Copyright (C) 2003-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: dev_dec5800.c,v 1.10 2005-01-23 11:19:36 debug Exp $
 *  
 *  Emulation of devices found in a DECsystem 58x0, where x is the number
 *  of CPUs in the system. (The CPU board is called KN5800 by Ultrix.)
 *
 *	o)  timers and misc stuff
 *	o)  BI  (Backplane Interconnect)
 *	o)  CCA (Console Communication Area)
 *	o)  XMI (Extended Memory Interconnect)
 *
 *  TODO:  This hardware is not very easy to find docs about.
 *  Perhaps VAX 6000/300 docs?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "mips_cpu.h"
#include "misc.h"


/*
 *  dev_dec5800_tick():
 */
void dev_dec5800_tick(struct cpu *cpu, void *extra)
{
	struct dec5800_data *d = extra;

	/*  Timer interrupts?  */
	if (d->csr & 0x8000) {
		debug("[ dec5800: timer interrupt! ]\n");

		/*  Set timer interrupt pending bit:  */
		d->csr |= 0x20000000;

		cpu_interrupt(cpu, 3);
	}
}


/*
 *  dev_dec5800_vectors_access():
 */
int dev_dec5800_vectors_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct dec5800_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	if (writeflag == MEM_READ) {
		/*  TODO  */
		/*  0xfc = transmit interrupt, 0xf8 = receive interrupt, 0x80 = IPI  */
		odata = d->vector_0x50;
/* odata = 0xfc; */
		debug("[ dec5800_vectors: read from 0x%02x: 0x%02x ]\n", (int)relative_addr, (int)odata);
	} else {
		d->vector_0x50 = idata;
		debug("[ dec5800_vectors: write to 0x%02x: 0x%02x ]\n", (int)relative_addr, (int)idata);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec5800_access():
 */
int dev_dec5800_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	struct dec5800_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	/*  Lowest 4 bits of csr contain cpu id:  */
	d->csr = (d->csr & ~0xf) | (cpu->cpu_id & 0xf);

	switch (relative_addr) {
	case 0x0000:	/*  csr  */
		if (writeflag == MEM_READ) {
			odata = d->csr;
			odata ^= random() & 0x10000;
			debug("[ dec5800: read from csr: 0x%08x ]\n", (int)odata);
		} else {
			d->csr = idata;

			/*  Ack. timer interrupts:  */
			d->csr &= ~0x20000000;
			cpu_interrupt_ack(cpu, 3);

			debug("[ dec5800: write to csr: 0x%08x ]\n", (int)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ dec5800: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ dec5800: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_dec5800_init():
 */
struct dec5800_data *dev_dec5800_init(struct cpu *cpu,
	struct memory *mem, uint64_t baseaddr)
{
	struct dec5800_data *d;

	d = malloc(sizeof(struct dec5800_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct dec5800_data));

	memory_device_register(mem, "dec5800", baseaddr,
	    DEV_DEC5800_LENGTH, dev_dec5800_access, d, MEM_DEFAULT, NULL);
	memory_device_register(mem, "dec5800_vectors",
	    baseaddr + 0x30000000, 0x100, dev_dec5800_vectors_access, d, MEM_DEFAULT, NULL);
	cpu_add_tickfunction(cpu, dev_dec5800_tick, d, 14);

	return d;
}


/*****************************************************************************/


#include "bireg.h"

struct decbi_data {
	int		csr[NNODEBI];
};


/*
 *  dev_decbi_access():
 */
int dev_decbi_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int node_nr;
	struct decbi_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	relative_addr += BI_NODESIZE;	/*  HACK  */

	node_nr = relative_addr / BI_NODESIZE;
	relative_addr &= (BI_NODESIZE - 1);

	/*  TODO:  This "1" here is the max node number in actual use.  */
	if (node_nr > 1 || node_nr >= NNODEBI)
		return 0;

	switch (relative_addr) {
	case BIREG_DTYPE:
		if (writeflag==MEM_READ) {
			/*
			 *  This is a list of the devices in our BI slots:
			 */
			switch (node_nr) {
			case 1:	odata = BIDT_KDB50; break;		/*  Disk  */
			/*  case 2:	odata = BIDT_DEBNA; break;  */	/*  Ethernet  */
			/*  case 3:	odata = BIDT_MS820; break;  */	/*  Memory  */
			default:
				/*  No device.  */
				odata = 0;
			}

			debug("[ decbi: (node %i) read from BIREG_DTYPE: 0x%x ]\n", node_nr, odata);
		} else {
			debug("[ decbi: (node %i) attempt to write to BIREG_DTYPE: 0x%08x ]\n", node_nr, idata);
		}
		break;
	case BIREG_VAXBICSR:
		if (writeflag==MEM_READ) {
			odata = (d->csr[node_nr] & ~BICSR_NODEMASK) | node_nr;
			debug("[ decbi: (node %i) read from BIREG_VAXBICSR: 0x%x ]\n", node_nr, odata);
		} else {
			d->csr[node_nr] = idata;
			debug("[ decbi: (node %i) attempt to write to BIREG_VAXBICSR: 0x%08x ]\n", node_nr, idata);
		}
		break;
	case 0xf4:
		if (writeflag==MEM_READ) {
			odata = 0xffff;	/*  ?  */
			debug("[ decbi: (node %i) read from 0xf4: 0x%x ]\n", node_nr, odata);
		} else {
			debug("[ decbi: (node %i) attempt to write to 0xf4: 0x%08x ]\n", node_nr, idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ decbi: (node %i) read from unimplemented 0x%08lx ]\n", node_nr, (long)relative_addr, odata);
		} else {
			debug("[ decbi: (node %i) write to unimpltemeted  0x%08lx: 0x%08x ]\n", node_nr, (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_decbi_init():
 */
void dev_decbi_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct decbi_data *d;

	d = malloc(sizeof(struct decbi_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct decbi_data));

	memory_device_register(mem, "decbi", baseaddr + 0x2000,
	    DEV_DECBI_LENGTH - 0x2000, dev_decbi_access, d, MEM_DEFAULT, NULL);
}


/*****************************************************************************/


/*
 *  CCA, "Console Communication Area" for a DEC 5800 SMP system.
 */

struct deccca_data {
	int		dummy;
};


/*
 *  dev_deccca_access():
 */
int dev_deccca_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	/*  struct deccca_data *d = extra;  */

	idata = memory_readmax64(cpu, data, len);

	switch (relative_addr) {
	case 6:
	case 7:
		/*  CCA "ID" bytes? These must be here, or Ultrix complains.  */
		if (writeflag == MEM_READ)
			odata = 67;
		break;
	case 8:
		if (writeflag == MEM_READ)
			odata = cpu->machine->ncpus;
		break;
	case 20:
		if (writeflag == MEM_READ)
			odata = (1 << cpu->machine->ncpus) - 1;
			    /*  one bit for each cpu  */
		break;
	case 28:
		if (writeflag == MEM_READ)
			odata = (1 << cpu->machine->ncpus) - 1;
			    /*  one bit for each enabled(?) cpu  */
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ deccca: read from 0x%08lx ]\n", (long)relative_addr);
		} else {
			debug("[ deccca: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_deccca_init():
 */
void dev_deccca_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct deccca_data *d;

	d = malloc(sizeof(struct deccca_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct deccca_data));

	memory_device_register(mem, "deccca", baseaddr, DEV_DECCCA_LENGTH,
	    dev_deccca_access, d, MEM_DEFAULT, NULL);
}


/*****************************************************************************/


/*
 *  DEC 5800 XMI (this has to do with SMP...)
 */

#include "xmireg.h"

struct decxmi_data {
	uint32_t		reg_0xc[NNODEXMI];
};


/*
 *  dev_decxmi_access():
 */
int dev_decxmi_access(struct cpu *cpu, struct memory *mem,
	uint64_t relative_addr, unsigned char *data, size_t len,
	int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int node_nr;
	struct decxmi_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	node_nr = relative_addr / XMI_NODESIZE;
	relative_addr &= (XMI_NODESIZE - 1);

	if (node_nr >= cpu->machine->ncpus + 1 || node_nr >= NNODEXMI)
		return 0;

	switch (relative_addr) {
	case XMI_TYPE:
		if (writeflag == MEM_READ) {
			/*
			 *  The first node is an XMI->BI adapter node, and then
			 *  there are n CPU nodes.
			 */
			odata = XMIDT_ISIS;
			if (node_nr == 0)
				odata = XMIDT_DWMBA;

			debug("[ decxmi: (node %i) read from XMI_TYPE: 0x%08x ]\n", node_nr, odata);
		} else
			debug("[ decxmi: (node %i) write to XMI_TYPE: 0x%08x ]\n", node_nr, idata);
		break;
	case XMI_BUSERR:
		if (writeflag == MEM_READ) {
			odata = 0;
			debug("[ decxmi: (node %i) read from XMI_BUSERR: 0x%08x ]\n", node_nr, odata);
		} else
			debug("[ decxmi: (node %i) write to XMI_BUSERR: 0x%08x ]\n", node_nr, idata);
		break;
	case XMI_FAIL:
		if (writeflag == MEM_READ) {
			odata = 0;
			debug("[ decxmi: (node %i) read from XMI_FAIL: 0x%08x ]\n", node_nr, odata);
		} else
			debug("[ decxmi: (node %i) write to XMI_FAIL: 0x%08x ]\n", node_nr, idata);
		break;
	case 0xc:
		if (writeflag == MEM_READ) {
			odata = d->reg_0xc[node_nr];
			debug("[ decxmi: (node %i) read from REG 0xC: 0x%08x ]\n", node_nr, (int)odata);
		} else {
			d->reg_0xc[node_nr] = idata;
			debug("[ decxmi: (node %i) write to REG 0xC: 0x%08x ]\n", node_nr, (int)idata);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ decxmi: (node %i) read from unimplemented 0x%08lx ]\n", node_nr, (long)relative_addr, odata);
		} else {
			debug("[ decxmi: (node %i) write to unimpltemeted  0x%08lx: 0x%08x ]\n", node_nr, (long)relative_addr, idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_decxmi_init():
 */
void dev_decxmi_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr)
{
	struct decxmi_data *d;

	d = malloc(sizeof(struct decxmi_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct decxmi_data));

	memory_device_register(mem, "decxmi", baseaddr, DEV_DECXMI_LENGTH,
	    dev_decxmi_access, d, MEM_DEFAULT, NULL);
}

