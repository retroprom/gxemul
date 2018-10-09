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
 *  COMMENT: SGI "Graphics Back End", graphics controller + framebuffer
 *
 *  Guesswork, based on how Linux and NetBSD use the graphics on the SGI O2.
 *  Using NetBSD terminology (from crmfbreg.h):
 *
 *	0x15001000	rendering engine
 *	0x15002000	drawing engine
 *	0x15003000	memory transfer engine
 *	0x15004000	status registers for drawing engine
 *	0x16000000	crm (or gbe) framebuffer control
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "cpu.h"
#include "devices.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"

#include "thirdparty/crmfbreg.h"


/*  Let's hope nothing is there already...  */
#define	FAKE_GBE_FB_ADDRESS	0x38000000

// #define	GBE_DEBUG
// #define debug fatal

#define MTE_TEST

#define	GBE_DEFAULT_XRES		640
#define	GBE_DEFAULT_YRES		480


struct sgi_gbe_data {
	int		xres, yres;

	uint32_t	control;		/* 0x00000  */
	uint32_t	dotclock;		/* 0x00004  */
	uint32_t	i2c;			/* 0x00008  */
	uint32_t	i2cfp;			/* 0x00010  */
	uint32_t	plane0ctrl;		/* 0x30000  */
	uint32_t	frm_control;		/* 0x3000c  */
	int		freeze;

	int		bitdepth;
	struct vfb_data *fb_data;
};


/*
 *  dev_sgi_gbe_tick():
 *
 *  Every now and then, copy data from the framebuffer in normal ram
 *  to the actual framebuffer (which will then redraw the window).
 *  TODO:  This is utterly slow, even slower than the normal framebuffer
 *  which is really slow as it is.
 *
 *  frm_control (bits 31..9) is a pointer to an array of uint16_t.
 *  These numbers (when << 16 bits) are pointers to the tiles. Tiles are
 *  512x128 in 8-bit mode, 256x128 in 16-bit mode, and 128x128 in 32-bit mode.
 */
DEVICE_TICK(sgi_gbe)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	int tile_nr = 0, on_screen = 1, xbase = 0, ybase = 0;
	unsigned char tileptr_buf[sizeof(uint16_t)];
	uint64_t tileptr, tiletable;
	int lines_to_copy, pixels_per_line, y;
	unsigned char buf[16384];	/*  must be power of 2, at most 65536 */
	int copy_len, copy_offset;
	uint64_t old_fb_offset = 0;
	int tweaked = 1;

#ifdef MTE_TEST
/*  Actually just a return, but this fools the Compaq compiler...  */
if (cpu != NULL)
return;
#endif

	debug("[ sgi_gbe: dev_sgi_gbe_tick() ]\n");

	tiletable = (d->frm_control & 0xfffffe00);
	if (tiletable == 0)
		on_screen = 0;
