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

#define	ZERO_CHUNK_LEN		4096

// #define	GBE_DEBUG
// #define debug fatal

#define	GBE_DEFAULT_XRES		1280
#define	GBE_DEFAULT_YRES		1024


struct sgi_gbe_data {
	// crm / gbe
	int		xres, yres;

	uint32_t	control;		/* 0x00000  */
	uint32_t	dotclock;		/* 0x00004  */
	uint32_t	i2c;			/* 0x00008  */
	uint32_t	i2cfp;			/* 0x00010  */
	uint32_t	plane0ctrl;		/* 0x30000  */
	uint32_t	frm_control;		/* 0x3000c  */
	int		freeze;

	int 		width_in_tiles;
	int		partial_pixels;
	int		bitdepth;
	struct vfb_data *fb_data;

	// rendering engine
	uint32_t	re_reg[DEV_SGI_RE_LENGTH / sizeof(uint32_t)];

	// drawing engine
	uint32_t	de_reg[DEV_SGI_DE_LENGTH / sizeof(uint32_t)];

	// memory transfer engine
	uint32_t	mte_reg[DEV_SGI_MTE_LENGTH / sizeof(uint32_t)];
};


void horrible_putpixel(struct cpu* cpu, struct sgi_gbe_data* d, int x, int y, uint32_t color)
{
	int linear = d->width_in_tiles == 1 && d->partial_pixels == 0;

	if (linear) {
		fatal("sgi gbe horrible_putpixel called in linear mode?!\n");
		exit(1);
	}

	if (x < 0 || y < 0 || x >= d->xres || y >= d->yres)
		return;

	int tilewidth_in_pixels = 512 * 8 / d->bitdepth;
	
	int tile_nr_x = x / tilewidth_in_pixels;
	int tile_nr_y = y / 128;

	int w = d->width_in_tiles + (d->partial_pixels > 0 ? 1 : 0);
	int tile_nr = tile_nr_y * w + tile_nr_x;

	uint64_t tiletable = (d->frm_control & 0xfffffe00);

	unsigned char tileptr_buf[sizeof(uint16_t)];
	cpu->memory_rw(cpu, cpu->mem, tiletable +
	    sizeof(tileptr_buf) * tile_nr,
	    tileptr_buf, sizeof(tileptr_buf), MEM_READ,
	    NO_EXCEPTIONS | PHYSICAL);
	uint64_t tileptr = 256 * tileptr_buf[0] + tileptr_buf[1];
	tileptr <<= 16;

	if (tileptr == 0) {
		fatal("sgi gbe horrible_putpixel: bad tileptr?\n");
		exit(1);
	}

	y %= 128;
	int xofs = (x % tilewidth_in_pixels) * d->bitdepth / 8;

	uint8_t buf[4];
	buf[0] = color;
	buf[1] = buf[2] = buf[3] = 0x00;

	if (d->bitdepth != 8) {
		fatal("sgi gbe horrible_putpixel: TODO: non-8-bit\n");
		exit(1);
	}

	cpu->memory_rw(cpu, cpu->mem, tileptr + 512 * y + xofs,
	    buf, d->bitdepth / 8,
	    MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
}


/*
 *  dev_sgi_gbe_tick():
 *
 *  Every now and then, copy data from the framebuffer in normal ram
 *  to the actual framebuffer (which will then redraw the window).
 *
 *  NOTE: This is very slow, even slower than the normal emulated framebuffer,
 *  which is already slow as it is.
 *
 *  frm_control (bits 31..9) is a pointer to an array of uint16_t.
 *  These numbers (when << 16 bits) are pointers to the tiles. Tiles are
 *  512x128 in 8-bit mode, 256x128 in 16-bit mode, and 128x128 in 32-bit mode.
 *
 *  An exception is how Linux/O2 uses the framebuffer, in a "tweaked" mode
 *  which resembles linear mode. This code attempts to support both.
 *
 *  TODO: It doesn't really use width_in_tiles and partial_pixels, rather
 *  it just draws tiles to cover the whole xres * yres pixels. Perhaps this
 *  could be fixed some day, if it matters.
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

	// Linear "tweaked" mode is something that Linux/O2 uses.
	int linear = d->width_in_tiles == 1 && d->partial_pixels == 0;

	tiletable = (d->frm_control & 0xfffffe00);

#ifdef GBE_DEBUG
	fatal("[ sgi_gbe: dev_sgi_gbe_tick(): tiletable = 0x%llx, linear = %i ]\n",
		(long long)tiletable, linear);
#endif

	if (tiletable == 0)
		on_screen = 0;

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
#ifdef GBE_DEBUG
		fatal("[ sgi_gbe:   tile_nr = %2i, tileptr = 0x%08lx, xbase"
		    " = %4i, ybase = %4i ]\n", tile_nr, tileptr, xbase, ybase);
#endif

		if (linear) {
			/*
			 *  Tweaked (linear) mode, as used by Linux/O2:
			 *
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

				// for (int i=0; i<sizeof(buf); ++i) buf[i] ^= (random() & 0x20);

				dev_fb_access(cpu, cpu->mem, old_fb_offset,
				    buf, copy_len, MEM_WRITE, d->fb_data);
				copy_offset += sizeof(buf);
				old_fb_offset += sizeof(buf);
			}
		} else {
			/*
			 *  Tiled mode (used by other things than Linux):
			 */

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
	buf[i] ^= (random() & 0x21);
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

	if (writeflag == MEM_WRITE) {
		idata = memory_readmax64(cpu, data, len);

#ifdef GBE_DEBUG
		fatal("[ sgi_gbe: DEBUG: write to address 0x%llx, data"
		    "=0x%llx ]\n", (long long)relative_addr, (long long)idata);
#endif
	}

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
		if (writeflag == MEM_WRITE) {
			d->xres = (idata & CRMFB_HCMAP_ON_MASK) >> CRMFB_VT_HCMAP_ON_SHIFT;
			dev_fb_resize(d->fb_data, d->xres, d->yres);
		}
		
		odata = (d->xres << CRMFB_VT_HCMAP_ON_SHIFT) + d->xres + 100;
		break;

	case CRMFB_VT_VCMAP:	// 0x10040
		if (writeflag == MEM_WRITE) {
			d->yres = (idata & CRMFB_VCMAP_ON_MASK) >> CRMFB_VT_VCMAP_ON_SHIFT;
			dev_fb_resize(d->fb_data, d->xres, d->yres);
		}

		odata = (d->yres << CRMFB_VT_VCMAP_ON_SHIFT) + d->yres + 100;
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
			d->width_in_tiles = (idata >> CRMFB_FRM_TILESIZE_WIDTH_SHIFT) & 0xff;
			d->partial_pixels = ((idata >> CRMFB_FRM_TILESIZE_RHS_SHIFT) & 0x1f) * 32;

			debug("[ sgi_gbe: setting color depth to %i bits, width in tiles = %i, partial pixels = %i ]\n",
			    d->bitdepth, d->width_in_tiles, d->partial_pixels);

			if (d->bitdepth != 8) {
				fatal("sgi_gbe: warning: bitdepth %i not "
				    "really implemented yet\n", d->bitdepth);
				exit(1);
			}
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
	 *  "sgio2fb: initializing
	 *   sgio2fb: I/O at 0xffffffffb6000000
	 *   sgio2fb: tiles at ffffffffa2ef5000
	 *   sgio2fb: framebuffer at ffffffffa1000000
	 *   sgio2fb: 8192kB memory
	 *   Console: switching to colour frame buffer device 80x30"
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

	d->xres = GBE_DEFAULT_XRES;
	d->yres = GBE_DEFAULT_YRES;
	d->bitdepth = 8;
	d->control = 0x20aa000;		/*  or 0x00000001?  */

	d->fb_data = dev_fb_init(machine, mem, FAKE_GBE_FB_ADDRESS,
	    VFB_GENERIC, d->xres, d->yres, d->xres, d->yres, 8, "SGI GBE");
	set_grayscale_palette(d->fb_data, 256);

	memory_device_register(mem, "sgi_gbe", baseaddr, DEV_SGI_GBE_LENGTH,
	    dev_sgi_gbe_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_sgi_gbe_tick, d, 18);

	dev_sgi_re_init(mem, 0x15001000, d);
	dev_sgi_de_init(mem, 0x15002000, d);
	dev_sgi_mte_init(mem, 0x15003000, d);
	dev_sgi_de_status_init(mem, 0x15004000, d);
}


