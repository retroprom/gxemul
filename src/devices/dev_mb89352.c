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
 *  loader), and openbsd/sys/arch/luna88k/dev/mb89352* (the kernel), and
 *  matching their expectations.
 *
 *  TODO: Probably lots of details. Most likely, running anything other than
 *        OpenBSD/luna88k as a guest OS will trigger unimplemented code
 *        paths.
 *
 *  TODO: OpenBSD complains about
 *        probe(spc0:0:0): Check Condition (error 0) on opcode 0x0
 *        during bootup.
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

#define MB89352_NREGS		0x10

const int MB89352_REGISTERS_LENGTH = MB89352_NREGS * 4;

const bool mb89352_abort_on_unimplemented_stuff = true;

static char *regname[16] = {
	"BDID", "SCTL", "SCMD", "TMOD", "INTS", "PSNS", "SSTS", "SERR",
	"PCTL", "MBC",  "DREG", "TEMP", "TCH",  "TCM",  "TCL",  "EXBF"
};


#define PH_BUS_FREE		4


struct mb89352_data {
	int			subsys;

	struct interrupt	irq;
	bool			irq_asserted;

	uint8_t			reg[MB89352_NREGS];
	
	int			target;		// target SCSI ID, e.g. 6
	int			phase;

	struct scsi_transfer	*xferp;
	size_t			transfer_count;
	size_t			transfer_bufpos;
};


void reg_debug(struct cpu* cpu, struct mb89352_data *d, int writeflag, int regnr, int data)
{
	if (!ENOUGH_VERBOSITY(d->subsys, VERBOSITY_DEBUG))
		return;

	if (writeflag == MEM_WRITE)
		debugmsg_cpu(cpu, d->subsys, "",
		    VERBOSITY_DEBUG, "WRITE to %s: 0x%02x", regname[regnr], data);
	else
		debugmsg_cpu(cpu, d->subsys, "",
		    VERBOSITY_DEBUG, "read from %s", regname[regnr]);
}			    


static void reassert_interrupts(struct mb89352_data *d)
{
	bool assert = d->reg[INTS] != 0;

	// TODO: BUS FREE interrupt enable: PCTL_BFINT_ENAB

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
	memset(d->reg, 0, sizeof(d->reg));

	d->reg[BDID] = 7;
	d->reg[SCTL] = SCTL_DISABLE;
}


DEVICE_TICK(mb89352)
{
	struct mb89352_data *d = (struct mb89352_data *) extra;
	reassert_interrupts(d);
}


int mb89352_dreg_read(struct cpu* cpu, struct mb89352_data *d, int writeflag)
{
	int odata;

	if (d->xferp == NULL) {
		debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_ERROR,
	    	    "DREG: no ongoing transfer to read from?");
	    	cpu->running = 0;
	    	return 0;
	}
	
	uint8_t *p = d->xferp->data_in;
	size_t len = d->xferp->data_in_len;
	
	switch (d->phase) {
	case PH_DATAIN:
		p = d->xferp->data_in;
		len = d->xferp->data_in_len;
		break;
	case PH_STAT:
		p = d->xferp->status;
		len = d->xferp->status_len;
		break;
	case PH_MSGIN:
		p = d->xferp->msg_in;
		len = d->xferp->msg_in_len;
		break;
	default:
		debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_ERROR,
	    	    "DREG: read in unimplemented phase %i", d->phase);
	    	cpu->running = 0;
	    	return 0;
	}

	if (d->transfer_bufpos >= len) {
		debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_ERROR,
	    	    "DREG: read longer than buffer?");
	    	cpu->running = 0;
	    	return 0;
	}

	odata = p[d->transfer_bufpos ++];

	debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_DEBUG,
    	    "DREG read: 0x%02x", (int)odata);

	if (d->transfer_bufpos < len)
		return odata;

	debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_DEBUG,
    	    "DREG read entire result completed.");

	d->reg[SSTS] &= ~SSTS_XFR;

	if (d->phase == PH_DATAIN)
		d->phase = PH_STAT;
	else if (d->phase == PH_STAT)
		d->phase = PH_MSGIN;
	else if (d->phase == PH_MSGIN)
		d->phase = PH_BUS_FREE;

	d->reg[PSNS] |= PSNS_REQ;

	if (d->phase == PH_BUS_FREE)
		d->reg[INTS] |= INTS_DISCON;
	else
		d->reg[INTS] |= INTS_CMD_DONE;

	return odata;
}