/*
tweaked = 0;
*/
	while (on_screen) {
		/*  Get pointer to a tile:  */
		cpu->memory_rw(cpu, cpu->mem, tiletable +
		    sizeof(tileptr_buf) * tile_nr,
		    tileptr_buf, sizeof(tileptr_buf), MEM_READ,
		    NO_EXCEPTIONS | PHYSICAL);
		tileptr = 256 * tileptr_buf[0] + tileptr_buf[1];
		/*  TODO: endianness  */
		tileptr <<= 16;

		/*  tileptr is now a physical address of a tile.  */
		debug("[ sgi_gbe:   tile_nr = %2i, tileptr = 0x%08lx, xbase"
		    " = %4i, ybase = %4i ]\n", tile_nr, tileptr, xbase, ybase);

		if (tweaked) {
			/*  Tweaked (linear) mode:  */

			/*
			 *  Copy data from this 64KB physical RAM block to the
			 *  framebuffer:
			 *
			 *  NOTE: Copy it in smaller chunks than 64KB, in case
			 *        the framebuffer device can optimize away
			 *        portions that aren't modified that way.
			 */
			copy_len = sizeof(buf);
			copy_offset = 0;

			while (on_screen && copy_offset < 65536) {
				if (old_fb_offset + copy_len > (uint64_t)
				    (d->xres * d->yres * d->bitdepth / 8)) {
					copy_len = d->xres * d->yres *
					    d->bitdepth / 8 - old_fb_offset;
					/*  Stop after copying this block...  */
					on_screen = 0;
				}

				/*  debug("old_fb_offset = %08x copylen"
				    "=%i\n", old_fb_offset, copy_len);  */

				cpu->memory_rw(cpu, cpu->mem, tileptr +
				    copy_offset, buf, copy_len, MEM_READ,
				    NO_EXCEPTIONS | PHYSICAL);
				dev_fb_access(cpu, cpu->mem, old_fb_offset,
				    buf, copy_len, MEM_WRITE, d->fb_data);
				copy_offset += sizeof(buf);
				old_fb_offset += sizeof(buf);
			}
		} else {
			/*  This is for non-tweaked (tiled) mode. Not really
			    tested with correct image data, but might work:  */

			lines_to_copy = 128;
			if (ybase + lines_to_copy > d->yres)
				lines_to_copy = d->yres - ybase;

			pixels_per_line = 512 * 8 / d->bitdepth;
			if (xbase + pixels_per_line > d->xres)
				pixels_per_line = d->xres - xbase;

			for (y=0; y<lines_to_copy; y++) {
				cpu->memory_rw(cpu, cpu->mem, tileptr + 512 * y,
				    buf, pixels_per_line * d->bitdepth / 8,
				    MEM_READ, NO_EXCEPTIONS | PHYSICAL);
#if 0
{
int i;
for (i=0; i<pixels_per_line * d->bitdepth / 8; i++)
	buf[i] ^= (random() & 0x20);
}
#endif
				dev_fb_access(cpu, cpu->mem, ((ybase + y) *
				    d->xres + xbase) * d->bitdepth / 8,
				    buf, pixels_per_line * d->bitdepth / 8,
				    MEM_WRITE, d->fb_data);
			}

			/*  Go to next tile:  */
			xbase += (512 * 8 / d->bitdepth);
			if (xbase >= d->xres) {
				xbase = 0;
				ybase += 128;
				if (ybase >= d->yres)
					on_screen = 0;
			}
		}

		/*  Go to next tile:  */
		tile_nr ++;
	}

	/*  debug("[ sgi_gbe: dev_sgi_gbe_tick() end]\n");  */
}


DEVICE_ACCESS(sgi_gbe)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;

	if (writeflag == MEM_WRITE)
		idata = memory_readmax64(cpu, data, len);

#ifdef GBE_DEBUG
	if (writeflag == MEM_WRITE)
		debug("[ sgi_gbe: DEBUG: write to address 0x%llx, data"
		    "=0x%llx ]\n", (long long)relative_addr, (long long)idata);
