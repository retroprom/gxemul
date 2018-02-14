/*
 *  Copyright (C) 2018  Anders Gavare.  All rights reserved.
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
 *  COMMENT: OMRON Luna 88K-specific devices and control registers
 *
 *  Almost everything in here is just dummy code which returns nonsense,
 *  just enough to fake hardware well enough to get OpenBSD/luna88k to
 *  not stop early during bootup. It does not really work yet.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "console.h"
#include "device.h"
#include "devices.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/luna88k_board.h"
#include "thirdparty/m8820x.h"

#define	LUNA88K_REGISTERS_BASE		0x3ffffff0UL
#define	LUNA88K_REGISTERS_END		0xff000000UL
#define	LUNA88K_REGISTERS_LENGTH	(LUNA88K_REGISTERS_END - LUNA88K_REGISTERS_BASE)

#define	MAX_CPUS	4


#define	BCD(x) ((((x) / 10) << 4) + ((x) % 10))


struct luna88k_data {
	struct vfb_data *fb;

	uint32_t	interrupt_status[MAX_CPUS];
	uint32_t	software_interrupt_status[MAX_CPUS];

	/*  Two channels.  */
	int		obio_sio_regno[2];
	uint8_t		obio_sio_reg[2][256];

	uint32_t	fuse_rom[FUSE_ROM_SPACE / sizeof(uint32_t)];
	uint8_t		nvram[NVRAM_SPACE];
};


static void swapBitOrder(uint8_t* data, int len)
{
	for (int bo = 0; bo < len; bo ++)
	{
		uint8_t b = (uint8_t)data[bo];
		uint8_t c = 0x00;
		for (int i = 0; i < 8; i++)
		{
			if (b & (128 >> i))
				c |= (1 << i);
		}

		data[bo] = c;
	}
}