/****************************************************************************/

/*
 *  SGI "re", NetBSD sources describes it as a "rendering engine".
 */

DEVICE_ACCESS(sgi_re)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	relative_addr += 0x1000;

	/*
	 *  Treat all registers as read/write, by default.  Sometimes these
	 *  are accessed as 32-bit words, sometimes as 64-bit words.
	 */
	if (len != 4) {
		if (writeflag == MEM_WRITE) {
			d->re_reg[regnr] = idata >> 32;
			d->re_reg[regnr+1] = idata;
		} else
			odata = ((uint64_t)d->re_reg[regnr] << 32) +
			    d->re_reg[regnr+1];
	}

	if (writeflag == MEM_WRITE)
		d->re_reg[regnr] = idata;
	else
		odata = d->re_reg[regnr];

	switch (relative_addr) {

	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_re: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_re: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_re_init():
 */
void dev_sgi_re_init(struct memory *mem, uint64_t baseaddr, struct sgi_gbe_data *d)
{
	memory_device_register(mem, "sgi_re", baseaddr, DEV_SGI_RE_LENGTH,
	    dev_sgi_re_access, (void *)d, DM_DEFAULT, NULL);
}



/****************************************************************************/

/*
 *  SGI "de", NetBSD sources describes it as a "drawing engine".
 */

DEVICE_ACCESS(sgi_de)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;
	bool startFlag = relative_addr & CRIME_DE_START ? true : false;

	relative_addr &= ~CRIME_DE_START;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	relative_addr += 0x2000;

	/*
	 *  Treat all registers as read/write, by default.  Sometimes these
	 *  are accessed as 32-bit words, sometimes as 64-bit words.
	 */
	if (len != 4) {
		if (writeflag == MEM_WRITE) {
			d->de_reg[regnr] = idata >> 32;
			d->de_reg[regnr+1] = idata;
		} else
			odata = ((uint64_t)d->de_reg[regnr] << 32) +
			    d->de_reg[regnr+1];
	}

	if (writeflag == MEM_WRITE)
		d->de_reg[regnr] = idata;
	else
		odata = d->de_reg[regnr];

#ifdef MTE_DEBUG
	if (writeflag == MEM_WRITE && relative_addr >= 0x2000 &&
	    relative_addr < 0x3000)
		fatal("[ DE: 0x%08x: 0x%016llx ]\n", (int)relative_addr,
		    (long long)idata);
#endif

	switch (relative_addr) {

	case CRIME_DE_DRAWMODE:
		debug("[ sgi_de: %s CRIME_DE_DRAWMODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_PRIMITIVE:
		debug("[ sgi_de: %s CRIME_DE_PRIMITIVE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_X_VERTEX_0:
		debug("[ sgi_de: %s CRIME_DE_X_VERTEX_0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_X_VERTEX_1:
		debug("[ sgi_de: %s CRIME_DE_X_VERTEX_1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_STIPPLE_MODE:
		debug("[ sgi_de: %s CRIME_DE_STIPPLE_MODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_STIPPLE_PAT:
		debug("[ sgi_de: %s CRIME_DE_STIPPLE_PAT: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_FG:
		debug("[ sgi_de: %s CRIME_DE_FG: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_BG:
		debug("[ sgi_de: %s CRIME_DE_BG: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_ROP:
		debug("[ sgi_de: %s CRIME_DE_ROP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_PLANEMASK:
		debug("[ sgi_de: %s CRIME_DE_PLANEMASK: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_NULL:	// 0x21f0
	case CRIME_DE_FLUSH:	// 0x21f8
		break;

	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_de: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_de: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (startFlag) {
		uint32_t op = d->de_reg[(CRIME_DE_PRIMITIVE - 0x2000) / sizeof(uint32_t)];

		uint32_t fg = d->de_reg[(CRIME_DE_FG - 0x2000) / sizeof(uint32_t)]&255;
		uint32_t bg = d->de_reg[(CRIME_DE_BG - 0x2000) / sizeof(uint32_t)]&255;
		uint32_t pattern = d->de_reg[(CRIME_DE_STIPPLE_PAT - 0x2000) / sizeof(uint32_t)];

		uint32_t x1 = (d->de_reg[(CRIME_DE_X_VERTEX_0 - 0x2000) / sizeof(uint32_t)]
		    >> 16) & 0xfff;
		uint32_t y1 = d->de_reg[(CRIME_DE_X_VERTEX_0 - 0x2000) / sizeof(uint32_t)]& 0xfff;
		uint32_t x2 = (d->de_reg[(CRIME_DE_X_VERTEX_1 - 0x2000) / sizeof(uint32_t)]
		    >> 16) & 0xfff;
		uint32_t y2 = d->de_reg[(CRIME_DE_X_VERTEX_1 - 0x2000) / sizeof(uint32_t)]& 0xfff;
		size_t x, y;

		// TODO: Take drawmode, rop, bg, and planemask into account etc.

		debug("[ sgi_de: STARTING DRAWING COMMAND: op = 0x%08x,"
		    " x1=%i y1=%i x2=%i y2=%i fg=0x%x bg=0x%x pattern=0x%08x ]\n",
		    op, x1, y1, x2, y2, fg, bg, pattern);

		switch (op & 0xff000000) {
		case DE_PRIM_LINE:
			/*
			 *  Used by the PROM to draw text
			 *  characters on the screen.
			 */

			if (x2 < x1) {
				int tmp = x1; x1 = x2; x2 = tmp;
			}
			if (y2 < y1) {
				int tmp = y1; y1 = y2; y2 = tmp;
			}
			if (x2-x1 <= 15)
				pattern <<= 16;

			x=x1; y=y1;
			while (x <= x2 && y <= y2) {
				if (pattern & 0x80000000UL)
					horrible_putpixel(cpu, d, x, y, fg);

				pattern <<= 1;
				x++;
				if (x > x2) {
					x = x1;
					y++;
				}
			}
			break;
		case DE_PRIM_RECTANGLE:
			/*
			 *  Used by the PROM to fill parts of the background,
			 *  and used by NetBSD/OpenBSD to draw text characters.
			 */
			if (x2 < x1) {
				int tmp = x1; x1 = x2; x2 = tmp;
			}
			if (y2 < y1) {
				int tmp = y1; y1 = y2; y2 = tmp;
			}

			for (y=y1; y<=y2; y++)
				for (x = x1; x <= x2; ++x)
					horrible_putpixel(cpu, d, x, y, fg);
			break;

		default:fatal("[ sgi_de: UNIMPLEMENTED drawing command: op = 0x%08x,"
			    " x1=%i y1=%i x2=%i y2=%i fg=0x%x bg=0x%x pattern=0x%08x ]\n",
			    op, x1, y1, x2, y2, fg, bg, pattern);
			exit(1);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_de_init():
 */
void dev_sgi_de_init(struct memory *mem, uint64_t baseaddr, struct sgi_gbe_data *d)
{
	memory_device_register(mem, "sgi_de", baseaddr, DEV_SGI_DE_LENGTH,
	    dev_sgi_de_access, (void *)d, DM_DEFAULT, NULL);
}


/****************************************************************************/

/*
 *  SGI "mte", NetBSD sources describes it as a "memory transfer engine".
 *
 *  If the relative address has the 0x0800 flag set, it means "go ahead
 *  with the transfer". Otherwise, it is just reads and writes of the
 *  registers.
 */

DEVICE_ACCESS(sgi_mte)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;
	int regnr;
	bool startFlag = relative_addr & 0x0800 ? true : false;

	relative_addr &= ~0x0800;

	idata = memory_readmax64(cpu, data, len);
	regnr = relative_addr / sizeof(uint32_t);

	relative_addr += 0x3000;

	/*
	 *  Treat all registers as read/write, by default.  Sometimes these
	 *  are accessed as 32-bit words, sometimes as 64-bit words.
	 *
	 *  NOTE: The lowest bits are internally stored in the "low" (+0)
	 *  register, and the higher bits are stored in the "+1" word.
	 */
	if (len == 4) {
		if (writeflag == MEM_WRITE)
			d->mte_reg[regnr] = idata;
		else
			odata = d->mte_reg[regnr];
	} else if (len != 4) {
		if (writeflag == MEM_WRITE) {
			d->mte_reg[regnr+1] = idata >> 32;
			d->mte_reg[regnr] = idata;
		} else {
			odata = ((uint64_t)d->mte_reg[regnr+1] << 32) +
			    d->mte_reg[regnr];
		}
	} else {
		fatal("[ sgi_mte: UNIMPLEMENTED read/write len %i ]\n", len);
		exit(1);
	}

	switch (relative_addr) {

	case CRIME_MTE_MODE:
		debug("[ sgi_mte: %s CRIME_MTE_MODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_BYTEMASK:
		debug("[ sgi_mte: %s CRIME_MTE_BYTEMASK: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_STIPPLEMASK:
		fatal("[ sgi_mte: %s CRIME_MTE_STIPPLEMASK: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_BG:
		debug("[ sgi_mte: %s CRIME_MTE_BG: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_SRC0:
		fatal("[ sgi_mte: %s CRIME_MTE_SRC0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_SRC1:
		fatal("[ sgi_mte: %s CRIME_MTE_SRC1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST0:
		debug("[ sgi_mte: %s CRIME_MTE_DST0: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST1:
		debug("[ sgi_mte: %s CRIME_MTE_DST1: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_SRC_Y_STEP:
		fatal("[ sgi_mte: %s CRIME_MTE_SRC_Y_STEP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST_Y_STEP:
		fatal("[ sgi_mte: %s CRIME_MTE_DST_Y_STEP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_NULL:
		fatal("[ sgi_mte: %s CRIME_MTE_NULL: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_FLUSH:
		fatal("[ sgi_mte: %s CRIME_MTE_FLUSH: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	default:
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_mte: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_mte: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (startFlag && writeflag == MEM_WRITE) {
		uint32_t mode = d->mte_reg[(CRIME_MTE_MODE - 0x3000) / sizeof(uint32_t)];
		uint64_t dst0 = d->mte_reg[(CRIME_MTE_DST0 - 0x3000) / sizeof(uint32_t)];
		uint64_t dst1 = d->mte_reg[(CRIME_MTE_DST1 - 0x3000) / sizeof(uint32_t)];
		uint64_t dstlen = dst1 - dst0 + 1;
		unsigned char zerobuf[ZERO_CHUNK_LEN];
		int depth = (mode & MTE_MODE_DEPTH_MASK) >> MTE_DEPTH_SHIFT;
		int src = (mode & MTE_MODE_SRC_BUF_MASK) >> MTE_SRC_TLB_SHIFT;
		uint32_t bytemask = d->mte_reg[(CRIME_MTE_BYTEMASK - 0x3000) / sizeof(uint32_t)];
		uint32_t bg = d->mte_reg[(CRIME_MTE_BG - 0x3000) / sizeof(uint32_t)];

		// TODO: copy from src0/src1?

		if (mode == 0x01) {
			fatal("[ sgi_mte: TODO! unimplemented mode 0x%x ]", mode);
			return 1;
		}

		if (mode != 0x11) {
			fatal("[ sgi_mte: unimplemented mode 0x%x ]", mode);
			exit(1);
		}

		if (depth != MTE_DEPTH_8) {
			fatal("[ sgi_mte: unimplemented MTE_DEPTH_x ]");
			exit(1);
		}

		if (bg != 0) {
			fatal("[ sgi_mte: unimplemented BG != 0 ]");
			exit(1);
		}

		if (src != 0) {
			fatal("[ sgi_mte: unimplemented SRC ]");
			exit(1);
		}

		if (mode & MTE_MODE_COPY) {
			fatal("[ sgi_mte: unimplemented MTE_MODE_COPY ]");
			exit(1);
		}

		if (mode & MTE_MODE_STIPPLE) {
			fatal("[ sgi_mte: unimplemented MTE_MODE_STIPPLE ]");
			exit(1);
		}

		debug("[ sgi_mte: STARTING TRANSFER: dst0 = 0x%016llx,"
		    " dst1 = 0x%016llx (length = 0x%llx), bg = 0x%x, bytemask = 0x%x ]\n",
		    (long long)dst0, (long long)dst1,
		    (long long)dstlen, (int)bg, (int)bytemask);

		memset(zerobuf, 0, sizeof(zerobuf));
		uint64_t fill_addr = dst0;
		while (dstlen != 0) {
			uint64_t fill_len;
			if (dstlen > sizeof(zerobuf))
				fill_len = sizeof(zerobuf);
			else
				fill_len = dstlen;

			cpu->memory_rw(cpu, mem, fill_addr, zerobuf, fill_len,
				MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);

			fill_addr += fill_len;
			dstlen -= sizeof(zerobuf);
		}
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_mte_init():
 */
void dev_sgi_mte_init(struct memory *mem, uint64_t baseaddr, struct sgi_gbe_data *d)
{
	memory_device_register(mem, "sgi_mte", baseaddr, DEV_SGI_MTE_LENGTH,
	    dev_sgi_mte_access, (void *)d, DM_DEFAULT, NULL);
}


/****************************************************************************/

/*
 *  SGI "de_status".
 */

DEVICE_ACCESS(sgi_de_status)
{
	// struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;
	// int regnr;

	idata = memory_readmax64(cpu, data, len);
	// regnr = relative_addr / sizeof(uint32_t);

	relative_addr += 0x4000;

	switch (relative_addr) {

	case CRIME_DE_STATUS:	// 0x4000
		odata = CRIME_DE_IDLE;
		break;

	default:
		if (writeflag == MEM_WRITE)
			debug("[ sgi_de_status: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			debug("[ sgi_de_status: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
	}

	if (writeflag == MEM_READ)
		memory_writemax64(cpu, data, len, odata);

	return 1;
}


/*
 *  dev_sgi_de_status_init():
 */
void dev_sgi_de_status_init(struct memory *mem, uint64_t baseaddr, struct sgi_gbe_data *d)
{
	memory_device_register(mem, "sgi_de_status", baseaddr, DEV_SGI_DE_STATUS_LENGTH,
	    dev_sgi_de_status_access, (void *)d, DM_DEFAULT, NULL);
}