#endif

	switch (relative_addr) {

	case CRMFB_CTRLSTAT:	// 0x0
		if (writeflag == MEM_WRITE)
			d->control = idata;
		else
			odata = d->control;
		break;

	case CRMFB_DOTCLOCK:	// 0x4
		if (writeflag == MEM_WRITE)
			d->dotclock = idata;
		else
			odata = d->dotclock;
		break;

	case CRMFB_I2C_VGA:	// 0x8
		/*
		 *  "CRT I2C control".
		 *
		 *  I'm not sure what this does. It isn't really commented
		 *  in the Linux sources.  The IP32 PROM writes the values
		 *  0x03, 0x01, and then 0x00 to this address, and then
		 *  reads back a value.
		 */
		if (writeflag == MEM_WRITE) {
			d->i2c = idata;
		} else {
			odata = d->i2c;
			odata |= 1;	/*  ?  The IP32 prom wants this?  */
		}
		break;

	case CRMFB_I2C_FP:	// 0x10, i2cfp, flat panel control
		if (writeflag == MEM_WRITE) {
			d->i2cfp = idata;
		} else {
			odata = d->i2cfp;
			odata |= 1;	/*  ?  The IP32 prom wants this?  */
		}
		break;

	case CRMFB_DEVICE_ID:	// 0x14
		odata = CRMFB_DEVICE_ID_DEF;
		break;

	case CRMFB_VT_XY:	// 0x10000
		if (writeflag == MEM_WRITE)
			d->freeze = idata & ((uint32_t)1<<31)? 1 : 0;
		else {
			/*
			 *  vt_xy, according to Linux:
			 *
			 * bit 31 = freeze, 23..12 = cury, 11.0 = curx
			 */
			/*  odata = ((random() % (d->yres + 10)) << 12)
			    + (random() % (d->xres + 10)) +
			    (d->freeze? ((uint32_t)1 << 31) : 0);  */

			/*
			 *  Hack for IRIX/IP32. During startup, it waits for
			 *  the value to be over 0x400 (in "gbeRun").
			 *
			 *  Hack for the IP32 PROM: During startup, it waits
			 *  for the value to be above 0x500 (I think).
			 */
			odata = random() & 1 ? 0x3ff : 0x501;
		}
		break;

	case CRMFB_VT_XYMAX:	// 0x10004, vt_xymax, according to Linux & NetBSD
		odata = ((d->yres-1) << 12) + d->xres-1;
		/*  ... 12 bits maxy, 12 bits maxx.  */
		break;

	case CRMFB_VT_VSYNC:	// 0x10008
	case CRMFB_VT_HSYNC:	// 0x1000c
	case CRMFB_VT_VBLANK:	// 0x10010
	case CRMFB_VT_HBLANK:	// 0x10014
		// TODO
		break;

	case CRMFB_VT_FLAGS:	// 0x10018
		// OpenBSD/sgi writes to this register.
		break;

	case CRMFB_VT_FRAMELOCK:	// 0x1001c
		// TODO.
		break;

	case 0x10028:	// 0x10028
	case 0x1002c:	// 0x1002c
	case 0x10030:	// 0x10030
		// TODO: Unknown, written to by the PROM?
		break;

	case CRMFB_VT_HPIX_EN:	// 0x10034, vt_hpixen, according to Linux
		odata = (0 << 12) + d->xres-1;
		/*  ... 12 bits on, 12 bits off.  */
		break;

	case CRMFB_VT_VPIX_EN:	// 0x10038, vt_vpixen, according to Linux
		odata = (0 << 12) + d->yres-1;
		/*  ... 12 bits on, 12 bits off.  */
		break;

	case CRMFB_VT_HCMAP:	// 0x1003c
		odata = d->xres << CRMFB_VT_HCMAP_ON_SHIFT;
		break;

	case CRMFB_VT_VCMAP:	// 0x10040
		odata = d->yres << CRMFB_VT_VCMAP_ON_SHIFT;
		break;

	case CRMFB_VT_DID_STARTXY:	// 0x10044
	case CRMFB_VT_CRS_STARTXY:	// 0x10048
	case CRMFB_VT_VC_STARTXY:	// 0x1004c
		// TODO
		break;
		
	case CRMFB_OVR_WIDTH_TILE:	// 0x20000
		// TODO
		break;

	case CRMFB_OVR_TILE_PTR:// 0x20004
		/*
		 *  Unknown. Hacks to get the IP32 PROM and IRIX to
		 *  get slightly further than without these values.
		 *
		 *  However, IRIX says
		 *
		 *  "WARNING: crime idle timeout after xxxx us"
		 *
		 *  so perhaps it expects some form of interrupt instead.
		 */
		odata = random();	/*  IP32 prom test hack. TODO  */
		/*  IRIX wants 0x20, it seems.  */
		if (random() & 1)
			odata = 0x20;
		if (random() & 1)
			odata = 0x3bf6a0;
		break;

	case CRMFB_OVR_CONTROL:	// 0x20008
		// TODO.
		break;

	case CRMFB_FRM_TILESIZE:	// 0x30000:
		/*  normal plane ctrl 0  */
		/*  bit 15 = fifo reset, 14..13 = depth, 
		    12..5 = tile width, 4..0 = rhs  */
		if (writeflag == MEM_WRITE) {
			d->plane0ctrl = idata;
			d->bitdepth = 8 << ((d->plane0ctrl >> CRMFB_FRM_TILESIZE_DEPTH_SHIFT) & 3);
			debug("[ sgi_gbe: setting color depth to %i bits ]\n",
			    d->bitdepth);
			if (d->bitdepth != 8)
				fatal("sgi_gbe: warning: bitdepth %i not "
				    "really implemented yet\n", d->bitdepth);
		} else
			odata = d->plane0ctrl;
		break;

	case CRMFB_FRM_PIXSIZE:
		// TODO.
		break;
		
	case 0x30008:	/*  normal plane ctrl 2  */
		odata = random();	/*  IP32 prom test hack. TODO  */
		/*  IRIX wants 0x20, it seems.  */
		if (random() & 1)
			odata = 0x20;
		break;

	case CRMFB_FRM_CONTROL:	// 0x3000c
		/*  normal plane ctrl 3  */
		/*
		 *  Writes to 3000c should be readable back at 30008?
		 *  At least bit 0 (dma) ctrl 3.
		 *
		 *  Bits 31..9 = tile table pointer bits,
		 *  Bit 1 = linear
		 *  Bit 0 = dma
		 */
		if (writeflag == MEM_WRITE) {
			d->frm_control = idata;
			debug("[ sgi_gbe: frm_control = 0x%08x ]\n",
			    d->frm_control);
		} else
			odata = d->frm_control;
		break;

	case CRMFB_DID_PTR:	// 0x40000
		odata = random();	/*  IP32 prom test hack. TODO  */
		/*  IRIX wants 0x20, it seems.  */
		if (random() & 1)
			odata = 0x20;
		break;

	case CRMFB_DID_CONTROL:	// 0x40004
		// TODO
		break;

	case CRMFB_CMAP_FIFO:		// 0x58000
	case CRMFB_CURSOR_POS:		// 0x70000
	case CRMFB_CURSOR_CONTROL:	// 0x70004
	case CRMFB_CURSOR_CMAP0:	// 0x70008
	case CRMFB_CURSOR_CMAP1:	// 0x7000c
	case CRMFB_CURSOR_CMAP2:	// 0x70010
		// TODO
		break;

	/*
	 *  Linux/sgimips seems to write color palette data to offset 0x50000
	 *  to 0x503xx, and gamma correction data to 0x60000 - 0x603ff, as
	 *  32-bit values at addresses divisible by 4 (formated as 0xrrggbb00).
	 *
	 *  sgio2fb: initializing
	 *  sgio2fb: I/O at 0xffffffffb6000000
	 *  sgio2fb: tiles at ffffffffa2ef5000
	 *  sgio2fb: framebuffer at ffffffffa1000000
	 *  sgio2fb: 8192kB memory
	 *  Console: switching to colour frame buffer device 80x30
	 */

	default:
		/*  WID at 0x48000 .. 0x48000 + 4*31:  */
		if (relative_addr >= CRMFB_WID && relative_addr <= CRMFB_WID + 4 * 31) {
			/*  ignore WID for now  */
			break;
		}

		/*  RGB Palette at 0x50000 .. 0x503ff:  */
		if (relative_addr >= CRMFB_CMAP && relative_addr <= CRMFB_CMAP + 0x3ff) {
			int color_nr, r, g, b;
			int old_r, old_g, old_b;

			color_nr = (relative_addr & 0x3ff) / 4;
			r = (idata >> 24) & 0xff;
			g = (idata >> 16) & 0xff;
			b = (idata >>  8) & 0xff;

			old_r = d->fb_data->rgb_palette[color_nr * 3 + 0];
			old_g = d->fb_data->rgb_palette[color_nr * 3 + 1];
			old_b = d->fb_data->rgb_palette[color_nr * 3 + 2];

			d->fb_data->rgb_palette[color_nr * 3 + 0] = r;
			d->fb_data->rgb_palette[color_nr * 3 + 1] = g;
			d->fb_data->rgb_palette[color_nr * 3 + 2] = b;

			if (r != old_r || g != old_g || b != old_b) {
				/*  If the palette has been changed, the entire
				    image needs to be redrawn...  :-/  */
				d->fb_data->update_x1 = 0;
				d->fb_data->update_x2 = d->fb_data->xsize - 1;
				d->fb_data->update_y1 = 0;
				d->fb_data->update_y2 = d->fb_data->ysize - 1;
			}
			break;
		}

		/*  Gamma correction at 0x60000 .. 0x603ff:  */
		if (relative_addr >= CRMFB_GMAP && relative_addr <= CRMFB_GMAP + 0x3ff) {
			/*  ignore gamma correction for now  */
			break;
		}

		/*  Cursor bitmap at 0x78000 ..:  */
		if (relative_addr >= CRMFB_CURSOR_BITMAP && relative_addr <= CRMFB_CURSOR_BITMAP + 0xff) {
			/*  ignore gamma correction for now  */
			break;
		}

#ifdef GBE_DEBUG
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_gbe: unimplemented write to address "
			    "0x%llx, data=0x%llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_gbe: unimplemented read from address "
			    "0x%llx ]\n", (long long)relative_addr);

		exit(1);