void mb89352_dreg_write(struct cpu* cpu, struct mb89352_data *d, int writeflag, int idata)
{
	if (d->xferp == NULL) {
		debugmsg_cpu(cpu, d->subsys, "",
		    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
	    	    "DREG: no ongoing transfer to write to?");
	    	return;
	}
	
	uint8_t *p = d->xferp->data_in;
	size_t len = d->xferp->data_in_len;
	
	switch (d->phase) {
	case PH_DATAOUT:
		p = d->xferp->data_out;
		len = d->xferp->data_out_len;
		break;
	case PH_CMD:
		p = d->xferp->cmd;
		len = d->xferp->cmd_len;
		break;
	default:
		debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_ERROR,
	    	    "DREG: write in unimplemented phase %i", d->phase);
	    	cpu->running = 0;
	    	return;
	}

	if (d->transfer_bufpos >= len) {
		debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_ERROR,
	    	    "DREG: write longer than buffer?");
	    	cpu->running = 0;
	    	return;
	}

	p[d->transfer_bufpos ++] = idata;

	if (d->transfer_bufpos < len)
		return;

	debugmsg_cpu(cpu, d->subsys, "",
	    VERBOSITY_DEBUG,
    	    "%i bytes written", d->transfer_bufpos);

	int res;

	switch (d->phase) {

	case PH_DATAOUT:
		res = diskimage_scsicommand(cpu,
	    	    d->target, DISKIMAGE_SCSI, d->xferp);

		// TODO: How about failure results?
		d->phase = PH_STAT;
		break;

	case PH_CMD:
		res = diskimage_scsicommand(cpu,
	    	    d->target, DISKIMAGE_SCSI, d->xferp);

		if (res == 2)
			d->phase = PH_DATAOUT;
		else if (d->xferp->data_in != NULL)
			d->phase = PH_DATAIN;
		else
			d->phase = PH_STAT;
		break;

	default:
		debugmsg_cpu(cpu, d->subsys, "", VERBOSITY_ERROR,
	    	    "DREG write: UNIMPLEMENTED PHASE %i", d->phase);
	    	cpu->running = 0;
	}
	
	d->reg[PSNS] |= PSNS_REQ;
	d->reg[SSTS] &= ~SSTS_XFR;
	d->reg[INTS] |= INTS_CMD_DONE;
}


