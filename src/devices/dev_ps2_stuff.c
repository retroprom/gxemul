/*
 *  Copyright (C) 2003-2004 by Anders Gavare.  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright  
 *     notice, this list of conditions and the following disclaimer in the 
 *     documentation and/or other materials provided with the distribution.
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
 *  $Id: dev_ps2_stuff.c,v 1.1 2004-03-06 17:10:52 debug Exp $
 *  
 *  Playstation 2 misc. stuff:
 *
 *	offset 0x0000	timer control
 *	offset 0x8000	DMA controller
 *	offset 0xf000	Interrupt register
 *
 *  TODO:  use netbsd's playstation2/ee/timerreg.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "devices.h"

#include "ps2_dmacreg.h"

#define	TICK_STEPS_SHIFT	17


/*
 *  dev_ps2_stuff_tick():
 */
void dev_ps2_stuff_tick(struct cpu *cpu, void *extra)
{
	struct ps2_data *d = extra;

	if (d->timer0_mode & 0x200)		/*  TODO: timer enable bits  */
		cpu_interrupt(cpu, 0x200 +8);		/*  0x200 is timer0  */
}


/*
 *  dev_ps2_stuff_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_ps2_stuff_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	uint64_t idata = 0, odata = 0;
	int i, regnr = 0;
	struct ps2_data *d = extra;

	idata = memory_readmax64(cpu, data, len);

	if (relative_addr >= 0x8000 && relative_addr < 0x8000 + DMAC_REGSIZE) {
		regnr = (relative_addr - 0x8000) / 16;
		if (writeflag == MEM_READ)
			odata = d->dmac_reg[regnr];
		else
			d->dmac_reg[regnr] = idata;
	}

	switch (relative_addr) {
	case 0x0000:	/*  timer 0 count  */
		if (writeflag == MEM_READ) {
			odata = d->timer0_count;
			d->timer0_count ++;	/*  :-)  TODO  */
			debug("[ ps2_stuff: read timer 0 count: 0x%llx ]\n", (long long)odata);
		} else {
			d->timer0_count = idata;
			debug("[ ps2_stuff: write timer 0 count: 0x%llx ]\n", (long long)idata);
		}
		break;
	case 0x0010:	/*  timer 0 mode  */
		if (writeflag == MEM_READ) {
			odata = d->timer0_mode;
			debug("[ ps2_stuff: read timer 0 mode: 0x%llx ]\n", (long long)odata);
		} else {
			d->timer0_mode = idata;
			debug("[ ps2_stuff: write timer 0 mode: 0x%llx ]\n", (long long)idata);
		}
		break;
	case 0x0020:	/*  timer 0 comp  */
		if (writeflag == MEM_READ) {
			odata = d->timer0_comp;
			debug("[ ps2_stuff: read timer 0 comp: 0x%llx ]\n", (long long)odata);
		} else {
			d->timer0_comp = idata;
			debug("[ ps2_stuff: write timer 0 comp: 0x%llx ]\n", (long long)idata);
		}
		break;

	case 0x8000 + D2_CHCR_REG:
		if (writeflag==MEM_READ) {
			odata = d->dmac_reg[regnr];
			/*  debug("[ ps2_stuff: dmac read from D2_CHCR (0x%llx) ]\n", (long long)d->dmac_reg[regnr]);  */
		} else {
			/*  debug("[ ps2_stuff: dmac write to D2_CHCR, data 0x%016llx ]\n", (long long) idata);  */
			if (idata & D_CHCR_STR) {
				int length = d->dmac_reg[D2_QWC_REG/0x10] * 16;
				uint64_t from_addr = 0xa0000000 + d->dmac_reg[D2_MADR_REG/0x10];
				uint64_t to_addr   = 0xa0000000 + d->dmac_reg[D2_TADR_REG/0x10];
				unsigned char *copy_buf;

				debug("[ ps2_stuff: dmac [ch2] transfer addr=0x%016llx len=0x%lx ]\n",
				    (long long)d->dmac_reg[D2_MADR_REG/0x10], (long)length);

				copy_buf = malloc(length);
				memory_rw(cpu, cpu->mem, from_addr, copy_buf, length, MEM_READ, CACHE_NONE);
				memory_rw(cpu, d->other_memory[2], to_addr, copy_buf, length, MEM_WRITE, CACHE_NONE);
				free(copy_buf);

				/*  Done with the transfer:  */
				d->dmac_reg[D2_QWC_REG/0x10] = 0;
				idata &= ~D_CHCR_STR;
			} else
				debug("[ ps2_stuff: dmac [ch2] stopping transfer ]\n");
			d->dmac_reg[regnr] = idata;
			return 1;
		}
		break;

	case 0x8000 + D2_QWC_REG:
	case 0x8000 + D2_MADR_REG:
	case 0x8000 + D2_TADR_REG:
		/*  no debug output  */
		break;

	case 0xe010:	/*  dmac interrupt status  */
		if (writeflag == MEM_WRITE) {
			/*  Clear out those bits that are set in idata:  */
			d->dmac_reg[regnr] &= ~idata;
		}
		break;

	case 0xf000:	/*  interrupt register  */
		if (writeflag == MEM_READ)
			odata = d->intr;
		else {
			/*  Clear out those bits that are set in idata:  */
			/*  d->intr &= ~idata;  */
			/*  TODO:  which of the above and below is best?  */
			cpu_interrupt_ack(cpu, idata +8);
		}
		break;

	case 0xf010:	/*  interrupt mask  */
		if (writeflag == MEM_READ)
			odata = d->imask;
		else {
			d->imask = idata;
		}
		break;

	default:
		if (writeflag==MEM_READ) {
			debug("[ ps2_stuff: read from addr 0x%x: 0x%llx ]\n", (int)relative_addr, (long long)odata);
		} else {
			debug("[ ps2_stuff: write to addr 0x%x: 0x%llx ]\n", (int)relative_addr, (long long)idata);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_ps2_stuff_init():
 *
 *	mem_gif			pointer to the GIF's memory
 */
struct ps2_data *dev_ps2_stuff_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, struct memory *mem_gif)
{
	struct ps2_data *d;

	d = malloc(sizeof(struct ps2_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct ps2_data));

	d->other_memory[DMA_CH_GIF] = mem_gif;

	memory_device_register(mem, "ps2_stuff", baseaddr, DEV_PS2_STUFF_LENGTH, dev_ps2_stuff_access, d);
	cpu_add_tickfunction(cpu, dev_ps2_stuff_tick, d, TICK_STEPS_SHIFT);

	return d;
}