#endif
	}

	if (writeflag == MEM_READ) {
#ifdef GBE_DEBUG
		debug("[ sgi_gbe: DEBUG: read from address 0x%llx: 0x%llx ]\n",
		    (long long)relative_addr, (long long)odata);
#endif
		memory_writemax64(cpu, data, len, odata);
	}

	return 1;
}


void dev_sgi_gbe_init(struct machine *machine, struct memory *mem,
	uint64_t baseaddr)
{
	struct sgi_gbe_data *d;

	CHECK_ALLOCATION(d = (struct sgi_gbe_data *) malloc(sizeof(struct sgi_gbe_data)));
	memset(d, 0, sizeof(struct sgi_gbe_data));

	/*  640x480 for Linux:  */
	d->xres = GBE_DEFAULT_XRES;
	d->yres = GBE_DEFAULT_YRES;
	d->bitdepth = 8;
	d->control = 0x20aa000;		/*  or 0x00000001?  */

	/*  1280x1024 for booting the O2's PROM, and experiments with NetBSD and OpenBSD:  */
	d->xres = 1280; d->yres = 1024;

	d->fb_data = dev_fb_init(machine, mem, FAKE_GBE_FB_ADDRESS,
	    VFB_GENERIC, d->xres, d->yres, d->xres, d->yres, 8, "SGI GBE");
	set_grayscale_palette(d->fb_data, 256);

	memory_device_register(mem, "sgi_gbe", baseaddr, DEV_SGI_GBE_LENGTH,
	    dev_sgi_gbe_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_sgi_gbe_tick, d, 18);
}