DEVICE_ACCESS(mb89352)
{
	struct mb89352_data *d = (struct mb89352_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (len != 1) {
		debugmsg_cpu(cpu, d->subsys, "",
		    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
	    	    "unimplemented LEN: %i-bit access, address 0x%x: 0x%x",
	    	    len * 8,
	    	    (int) relative_addr);

		if (mb89352_abort_on_unimplemented_stuff) {
			cpu->running = 0;
			return 0;
		}
	}

	int regnr = (relative_addr >> 2) & (MB89352_NREGS - 1);

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);
	else
		odata = d->reg[regnr];

	reg_debug(cpu, d, writeflag, regnr, idata);

	switch (regnr) {

	case BDID:
		if (writeflag == MEM_WRITE) {
			d->reg[BDID] = idata;
			if (idata != 7)
				debugmsg_cpu(cpu, d->subsys, "",
				    VERBOSITY_INFO, "unimplemented BDID value: 0x%x",
			    	    (int) idata);
		} else {
			odata = (1 << d->reg[BDID]);
		}
		break;

	case SCTL:
		if (writeflag == MEM_WRITE) {
			d->reg[SCTL] = idata;

			if (idata & SCTL_DIAG)
				debugmsg_cpu(cpu, d->subsys, "",
				    VERBOSITY_ERROR,
			    	    "Diagnostics mode NOT IMPLEMENTED");

			if (idata & SCTL_CTRLRST) {
				debugmsg(d->subsys, "", VERBOSITY_INFO, "resetting controller");
				reset_controller(d);
			}
		}
		break;

	case SCMD:
		if (writeflag == MEM_WRITE) {
			d->reg[SCMD] = idata;
			
			// RST = Reset ?
			if (idata & SCMD_RST)
				idata = SCMD_BUS_REL;
			
			switch (idata) {

			case SCMD_BUS_REL:	// 0x00
				d->reg[SSTS] &= ~(SSTS_TARGET | SSTS_INITIATOR | SSTS_XFR);
				d->phase = PH_BUS_FREE;
				// TODO: BUS FREE interrupt enable: PCTL_BFINT_ENAB?
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
						debugmsg_cpu(cpu, d->subsys, "",
						    VERBOSITY_DEBUG,
					    	    "selecting target %i", target);
					else
						debugmsg_cpu(cpu, d->subsys, "",
						    VERBOSITY_WARNING,
					    	    "SCMD_SELECT with no target?");

					d->target = target;
					d->reg[INTS] |= INTS_CMD_DONE;
					d->reg[PSNS] &= ~7;
					d->phase = PH_CMD;
					d->reg[PSNS] |= PSNS_REQ;

					if (d->xferp != NULL)
						scsi_transfer_free(d->xferp);

					d->xferp = scsi_transfer_alloc();
					d->transfer_bufpos = 0;

					d->reg[SSTS] &= ~SSTS_TARGET;
					d->reg[SSTS] |= SSTS_BUSY;
				}
				break;

			case SCMD_XFR | SCMD_PROG_XFR:
			case SCMD_XFR | SCMD_PROG_XFR | SCMD_ICPT_XFR:
				{
					d->reg[SSTS] |= SSTS_XFR;
					d->phase = d->reg[PCTL] & 7;

					d->transfer_bufpos = 0;

					debugmsg_cpu(cpu, d->subsys, "",
					    VERBOSITY_DEBUG,
				    	    "Transfer command: phase %i, len %i",
				    	    d->phase, d->transfer_count);

					switch (d->phase) {
					case PH_DATAOUT:
						scsi_transfer_allocbuf(&d->xferp->data_out_len,
						    &d->xferp->data_out, d->transfer_count, 0);
						d->xferp->data_out_offset = d->transfer_count;
						break;
					case PH_DATAIN:
						break;
					case PH_CMD:
						scsi_transfer_allocbuf(&d->xferp->cmd_len,
						    &d->xferp->cmd, d->transfer_count, 0);
						break;
					case PH_STAT:
						break;
					case PH_MSGIN:
						break;
					default:
						debugmsg_cpu(cpu, d->subsys, "",
						    VERBOSITY_ERROR,
					    	    "Transfer command: unimplemented phase %i",
					    	    d->phase);
				    		exit(1);
					}

					if (!(d->phase & 1))
						d->reg[INTS] |= INTS_CMD_DONE;
				}
				break;

			case SCMD_RST_ACK:
				d->reg[SSTS] &= ~(SSTS_INITIATOR | SSTS_TARGET);
				d->reg[PSNS] &= ~PSNS_REQ;
				break;

			default:
				debugmsg_cpu(cpu, d->subsys, "",
				    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
			    	    "unimplemented SCMD: 0x%x", (int) idata);

				if (mb89352_abort_on_unimplemented_stuff) {
					cpu->running = 0;
					return 0;
				}
			}
		}
		break;

	case TMOD:
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata;
			if (idata != 0)
				debugmsg_cpu(cpu, d->subsys, "",
				    VERBOSITY_WARNING,
				    "unimplemented write to TMOD: 0x%02x",
			    	    (int) idata);
		}
		break;	

	case INTS:
		if (writeflag == MEM_WRITE) {
			int old = d->reg[INTS];
			d->reg[INTS] &= ~idata;
			if (old != d->reg[INTS])
				debugmsg_cpu(cpu, d->subsys, "",
				    VERBOSITY_INFO, "INTS: 0x%02x -> 0x%02x",
			    	    old, d->reg[INTS]);
		}
		break;	

	case PSNS:
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata;
			debugmsg_cpu(cpu, d->subsys, "",
			    VERBOSITY_WARNING,
		    	    "unimplemented write to PSNS/SDGC: 0x%x",
			    (int) idata);
		}

		odata &= ~7;
		odata |= d->phase;
		break;

	case SSTS:
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata;
			debugmsg_cpu(cpu, d->subsys, "",
			    VERBOSITY_WARNING,
		    	    "unimplemented write to SSTS: 0x%x",
			    (int) idata);
		}

		if (d->transfer_count == 0)
			odata |= SSTS_TCZERO;
		else
			odata &= ~SSTS_TCZERO;

		odata &= ~(SSTS_DREG_FULL | SSTS_DREG_EMPTY | SSTS_BUSY);

		// Inbound phase, but no more data? Then the dreg is empty.
		if (d->phase & 1) {
			if (d->transfer_count == 0)
				odata |= SSTS_DREG_EMPTY;
		}

		// Outbound phase, then the dreg is always empty.
		if (!(d->phase & 1)) {
			odata |= SSTS_DREG_EMPTY;
		}

		if (d->phase & 1 && d->transfer_count > 0)
			odata |= SSTS_BUSY;
		break;

	case SERR:
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata;
			debugmsg_cpu(cpu, d->subsys, "",
			    VERBOSITY_WARNING,
		    	    "unimplemented write to SERR: 0x%x",
			    (int) idata);
		}
		break;

	case PCTL:
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata;
			if (idata & 0x78)
				debugmsg_cpu(cpu, d->subsys, "",
				    VERBOSITY_WARNING,
			    	    "write to read-only bits of PCTL ignored: 0x%x",
				    (int) idata);
			d->reg[regnr] &= 0x87;
		}
		break;

	case MBC:
		if (writeflag == MEM_WRITE) {
			debugmsg_cpu(cpu, d->subsys, "",
			    VERBOSITY_WARNING,
		    	    "write to read-only MBC ignored: 0x%x",
			    (int) idata);
		}
		break;

	case DREG:
		if (d->transfer_count == 0) {
			debugmsg_cpu(cpu, d->subsys, "",
			    VERBOSITY_WARNING,
		    	    "DREG %s, but transfer count = 0!",
		    	    writeflag == MEM_WRITE ? "WRITE" : "READ");
		} else {
			d->transfer_count --;

			if (writeflag == MEM_WRITE)
				mb89352_dreg_write(cpu, d, writeflag, idata);
			else
				odata = mb89352_dreg_read(cpu, d, writeflag);
		}
		break;

	case TEMP:
		if (writeflag == MEM_WRITE)
			d->reg[regnr] = idata;
		break;

	case TCH:
	case TCM:
	case TCL:
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata;

			// Update d->transfer_count to be in sync with the registers.
			d->transfer_count = (d->reg[TCH] << 16) + (d->reg[TCM] << 8) + d->reg[TCL];
		} else {
			d->reg[TCH] = d->transfer_count >> 16;
			d->reg[TCM] = d->transfer_count >> 8;
			d->reg[TCL] = d->transfer_count;
			odata = d->reg[regnr];
		}
		break;

	default:
		if (writeflag == MEM_WRITE)
			debugmsg_cpu(cpu, d->subsys, "",
			    mb89352_abort_on_unimplemented_stuff ? VERBOSITY_ERROR : VERBOSITY_WARNING,
		    	    "unimplemented %i-bit WRITE to address 0x%x: 0x%x",
		    	    len * 8,
			    (int) relative_addr,
			    (int) idata);
		else
			debugmsg_cpu(cpu, d->subsys, "",
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

	d->subsys = debugmsg_register_subsystem("mb89352");

	INTERRUPT_CONNECT(devinit->interrupt_path, d->irq);

	machine_add_tickfunction(devinit->machine, dev_mb89352_tick, d, TICK_STEPS_SHIFT);

	reset_controller(d);

	return 1;
}