DEVICE_ACCESS(luna88k)
{
	struct tm *tmp;
	time_t timet;
 	uint32_t addr = relative_addr + LUNA88K_REGISTERS_BASE;
	uint64_t idata = 0, odata = 0;
	struct luna88k_data *d = (struct luna88k_data *) extra;
	int cpunr;
	int sio_devnr;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

	if (addr >= FUSE_ROM_ADDR && len == sizeof(uint32_t) &&
	    addr < FUSE_ROM_ADDR + FUSE_ROM_SPACE) {
		if (writeflag == MEM_READ) {
			odata = d->fuse_rom[(addr - FUSE_ROM_ADDR) / sizeof(uint32_t)];
			memory_writemax64(cpu, data, len, odata);
		} else {
			d->fuse_rom[(addr - FUSE_ROM_ADDR) / sizeof(uint32_t)] = idata;
		}
		return 1;
	}

	if (addr >= FUSE_ROM_ADDR && len == sizeof(uint8_t) &&
	    addr < FUSE_ROM_ADDR + FUSE_ROM_SPACE) {
		if (writeflag == MEM_READ) {
			odata = d->fuse_rom[(addr - FUSE_ROM_ADDR) / sizeof(uint32_t)];
			odata >>= ((3 - (addr & 3)) * 8);
			memory_writemax64(cpu, data, len, odata);
		} else {
			fatal("TODO: luna88k byte write to fuse\n");
		}
		return 1;
	}

	if (addr >= NVRAM_ADDR && addr < NVRAM_ADDR + NVRAM_SPACE && len == sizeof(uint8_t)) {
		if (writeflag == MEM_READ) {
			odata = d->nvram[addr - NVRAM_ADDR];
			memory_writemax64(cpu, data, len, odata);
		} else {
			d->nvram[addr - NVRAM_ADDR] = idata;
		}
		return 1;
	}

	if (addr >= NVRAM_ADDR_88K2 && addr < NVRAM_ADDR_88K2 + NVRAM_SPACE && len == sizeof(uint8_t)) {
		if (writeflag == MEM_READ) {
			odata = d->nvram[addr - NVRAM_ADDR_88K2];
			memory_writemax64(cpu, data, len, odata);
		} else {
			d->nvram[addr - NVRAM_ADDR_88K2] = idata;
		}
		return 1;
	}

	if (addr >= BMAP_BMP && addr < BMAP_BMP + 0x40000) {
		size_t s = 1280 * 1024 / 8;
		addr -= (uint64_t)(uint32_t)BMAP_BMP;

		swapBitOrder(data, len);
		if (addr + len - 1 < s) {
			dev_fb_access(cpu, cpu->mem, addr, data, len, writeflag, d->fb);
			swapBitOrder(data, len);
			return 1;
		}
		else
			return 1;
	}

	if (addr >= BMAP_BMAP0 && addr < BMAP_BMAP0 + 0x40000) {
		size_t s = 1280 * 1024 / 8;
		addr -= (uint64_t)(uint32_t)BMAP_BMAP0;
		swapBitOrder(data, len);
		if (addr + len - 1 < s) {
			dev_fb_access(cpu, cpu->mem, addr, data, len, writeflag, d->fb);
			swapBitOrder(data, len);
			return 1;
		}
		else
			return 1;
	}

	if (addr >= BMAP_PALLET2 && addr < BMAP_PALLET2 + 16) {
		/*  Ignore for now.  */
		return 1;
	}

	if (addr >= TRI_PORT_RAM && addr < TRI_PORT_RAM + TRI_PORT_RAM_SPACE) {
		/*  Ignore for now.  */
		return 1;
	}

	switch (addr) {

	case 0x3ffffff0:
		/*  Accessed by OpenBSD/luna88k to trigger an illegal address  */
		cpu->cd.m88k.cmmu[1]->reg[CMMU_PFSR] = CMMU_PFSR_BERROR << 16;
		break;

	case PROM_ADDR:		/*  0x41000000  */
		/*  OpenBSD/luna88k write here during boot. Ignore for now. (?)  */
		break;

	case OBIO_CAL_CTL:	/*  calendar control register  */
		break;
	case OBIO_CAL_SEC:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_sec) << 24;
		break;
	case OBIO_CAL_MIN:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_min) << 24;
		break;
	case OBIO_CAL_HOUR:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_hour) << 24;
		break;
	case OBIO_CAL_DOW:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_wday + 0) << 24;
		break;
	case OBIO_CAL_DAY:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_mday) << 24;
		break;
	case OBIO_CAL_MON:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_mon + 1) << 24;
		break;
	case OBIO_CAL_YEAR:
		timet = time(NULL); tmp = gmtime(&timet);
		odata = BCD(tmp->tm_year - 1990) << 24;
		break;

	case OBIO_PIO0A:	/*  0x49000000: PIO-0 port A  */
		/*  OpenBSD reads dipswitch settings from PIO0A and B.  */
		odata = 0;	// high byte
		if (cpu->machine->x11_md.in_use)
			odata |= 0x40;
		odata |= 0x80;	// multi-user mode
		odata |= 0x20;	// don't ask name
		odata |= 0x10;	// don't do manual UKC config
		break;

	case OBIO_PIO0B:	/*  0x49000004: PIO-0 port B  */
		/*  OpenBSD reads dipswitch settings from PIO0A and B.  */
		odata = 0x00;	// low byte
		break;

	case OBIO_PIO0:		/*  0x4900000C: PIO-0 control  */
		/*  TODO: Implement for real.  */
		break;

	case OBIO_PIO1A:	/*  0x4d000000: PIO-0 port A  */
	case OBIO_PIO1B:	/*  0x4d000004: PIO-0 port B  */
	case OBIO_PIO1:		/*  0x4d00000C: PIO-0 control  */
		/*  Ignore for now. (?)  */
		break;

	case OBIO_SIO + 0:	/*  0x51000000: data channel 0 */
	case OBIO_SIO + 4:	/*  0x51000004: cmd channel 0 */
	case OBIO_SIO + 8:	/*  0x51000008: data channel 1 */
	case OBIO_SIO + 0xc:	/*  0x5100000c: cmd channel 1 */
		sio_devnr = (addr - OBIO_SIO) / 8;

		if ((addr - OBIO_SIO) & 4) {
			/*  cmd  */

			/*  TODO: Slightly similar to dev_scc.cc ?  */
			if (writeflag == MEM_WRITE) {
				if (d->obio_sio_regno[sio_devnr] == 0) {
					d->obio_sio_regno[sio_devnr] = idata;
				} else {
					d->obio_sio_reg[sio_devnr][d->obio_sio_regno[sio_devnr] & 255] = idata;
					d->obio_sio_regno[sio_devnr] = 0;
				}
			} else {
				d->obio_sio_reg[sio_devnr][0] = 0x04;
				d->obio_sio_reg[sio_devnr][1] = 0x2c;
				odata = d->obio_sio_reg[sio_devnr][d->obio_sio_regno[sio_devnr] & 255];
				d->obio_sio_regno[sio_devnr] = 0;
			}
		} else {
			/*  data  */
			if (writeflag == MEM_WRITE) {
				if (sio_devnr == 0)
					console_putchar(cpu->machine->main_console_handle, idata);
				else
					fatal("[ luna88k sio dev1 write data: TODO ]\n");
			} else {
				fatal("[ luna88k sio dev0 read data: TODO ]\n");
			}
		}

		break;

	case INT_ST_MASK0:	/*  0x65000000: Interrupt status CPU 0.  */
	case INT_ST_MASK1:	/*  0x65000004: Interrupt status CPU 1.  */
	case INT_ST_MASK2:	/*  0x65000008: Interrupt status CPU 2.  */
	case INT_ST_MASK3:	/*  0x6500000c: Interrupt status CPU 3.  */
		cpunr = (addr - INT_ST_MASK0) / 4;
		odata = d->interrupt_status[cpunr];
		if (writeflag) {
			if ((idata & 0x03ffffff) != 0x00000000) {
				fatal("[ TODO: luna88k interrupts, idata = 0x%08x ]\n", (uint32_t)idata);
				exit(1);
			}

			// It seems that the top 6 bits correspond to
			// interrupt 7 through 2, in some odd manner.
			// TODO.
		}

		// TODO.
		odata = (random() & 0xfc) << 24;

		break;

	case SOFT_INT0:		/*  0x69000000: Software Interrupt status CPU 0.  */
	case SOFT_INT1:		/*  0x69000004: Software Interrupt status CPU 1.  */
	case SOFT_INT2:		/*  0x69000008: Software Interrupt status CPU 2.  */
	case SOFT_INT3:		/*  0x6900000c: Software Interrupt status CPU 3.  */
		cpunr = (addr - SOFT_INT0) / 4;
		odata = d->software_interrupt_status[cpunr];

		// Reading status clears it.
		d->software_interrupt_status[cpunr] = 0;

		if (writeflag == MEM_WRITE) {
			fatal("TODO: luna88k write to software interrupts\n");
			exit(1);
		}
		break;

	case BMAP_RFCNT:	/*  0xb1000000: RFCNT register  */
	case BMAP_BMSEL:	/*  0xb1000000: BMSEL register  */
		/*  Ignore for now. (?)  */
		break;

	case BMAP_BMAP1:	/*  0xb1100000: Bitmap plane 1  */
		odata = 0xc0dec0de;
		/*  Return dummy value. OpenBSD writes and reads to detect presence
		    of bitplanes.  */
		break;

	case BMAP_FN + 0x14:	/*  0xb12c0014: "common bitmap function"  */
		/*  Function 5 is "ROP copy", according to OpenBSD sources.  */
		break;

	case SCSI_ADDR + 0x00:	/*  0xe1000000: SCSI ..  */
	case SCSI_ADDR + 0x04:	/*  0xe1000004: SCSI ..  */
	case SCSI_ADDR + 0x08:	/*  0xe1000008: SCSI ..  */
	case SCSI_ADDR + 0x0C:	/*  0xe100000C: SCSI ..  */
	case SCSI_ADDR + 0x20:	/*  0xe1000020: SCSI ..  */
	case SCSI_ADDR + 0x2c:	/*  0xe100002c: SCSI ..  */
	case SCSI_ADDR + 0x30:	/*  0xe1000030: SCSI ..  */
	case SCSI_ADDR + 0x34:	/*  0xe1000034: SCSI ..  */
	case SCSI_ADDR + 0x38:	/*  0xe1000038: SCSI ..  */
	case SCSI_ADDR + 0x40:	/*  0xe1000040: SCSI ..  */
	case SCSI_ADDR + 0x44:	/*  0xe1000044: SCSI ..  */
	case SCSI_ADDR + 0x48:	/*  0xe1000048: SCSI ..  */
	case SCSI_ADDR + 0x4c:	/*  0xe100004c: SCSI ..  */
	case SCSI_ADDR + 0x50:	/*  0xe1000050: SCSI ..  */
	case SCSI_ADDR + 0x60:	/*  0xe1000060: SCSI ..  */
	case SCSI_ADDR + 0x6c:	/*  0xe100006c: SCSI ..  */
	case SCSI_ADDR + 0x70:	/*  0xe1000070: SCSI ..  */
	case SCSI_ADDR + 0x74:	/*  0xe1000074: SCSI ..  */
	case SCSI_ADDR + 0x78:	/*  0xe1000078: SCSI ..  */
		/*  MB89352 SCSI Protocol Controller  */
		/*  Ignore for now. (?)  */
		break;

	case SCSI_ADDR + 0x10:	/*  0xe1000010: SCSI INTS  */
		odata = 0xffffffff;
		break;

	case 0xf1000000:	/*  Lance Ethernet. TODO.  */
	case 0xf1000004:
	case 0xf1000008:
		break;

	default:fatal("[ luna88k: unimplemented %s address 0x%x",
		    writeflag == MEM_WRITE? "write to" : "read from",
		    (int) addr);
		if (writeflag == MEM_WRITE)
			fatal(": 0x%x", (int)idata);
		fatal(" (%i bits) ]\n", len * 8);
		exit(1);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