/****************************************************************************/


/*
 *  SGI "mte". NetBSD sources describes it as a "memory transfer engine".
 *  This device seems to be an accelerator for copying/clearing
 *  memory. Used by (at least) the SGI O2 PROM.
 *
 *  Actually, it seems to be used for graphics output as well. (?)
 *  The O2's PROM uses it to output graphics.
 */
/*  #define debug fatal  */
/*  #define MTE_DEBUG  */
#define	ZERO_CHUNK_LEN		4096

struct sgi_mte_data {
	uint32_t	reg[DEV_SGI_MTE_LENGTH / sizeof(uint32_t)];
};


DEVICE_ACCESS(sgi_mte)
{
	struct sgi_mte_data *d = (struct sgi_mte_data *) extra;
	uint64_t first_addr, last_addr, zerobuflen, fill_addr, fill_len;
	unsigned char zerobuf[ZERO_CHUNK_LEN];
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	/*
	 *  Treat all registers as read/write, by default.  Sometimes these
	 *  are accessed as 32-bit words, sometimes as 64-bit words.
	 */
	if (len != 4) {
		if (writeflag == MEM_WRITE) {
			d->reg[regnr] = idata >> 32;
			d->reg[regnr+1] = idata;
		} else
			odata = ((uint64_t)d->reg[regnr] << 32) +
			    d->reg[regnr+1];
	}

	if (writeflag == MEM_WRITE)
		d->reg[regnr] = idata;
	else
		odata = d->reg[regnr];

#ifdef MTE_DEBUG
	if (writeflag == MEM_WRITE && relative_addr >= 0x2000 &&
	    relative_addr < 0x3000)
		fatal("[ MTE: 0x%08x: 0x%016llx ]\n", (int)relative_addr,
		    (long long)idata);
#endif

	/*
	 *  I've not found any docs about this 'mte' device at all, so this is
	 *  just a guess. The mte seems to be used for copying and zeroing
	 *  chunks of memory.
	 *
	 *   write to 0x3030, data=0x00000000003da000 ]  <-- first address
	 *   write to 0x3038, data=0x00000000003f9fff ]  <-- last address
	 *   write to 0x3018, data=0x0000000000000000 ]  <-- what to fill?
	 *   write to 0x3008, data=0x00000000ffffffff ]  <-- ?
	 *   write to 0x3800, data=0x0000000000000011 ]  <-- operation
	 *						     (0x11 = zerofill)
	 *
	 *   write to 0x1700, data=0x80001ea080001ea1  <-- also containing the
	 *   write to 0x1708, data=0x80001ea280001ea3      address to fill (?)
	 *   write to 0x1710, data=0x80001ea480001ea5
	 *  ...
	 *   write to 0x1770, data=0x80001e9c80001e9d
	 *   write to 0x1778, data=0x80001e9e80001e9f
	 */
	switch (relative_addr) {

	/*  No warnings for these:  */
	case 0x3030:
	case 0x3038:
		break;

	case CRIME_DE_STATUS:	// 0x4000
		odata = CRIME_DE_IDLE;
		break;

	/*  Unknown, but no warning:  */
	case 0x3018:
	case 0x3008:
	case 0x1700:
	case 0x1708:
	case 0x1710:
	case 0x1718:
	case 0x1720:
	case 0x1728:
	case 0x1730:
	case 0x1738:
	case 0x1740:
	case 0x1748:
	case 0x1750:
	case 0x1758:
	case 0x1760:
	case 0x1768:
	case 0x1770:
	case 0x1778:
		break;

	/*  Graphics stuff? No warning:  */
	case 0x2018:
	case 0x2060:
	case 0x2070:
	case 0x2074:
	case 0x20c0:
	case 0x20c4:
	case 0x20d0:
	case 0x21b0:
	case 0x21b8:
		break;

	/*  Perform graphics operation:  */
	case CRIME_DE_FLUSH:	// 0x21f8
		{
			uint32_t op = d->reg[0x2060 / sizeof(uint32_t)];
			uint32_t color = d->reg[0x20d0 / sizeof(uint32_t)]&255;
			uint32_t x1 = (d->reg[0x2070 / sizeof(uint32_t)]
			    >> 16) & 0xfff;
			uint32_t y1 = d->reg[0x2070 / sizeof(uint32_t)]& 0xfff;
			uint32_t x2 = (d->reg[0x2074 / sizeof(uint32_t)]
			    >> 16) & 0xfff;
			uint32_t y2 = d->reg[0x2074 / sizeof(uint32_t)]& 0xfff;
			uint32_t y;

			op >>= 24;

			switch (op) {
			case 1:	/*  Unknown. Used after drawing bitmaps?  */
				break;
			case 3:	/*  Fill:  */
				if (x2 < x1) {
					int tmp = x1; x1 = x2; x2 = tmp;
				}
				if (y2 < y1) {
					int tmp = y1; y1 = y2; y2 = tmp;
				}
				for (y=y1; y<=y2; y++) {
					unsigned char buf[1280];
					int length = x2-x1+1;
					int addr = (x1 + y*1280);
					if (length < 1)
						length = 1;
					memset(buf, color, length);
					if (x1 < 1280 && y < 1024)
						cpu->memory_rw(cpu, cpu->mem,
						    FAKE_GBE_FB_ADDRESS + addr, buf,
						    length, MEM_WRITE,
						    NO_EXCEPTIONS | PHYSICAL);
				}
				break;

			default:fatal("\n--- MTE OP %i color 0x%02x: %i,%i - "
				    "%i,%i\n\n", op, color, x1,y1, x2,y2);
			}
		}
		break;

	case 0x29f0:
		/*  Pixel output:  */
		{
			uint32_t pixeldata = d->reg[0x20c4 / sizeof(uint32_t)];
			uint32_t color = d->reg[0x20d0 / sizeof(uint32_t)]&255;
			uint32_t x1 = (d->reg[0x2070 / sizeof(uint32_t)]
			    >> 16) & 0xfff;
			uint32_t y1 = d->reg[0x2070 / sizeof(uint32_t)]& 0xfff;
			uint32_t x2 = (d->reg[0x2074 / sizeof(uint32_t)]
			    >> 16) & 0xfff;
			uint32_t y2 = d->reg[0x2074 / sizeof(uint32_t)]& 0xfff;
			size_t x, y;

			if (x2 < x1) {
				int tmp = x1; x1 = x2; x2 = tmp;
			}
			if (y2 < y1) {
				int tmp = y1; y1 = y2; y2 = tmp;
			}
			if (x2-x1 <= 15)
				pixeldata <<= 16;

			x=x1; y=y1;
			while (x <= x2 && y <= y2) {
				unsigned char buf = color;
				int addr = x + y*1280;
				int bit_set = pixeldata & 0x80000000UL;
				pixeldata <<= 1;
				if (x < 1280 && y < 1024 && bit_set)
					cpu->memory_rw(cpu, cpu->mem,
					    FAKE_GBE_FB_ADDRESS + addr, &buf,1,MEM_WRITE,
					    NO_EXCEPTIONS | PHYSICAL);
				x++;
				if (x > x2) {
					x = x1;
					y++;
				}
			}
		}
		break;


	/*  Operations:  */
	case 0x3800:
		if (writeflag == MEM_WRITE) {
			switch (idata) {
			case 0x11:		/*  zerofill  */
				first_addr = d->reg[0x3030 / sizeof(uint32_t)];
				last_addr  = d->reg[0x3038 / sizeof(uint32_t)];
				zerobuflen = last_addr - first_addr + 1;
				debug("[ sgi_mte: zerofill: first = 0x%016llx,"
				    " last = 0x%016llx, length = 0x%llx ]\n",
				    (long long)first_addr, (long long)
				    last_addr, (long long)zerobuflen);

				/*  TODO:  is there a better way to
				           implement this?  */
				memset(zerobuf, 0, sizeof(zerobuf));
				fill_addr = first_addr;
				while (zerobuflen != 0) {
					if (zerobuflen > sizeof(zerobuf))
						fill_len = sizeof(zerobuf);
					else
						fill_len = zerobuflen;
					cpu->memory_rw(cpu, mem, fill_addr,
					    zerobuf, fill_len, MEM_WRITE,
					    NO_EXCEPTIONS | PHYSICAL);
					fill_addr += fill_len;
					zerobuflen -= sizeof(zerobuf);
				}

				break;
			default:
				fatal("[ sgi_mte: UNKNOWN operation "
				    "0x%x ]\n", idata);
			}
		}
		break;
	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_mte: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_mte: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mte_init():
 */
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr)
{
	struct sgi_mte_data *d;

	CHECK_ALLOCATION(d = (struct sgi_mte_data *) malloc(sizeof(struct sgi_mte_data)));
	memset(d, 0, sizeof(struct sgi_mte_data));

	memory_device_register(mem, "sgi_mte", baseaddr, DEV_SGI_MTE_LENGTH,
	    dev_sgi_mte_access, (void *)d, DM_DEFAULT, NULL);
}


