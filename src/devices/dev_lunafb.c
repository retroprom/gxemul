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
 *  COMMENT: LUNA framebuffer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/hitachi_hm53462_rop.h"
#include "thirdparty/luna88k_board.h"


// Unfortunately, the framebuffer device isn't really meant to have an 8 byte
// offset into framebuffer memory, which OpenBSD seems to use. So for now,
// dyntrans access to the framebuffer memory is disabled.
static bool use_dyntrans = false;



struct lunafb_data {
	struct vfb_data *fb;
};


DEVICE_ACCESS(lunafb_a)
{
 	uint32_t addr = relative_addr + BMAP_START;
	uint64_t idata = 0, odata = 0;
	// struct lunafb_data *d = (struct lunafb_data *) extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	switch (addr) {

	case BMAP_RFCNT:	/*  0xb1000000: RFCNT register  */
		/*  video h-origin/v-origin, according to OpenBSD  */
		/*  Ignore for now. (?)  */
		break;

	case BMAP_BMSEL:	/*  0xb1000000: BMSEL register  */
		/*  Ignore for now. Used by OpenBSD's omfb_clear_framebuffer.  */
		break;

	default:
		if (writeflag == MEM_WRITE)
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "lunafb_a", VERBOSITY_ERROR,
		    	    "unimplemented %i-bit WRITE to address 0x%x: 0x%x",
		    	    len * 8,
			    (int) addr,
			    (int) idata);
		else
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "lunafb_a", VERBOSITY_ERROR,
		    	    "unimplemented %i-bit READ from address 0x%x",
		    	    len * 8,
			    (int) addr);

		// Stop the emulation immediately.
		cpu->running = 0;
		return 0;
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVICE_ACCESS(lunafb_bmp)
{
	struct lunafb_data *d = (struct lunafb_data *) extra;

	if (!use_dyntrans && relative_addr >= 8)
		relative_addr -= 8;

	if (relative_addr + len - 1 < 0x40000)
		dev_fb_access(cpu, cpu->mem, relative_addr, data, len, writeflag, d->fb);

	return 1;
}


DEVICE_ACCESS(lunafb_bmap0)
{
	struct lunafb_data *d = (struct lunafb_data *) extra;

	if (!use_dyntrans && relative_addr >= 8)
		relative_addr -= 8;

	if (relative_addr + len - 1 < 0x40000)
		dev_fb_access(cpu, cpu->mem, relative_addr, data, len, writeflag, d->fb);

	return 1;
}


DEVICE_ACCESS(lunafb_b)
{
 	uint32_t addr = relative_addr + BMAP_BMAP1;
	uint64_t idata = 0, odata = 0;
	// struct lunafb_data *d = (struct lunafb_data *) extra;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (addr >= BMAP_PALLET2 && addr < BMAP_PALLET2 + 16) {
		/*  Ignore for now.  */
		return 1;
	}

	switch (addr) {

	case BMAP_BMAP1:	/*  0xb1100000: Bitmap plane 1  */
		/*  Return a dummy value. OpenBSD writes and reads to
		    detect presence of bitplanes.  */
		odata = 0;
		break;

	case BMAP_FN + 4 * ROP_THROUGH:	/*  0xb12c0014: "common bitmap function"  */
		/*  Function 5 is "ROP copy", according to OpenBSD sources.  */
		/*  See hitachi_hm53462_rop.h  */
		if (writeflag == MEM_READ) {
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "lunafb_b", VERBOSITY_ERROR,
		    	    "TODO: lunafb READ from BMAP_FN ROP register");
			cpu->running = 0;
			return 0;
		}
		if (idata != 0xffffffff) {
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "lunafb_b", VERBOSITY_ERROR,
		    	    "TODO: lunafb write which does not set ALL bits");
			cpu->running = 0;
			return 0;
		}
		break;

	default:
		if (writeflag == MEM_WRITE)
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "lunafb_b", VERBOSITY_ERROR,
		    	    "unimplemented %i-bit WRITE to address 0x%x: 0x%x",
		    	    len * 8,
			    (int) addr,
			    (int) idata);
		else
			debugmsg_cpu(cpu, SUBSYS_DEVICE, "lunafb_b", VERBOSITY_ERROR,
		    	    "unimplemented %i-bit READ from address 0x%x",
		    	    len * 8,
			    (int) addr);

		// Stop the emulation immediately.
		cpu->running = 0;
		return 0;
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


DEVINIT(lunafb)
{
	struct lunafb_data *d;

	CHECK_ALLOCATION(d = (struct lunafb_data *) malloc(sizeof(struct lunafb_data)));
	memset(d, 0, sizeof(struct lunafb_data));

	/*
	 *  OpenBSD/luna88k uses both BMAP_BMP _and_ BMAP_BMAP0. During
	 *  normal bootup, it uses both approximately the same amount.
	 *  However, when running X11, it seems to use BMAP_BMAP0 more.
	 *  So, BMAP0 is the one that is used for the actual framebuffer
	 *  (meaning that fast dyntrans accesses to those pages is possible),
	 *  and a slow "forwarding device" at BMAP_BMP is also registered.
	 *
	 *  Unfortunately, this means that the rest of the registers are
	 *  split up into two parts, lunafb_a and lunafb_b.
	 */

	d->fb = dev_fb_init(devinit->machine, devinit->machine->memory,
		use_dyntrans ? BMAP_BMAP0 : 0x2ff000000, VFB_REVERSEBITS,
		1280, 1024, 2048, 1024, 1, "LUNA 88K");

	memory_device_register(devinit->machine->memory, "lunafb_a",
	    devinit->addr, 0x80000, dev_lunafb_a_access, (void *)d,
	    DM_DEFAULT, NULL);

	memory_device_register(devinit->machine->memory, "lunafb_bmp",
	    BMAP_BMP, BMAP_BMAP0 - BMAP_BMP, dev_lunafb_bmp_access, (void *)d,
	    DM_DEFAULT, NULL);

	if (!use_dyntrans)
		memory_device_register(devinit->machine->memory, "lunafb_bmap0",
		    BMAP_BMAP0, BMAP_BMAP1 - BMAP_BMAP0, dev_lunafb_bmap0_access, (void *)d,
		    DM_DEFAULT, NULL);

	memory_device_register(devinit->machine->memory, "lunafb_b",
	    BMAP_BMAP1, SCSI_ADDR - BMAP_BMAP1, dev_lunafb_b_access, (void *)d,
	    DM_DEFAULT, NULL);

	return 1;
}


