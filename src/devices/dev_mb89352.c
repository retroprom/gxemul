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
 *  Based on looking at openbsd/sys/arch/luna88k/stand/boot (the OpenBSD boot
 *  loader), and matching its expectations.
 *
 *  TODO: Almost everything.
 *  TODO IN PARTICULAR: Make it work also with the OpenBSD kernel!
 */

#include <stdio.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "diskimage.h"
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
	bool			irq_asserted;

	uint8_t			reg[MB89352_NREGS];
	
	int			target;		// target SCSI ID, e.g. 6
	int			phase;
	size_t			xlen;

	struct scsi_transfer	*xferp;

	size_t			bufpos;
	uint8_t			*buf;
};


static void reassert_interrupts(struct mb89352_data *d)
{
	bool assert = d->reg[INTS] != 0;

	if (!(d->reg[SCTL] & SCTL_INTR_ENAB))
		assert = false;
	if (d->reg[SCTL] & SCTL_DISABLE)
		assert = false;

	if (assert && !d->irq_asserted)
		INTERRUPT_ASSERT(d->irq);
	else if (!assert && d->irq_asserted)
		INTERRUPT_DEASSERT(d->irq);

	d->irq_asserted = assert;
}


static void reset_controller(struct mb89352_data *d)
{
	debugmsg(SUBSYS_DEVICE, "mb89352", VERBOSITY_INFO, "resetting controller");

	memset(d->reg, 0, sizeof(d->reg));

	d->reg[BDID] = 7;
	d->reg[SCTL] = SCTL_DISABLE;
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

	case BDID:
		if (writeflag == MEM_WRITE) {
			d->reg[BDID] = idata;
			if (idata != 7)
				debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
				    VERBOSITY_INFO, "unimplemented BDID value: 0x%x",
			    	    (int) idata);
		}
		break;

	case SCTL:
		if (writeflag == MEM_WRITE) {
			d->reg[SCTL] = idata;

			if (idata & SCTL_CTRLRST)
				reset_controller(d);
		}
		break;

	case SCMD:
		if (writeflag == MEM_WRITE) {
			d->reg[SCMD] = idata;
			
			switch (idata) {

			case SCMD_BUS_REL:	// 0x00
				break;

			case SCMD_SELECT:
				// Select target in the temp register.
				{
					int target = 0;
					while (target < 8) {
						int m = 1 << target;
						if (d->reg[TEMP] & ~(1 << 7) & m)
							break;
						target ++;
					}

					if (target < 8)
						debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
						    VERBOSITY_DEBUG,
					    	    "SCMD_SELECT target %i", target);
					else
						debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
						    VERBOSITY_WARNING,
					    	    "SCMD_SELECT with no target?");

					d->target = target;
					d->reg[INTS] |= INTS_CMD_DONE;
					d->reg[PSNS] &= ~7;
					d->reg[PSNS] |= 2; //CMD_PHASE;
					d->reg[PSNS] |= PSNS_REQ;

					if (d->xferp != NULL)
						scsi_transfer_free(d->xferp);

					d->xferp = scsi_transfer_alloc();
				}
				break;

			case SCMD_XFR | SCMD_PROG_XFR:
				{
					d->phase = d->reg[PCTL] & 7;
					d->xlen = (d->reg[TCH] << 16) + (d->reg[TCM] << 8)
						+ d->reg[TCL];

					if (d->buf == NULL)
						d->buf = malloc(d->xlen);

					d->bufpos = 0;

					debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
					    VERBOSITY_WARNING,
				    	    "SCMD_XFR | SCMD_PROG_XFR, phase %i, len %i", d->phase, d->xlen);

					d->reg[INTS] |= INTS_CMD_DONE;
				}
				break;

			default:
				debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
				    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
			    	    "unimplemented SCMD: 0x%x", (int) idata);

				if (mb89352_abort_on_unimplemented_stuff) {
					cpu->running = 0;
					return 0;
				}
			}
		}
		break;

	case INTS:
		if (writeflag == MEM_WRITE) {
			d->reg[INTS] &= ~idata;
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
			    VERBOSITY_INFO, "write to INTS: 0x%x",
		    	    (int) idata);
		}
		break;	

	case PSNS:
		if (writeflag == MEM_WRITE) {
			d->reg[relative_addr / 4] = idata;
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
			    VERBOSITY_WARNING,
		    	    "unimplemented write to PSNS: 0x%x",
			    (int) idata);
		}
		break;

	case SSTS:
		if (writeflag == MEM_WRITE) {
			d->reg[relative_addr / 4] = idata;
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
			    VERBOSITY_WARNING,
		    	    "unimplemented write to SSTS: 0x%x",
			    (int) idata);
		}
		break;

	case PCTL:
		if (writeflag == MEM_WRITE) {
			d->reg[relative_addr / 4] = idata;
		}
		break;

	case DREG:
		if (writeflag == MEM_WRITE) {
			d->buf[d->bufpos++] = idata;
			
			if (d->bufpos == d->xlen) {
				debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
				    VERBOSITY_WARNING,
			    	    "%i bytes written to buffer", d->xlen);

				switch (d->phase) {
				case 2:	// CMD
					d->xferp->cmd = d->buf;
					d->buf = NULL;
					d->xferp->cmd_len = d->bufpos;
					d->bufpos = 0;
					break;
				default:
					debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
					    VERBOSITY_ERROR,
				    	    "DREG write: UNIMPLEMENTED PHASE %i", d->phase);
				    	cpu->running = 0;
				}
				
				/* int res = */ diskimage_scsicommand(cpu,
				    d->target, DISKIMAGE_SCSI, d->xferp);

				if (d->xferp->data_in != NULL) {
					d->buf = d->xferp->data_in;
					d->xferp->data_in = NULL;
					d->xlen = d->xferp->data_in_len;
					d->bufpos = 0;
					d->phase = 1;	// DATA_IN
				} else if (d->xferp->status != NULL) {
					d->buf = d->xferp->status;
					d->xferp->status = NULL;
					d->xlen = d->xferp->status_len;
					d->bufpos = 0;
					d->phase = 3;	// STATUS
				}
				
				d->reg[PSNS] &= ~7;
				d->reg[PSNS] |= d->phase;
				d->reg[PSNS] |= PSNS_REQ;
				
				d->reg[INTS] |= INTS_CMD_DONE;	// ?
			}
		} else {
			if (d->buf == NULL) {
				debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
				    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
			    	    "DREG: no buffer to read from?");
			} else if (d->bufpos >= d->xlen) {
				debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
				    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
			    	    "DREG: read longer than buffer?");
			} else {
				odata = d->buf[d->bufpos ++];
				debugmsg_cpu(cpu, SUBSYS_DEVICE, "mb89352",
				    VERBOSITY_DEBUG,
			    	    "DREG: read from buffer: 0x%02x", (int)odata);

				if (d->bufpos >= d->xlen) {
					d->phase = 4;  // BUS_FREE
					d->reg[INTS] |= INTS_DISCON;
					d->reg[PSNS] &= ~7;
					d->reg[PSNS] |= d->phase;
					d->reg[PSNS] |= PSNS_REQ;
				}
			}
			
		}
		break;

	case TEMP:
	case TCH:
	case TCM:
	case TCL:
		if (writeflag == MEM_WRITE)
			d->reg[relative_addr / 4] = idata;
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

	reassert_interrupts(d);

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

	reset_controller(d);

	return 1;
}