void add_cmmu_for_cpu(struct devinit* devinit, int cpunr, uint32_t iaddr, uint32_t daddr)
{
	char tmpstr[300];
	struct m8820x_cmmu *cmmu;

	/*  Instruction CMMU:  */
	CHECK_ALLOCATION(cmmu = (struct m8820x_cmmu *) malloc(sizeof(struct m8820x_cmmu)));
	memset(cmmu, 0, sizeof(struct m8820x_cmmu));

	if (cpunr < devinit->machine->ncpus)
		devinit->machine->cpus[cpunr]->cd.m88k.cmmu[0] = cmmu;

	/*  This is a 88200, revision 9:  */
	cmmu->reg[CMMU_IDR] = (M88200_ID << 21) | (9 << 16);
	snprintf(tmpstr, sizeof(tmpstr), "m8820x addr=0x%x addr2=0", iaddr);
	device_add(devinit->machine, tmpstr);

	/*  ... and data CMMU:  */
	CHECK_ALLOCATION(cmmu = (struct m8820x_cmmu *) malloc(sizeof(struct m8820x_cmmu)));
	memset(cmmu, 0, sizeof(struct m8820x_cmmu));

	if (cpunr < devinit->machine->ncpus)
		devinit->machine->cpus[cpunr]->cd.m88k.cmmu[1] = cmmu;

	/*  This is also a 88200, revision 9:  */
	cmmu->reg[CMMU_IDR] = (M88200_ID << 21) | (9 << 16);
	cmmu->batc[8] = BATC8;
	cmmu->batc[9] = BATC9;
	snprintf(tmpstr, sizeof(tmpstr), "m8820x addr=0x%x addr2=1", daddr);
	device_add(devinit->machine, tmpstr);
}


