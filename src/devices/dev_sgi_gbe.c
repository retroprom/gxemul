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
 *  $Id: dev_sgi_gbe.c,v 1.3 2004-01-06 10:49:25 debug Exp $
 *  
 *  SGI "gbe", whatever that means :)  Perhaps something to do with
 *  graphics. Framebuffer. Something.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "misc.h"
#include "console.h"
#include "devices.h"


struct sgi_gbe_data {
	unsigned char	reg[DEV_SGI_GBE_LENGTH];
};


/*
 *  dev_sgi_gbe_access():
 *
 *  Returns 1 if ok, 0 on error.
 */
int dev_sgi_gbe_access(struct cpu *cpu, struct memory *mem, uint64_t relative_addr, unsigned char *data, size_t len, int writeflag, void *extra)
{
	int i;
	struct sgi_gbe_data *d = extra;

	if (writeflag == MEM_WRITE)
		memcpy(&d->reg[relative_addr], data, len);
	else
		memcpy(data, &d->reg[relative_addr], len);

	/*
	 *  Linux/sgimips seems to write palette data to offset 0x50000 - 0x503xx.
	 */

	switch (relative_addr) {
	default:
		if (writeflag==MEM_READ) {
			debug("[ sgi_gbe: read from 0x%x, len=%i ]\n", (int)relative_addr, len);
		} else {
			debug("[ sgi_gbe: write to 0x%x:", (int)relative_addr);
			for (i=0; i<len; i++)
				debug(" %02x", data[i]);
			debug(" (len=%i) ]\n", len);
		}
	}

	return 1;
}


/*
 *  dev_sgi_gbe_init():
 */
void dev_sgi_gbe_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_gbe_data *d;

	d = malloc(sizeof(struct sgi_gbe_data));
	if (d == NULL) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}
	memset(d, 0, sizeof(struct sgi_gbe_data));

	memory_device_register(mem, "sgi_gbe", baseaddr, DEV_SGI_GBE_LENGTH, dev_sgi_gbe_access, d);
}

