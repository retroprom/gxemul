/*
 *  Copyright (C) 2004 by Anders Gavare.  All rights reserved.
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
 *  $Id: dev_zs.c,v 1.2 2004-01-05 01:24:02 debug Exp $
 *  
 *  Zilog serial controller, used by (at least) the SGI emulation mode.
 *
 *  TODO:  Implement this correctly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "console.h"
#include "devices.h"


struct zs_data {
	int		irq_nr;
	int		addrmult;
};


/*
 *  dev_zs_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_zs_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	struct zs_data *d = extra;
	int idata = 0, odata=0, odata_set=0;

	/*  Switch byte order for incoming data, if neccessary:  */
	if (cpu->byte_order == EMUL_BIG_ENDIAN)
		for (i=0; i<len; i++) {
			idata <<= 8;
			idata |= data[i];
		}
	else
		for (i=len-1; i>=0; i--) {
			idata <<= 8;
			idata |= data[i];
		}

	relative_addr /= d->addrmult;

	switch (relative_addr) {
	case 3:
		if (writeflag==MEM_READ) {
			/*  debug("[ zs: read from 0x%08lx ]\n", (long)relative_addr);  */
			odata_set = 1;
			odata = 0x04;	/*  ??? 0x04 allows transmission  */
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
		break;
	case 7:
		if (writeflag==MEM_READ) {
			/*  debug("[ zs: read from 0x%08lx ]\n", (long)relative_addr);  */
			odata_set = 1;
			if (console_charavail())
				odata = console_readchar();
			else
				odata = 0;
		} else {
			/*  debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);  */
			console_putchar(idata & 255);
		}
		break;
	default:
		if (writeflag==MEM_READ) {
			debug("[ zs: read from 0x%08lx ]\n", (long)relative_addr);
			odata_set = 1;
		} else {
			debug("[ zs: write to  0x%08lx: 0x%08x ]\n", (long)relative_addr, idata);
		}
	}

	if (odata_set) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN) {
			for (i=0; i<len; i++)
				data[i] = (odata >> (i*8)) & 255;
		} else {
			for (i=0; i<len; i++)
				data[len - 1 - i] = (odata >> (i*8)) & 255;
		}
	}

	return 1;
}


/*
 *  dev_zs_init():
 */
void dev_zs_init(struct cpu *cpu, struct memory *mem, uint64_t baseaddr, int irq_nr, int addrmult)
{
	struct zs_data *d;

	d = malloc(sizeof(struct zs_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct zs_data));
	d->irq_nr   = irq_nr;
	d->addrmult = addrmult;

	memory_device_register(mem, "zs", baseaddr, DEV_ZS_LENGTH * addrmult, dev_zs_access, d);
}