DEVINIT(luna88k)
{
	struct luna88k_data *d;

	CHECK_ALLOCATION(d = (struct luna88k_data *) malloc(sizeof(struct luna88k_data)));
	memset(d, 0, sizeof(struct luna88k_data));


	memory_device_register(devinit->machine->memory, devinit->name,
	    LUNA88K_REGISTERS_BASE, LUNA88K_REGISTERS_LENGTH, dev_luna88k_access, (void *)d,
	    DM_DEFAULT, NULL);

	if (devinit->machine->x11_md.in_use)
	{
		d->fb = dev_fb_init(devinit->machine, devinit->machine->memory,
			0x100000000ULL + BMAP_BMAP0, VFB_GENERIC,
			1280, 1024, 2048, 1024, 1, "LUNA 88K");
	}

	if (devinit->machine->ncpus > 4)
	{
		printf("LUNA 88K can't have more than 4 CPUs.\n");
		exit(1);
	}

	add_cmmu_for_cpu(devinit, 0, CMMU_I0, CMMU_D0);
	add_cmmu_for_cpu(devinit, 1, CMMU_I1, CMMU_D1);
	add_cmmu_for_cpu(devinit, 2, CMMU_I2, CMMU_D2);
	add_cmmu_for_cpu(devinit, 3, CMMU_I3, CMMU_D3);

	return 1;
}

