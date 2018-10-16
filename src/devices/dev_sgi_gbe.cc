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
 *  Guesswork, based on how Linux, NetBSD, and OpenBSD use the graphics on
 *  the SGI O2. Using NetBSD terminology (from crmfbreg.h):
 *
 *	0x15001000	rendering engine (TLBs)
 *	0x15002000	drawing engine
 *	0x15003000	memory transfer engine
 *	0x15004000	status registers for drawing engine
 *
 *	0x16000000	crm (or gbe) framebuffer control / video output
 *
 *  According to https://www.linux-mips.org/wiki/GBE, the GBE is also used in
 *  the SGI Visual Workstation.
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
#include "thirdparty/sgi_gl.h"


/*  Let's hope nothing is there already...  */
#define	FAKE_GBE_FB_ADDRESS	0x380000000

#define	ZERO_CHUNK_LEN		4096

// #define	GBE_DEBUG
// #define debug fatal

#define	GBE_DEFAULT_XRES		1280
#define	GBE_DEFAULT_YRES		1024
#define	GBE_DEFAULT_BITDEPTH		8


struct sgi_gbe_data {
	// CRM / GBE registers:
	int		xres, yres;

	uint32_t	ctrlstat;		/* 0x00000  */
	uint32_t	dotclock;		/* 0x00004  */
	uint32_t	i2c;			/* 0x00008  */
	uint32_t	i2cfp;			/* 0x00010  */

	uint32_t	tilesize;		/* 0x30000  */
	uint32_t	frm_control;		/* 0x3000c  */
	int		freeze;

	uint32_t	palette[256];		/* 0x50000  */

	uint32_t	cursor_pos;		/* 0x70000  */
	uint32_t	cursor_control;		/* 0x70004  */
	uint32_t	cursor_cmap0;		/* 0x70008  */
	uint32_t	cursor_cmap1;		/* 0x7000c  */
	uint32_t	cursor_cmap2;		/* 0x70010  */
	uint32_t	cursor_bitmap[64];	/* 0x78000  */

	// Emulator's representation:
	int 		width_in_tiles;
	int		partial_pixels;
	int		bitdepth;
	int		color_mode;
	struct vfb_data *fb_data;

	// Rendering engine registers:
	uint16_t	re_tlb_a[256];
	uint16_t	re_tlb_b[256];
	uint16_t	re_tlb_c[256];
	uint16_t	re_tex[112];
	// todo: clip_ids registers.
	uint32_t	re_linear_a[32];
	uint32_t	re_linear_b[32];

	// Drawing engine registers:
	uint32_t	de_reg[DEV_SGI_DE_LENGTH / sizeof(uint32_t)];

	// Memory transfer engine registers:
	uint32_t	mte_reg[DEV_SGI_MTE_LENGTH / sizeof(uint32_t)];
};


void get_rgb(struct sgi_gbe_data *d, uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	switch (d->color_mode) {
	case CRMFB_MODE_TYP_I8:
		color &= 0xff;
		*r = d->palette[color] >> 24;
		*g = d->palette[color] >> 16;
		*b = d->palette[color] >> 8;
		break;
	case CRMFB_MODE_TYP_RG3B2:	// Used by NetBSD
		*r = (color >> 5) << 5;	if (*r & 0x20) *r |= 0x1f;
		*g = (color >> 2) << 5;	if (*g & 0x20) *g |= 0x1f;
		*b = color << 6;	if (*b & 0x40) *b |= 0x3f;
		break;
	default:fatal("sgi gbe get_rgb(): unimplemented mode %i\n", d->color_mode);
		exit(1);
	}
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
 */
DEVICE_TICK(sgi_gbe)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t tiletable;
	unsigned char buf[16384];	/*  must be power of 2, at most 65536 */
	int bytes_per_pixel = d->bitdepth / 8;

	if (!cpu->machine->x11_md.in_use)
		return;

	tiletable = (d->frm_control & 0xfffffe00);

#ifdef GBE_DEBUG
	fatal("[ sgi_gbe: dev_sgi_gbe_tick(): tiletable = 0x%llx, linear = %i ]\n",
		(long long)tiletable, linear);
#endif

	if (tiletable == 0)
		return;

	/*
	 *  Tiled mode (used by other things than Linux):
	 */

	// Nr of tiles horizontally:
	int w = d->width_in_tiles + (d->partial_pixels > 0 ? 1 : 0);

	// Actually, the number of tiles vertically is usually very few,
	// but this algorithm will render "up to" 256 and abort as soon
	// as the screen is filled instead. This makes it work for both
	// Linux' "tweaked linear" mode and all the other guest OSes.
	const int max_nr_of_tiles = 256;
	
	uint32_t tile[max_nr_of_tiles];
	uint8_t alltileptrs[max_nr_of_tiles * sizeof(uint16_t)];
	
	cpu->memory_rw(cpu, cpu->mem, tiletable,
	    alltileptrs, sizeof(alltileptrs), MEM_READ,
	    NO_EXCEPTIONS | PHYSICAL);

	/*
	 *  PERHAPS this is the way it works:
	 *
	 *  tiles 0 and forward are for regular rendering.
	 *  tiles 64 and forward are for overlay?
	 *
	 *  TODO: figure out.
	 */

	for (int i = 0; i < 256; ++i) {
		tile[i] = (256 * alltileptrs[i*2] + alltileptrs[i*2+1]) << 16;
#ifdef GBE_DEBUG
		if (tile[i] != 0)
			printf("tile[%i] = 0x%08x\n", i, tile[i]);
#endif
	}

	int screensize = d->xres * d->yres * 3;
	int x = 0, y = 0;

	for (int tiley = 0; tiley < max_nr_of_tiles; ++tiley) {
		for (int line = 0; line < 128; ++line) {
			for (int tilex = 0; tilex < w; ++tilex) {
				int tilenr = tilex + tiley * w;
				
				if (tilenr >= max_nr_of_tiles)
					continue;
				
				uint32_t base = tile[tilenr];
				
				if (base == 0)
					continue;
				
				// Read one line of up to 512 bytes from the tile.
				int len = tilex < d->width_in_tiles ? 512 : (d->partial_pixels * bytes_per_pixel);

				cpu->memory_rw(cpu, cpu->mem, base + 512 * line,
				    buf, len, MEM_READ, NO_EXCEPTIONS | PHYSICAL);

#if 0
				for (int j=0; j<len; j++) buf[j] ^= (random() & 0x21);
#endif

				int fb_offset = (x + y * d->xres) * 3;
				int fb_len = (len / bytes_per_pixel) * 3;

				if (fb_offset + fb_len > screensize) {
					fb_len = screensize - fb_offset;
				}
				
				if (fb_len <= 0) {
					tiley = max_nr_of_tiles;  // to break
					tilex = w;
					line = 128;
				}

				uint8_t fb_buf[512 * 3];
				int fb_i = 0;
				for (int i = 0; i < 512; i+=bytes_per_pixel) {
					uint32_t color;
					if (bytes_per_pixel == 1)
						color = buf[i];
					else if (bytes_per_pixel == 2)
						color = (buf[i]<<8) + buf[i+1];
					else // if (bytes_per_pixel == 4)
						color = (buf[i]<<24) + (buf[i+1]<<16)
							+ (buf[i+2]<<8)+buf[i+3];
					get_rgb(d, color,
					    &fb_buf[fb_i],
					    &fb_buf[fb_i+1],
					    &fb_buf[fb_i+2]);
					fb_i += 3;
				}

				dev_fb_access(cpu, cpu->mem, fb_offset,
				    fb_buf, fb_len, MEM_WRITE, d->fb_data);

				x += len / bytes_per_pixel;
				if (x >= d->xres) {
					x -= d->xres;
					++y;
					if (y >= d->yres) {
						tiley = max_nr_of_tiles; // to break
						tilex = w;
						line = 128;
					}
				}
			}
		}
	}

	if (d->cursor_control & CRMFB_CURSOR_ON) {
		int16_t cx = d->cursor_pos & 0xffff;
		int16_t cy = d->cursor_pos >> 16;

		if (d->cursor_control & CRMFB_CURSOR_CROSSHAIR) {
			uint8_t pixel[3];
			pixel[0] = d->cursor_cmap0 >> 24;
			pixel[1] = d->cursor_cmap0 >> 16;
			pixel[2] = d->cursor_cmap0 >> 8;

			if (cx >= 0 && cx < d->xres) {
				for (y = 0; y < d->yres; ++y)
					dev_fb_access(cpu, cpu->mem, (cx + y * d->xres) * 3,
					    pixel, 3, MEM_WRITE, d->fb_data);
			}

			// TODO: Rewrite as a single framebuffer block write?
			if (cy >= 0 && cy < d->yres) {
				for (x = 0; x < d->xres; ++x)
					dev_fb_access(cpu, cpu->mem, (x + cy * d->xres) * 3,
					    pixel, 3, MEM_WRITE, d->fb_data);
			}
		} else {
			uint8_t pixel[3];
			int sx, sy;

			for (int dy = 0; dy < 32; ++dy) {
				for (int dx = 0; dx < 32; ++dx) {
					sx = cx + dx;
					sy = cy + dy;
					
					if (sx < 0 || sx >= d->xres ||
					    sy < 0 || sy >= d->yres)
						continue;
					
					int wordindex = dy*2 + (dx>>4);
					uint32_t word = d->cursor_bitmap[wordindex];
					
					int color = (word >> ((15 - (dx&15))*2)) & 3;
					
					if (!color)
						continue;

					if (color == 1) {
						pixel[0] = d->cursor_cmap0 >> 24;
						pixel[1] = d->cursor_cmap0 >> 16;
						pixel[2] = d->cursor_cmap0 >> 8;
					} else if (color == 2) {
						pixel[0] = d->cursor_cmap1 >> 24;
						pixel[1] = d->cursor_cmap1 >> 16;
						pixel[2] = d->cursor_cmap1 >> 8;
					} else {
						pixel[0] = d->cursor_cmap2 >> 24;
						pixel[1] = d->cursor_cmap2 >> 16;
						pixel[2] = d->cursor_cmap2 >> 8;
					}

					dev_fb_access(cpu, cpu->mem, (sx + sy * d->xres) * 3,
					    pixel, 3, MEM_WRITE, d->fb_data);
				}
			}
		}
	}
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
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_gbe: write to ctrlstat: 0x%08x ]\n", (int)idata);
			d->ctrlstat = (idata & ~CRMFB_CTRLSTAT_CHIPID_MASK)
				| (d->ctrlstat & CRMFB_CTRLSTAT_CHIPID_MASK);
		} else
			odata = d->ctrlstat;
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
			//if (!(d->i2c & CRMFB_I2C_SCL) &&
			//    (idata & CRMFB_I2C_SCL)) {
			//	fatal("vga i2c data: %i\n", idata & CRMFB_I2C_SDA);
			//}

			d->i2c = idata;
		} else {
			odata = d->i2c;
			odata |= 1;	/*  ?  The IP32 prom wants this?  */
		}
		break;

	case CRMFB_I2C_FP:	// 0x10, i2cfp, flat panel control
		if (writeflag == MEM_WRITE) {
			//if (d->i2c & CRMFB_I2C_SCL &&
			//    !(idata & CRMFB_I2C_SCL)) {
			//	fatal("fp i2c data: %i\n", idata & CRMFB_I2C_SDA);
			//}

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
		if (writeflag == MEM_WRITE) {
			d->tilesize = idata;

			d->bitdepth = 8 << ((d->tilesize >> CRMFB_FRM_TILESIZE_DEPTH_SHIFT) & 3);
			d->width_in_tiles = (idata >> CRMFB_FRM_TILESIZE_WIDTH_SHIFT) & 0xff;
			d->partial_pixels = ((idata >> CRMFB_FRM_TILESIZE_RHS_SHIFT) & 0x1f) * 32 * 8 / d->bitdepth;

			debug("[ sgi_gbe: setting color depth to %i bits, width in tiles = %i, partial pixels = %i ]\n",
			    d->bitdepth, d->width_in_tiles, d->partial_pixels);
		} else
			odata = d->tilesize;
		break;

	case CRMFB_FRM_PIXSIZE:	// 0x30004
		if (writeflag == MEM_WRITE) {
			debug("[ sgi_gbe: setting PIXSIZE to 0x%08x ]\n", (int)idata);
		}
		break;
		
	case 0x30008:
		odata = random();	/*  IP32 prom test hack. TODO  */
		/*  IRIX wants 0x20, it seems.  */
		if (random() & 1)
			odata = 0x20;
		break;

	case CRMFB_FRM_CONTROL:	// 0x3000c
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

	case CRMFB_WID:		// 0x48000
		// TODO: Figure out how this really works.
		if (writeflag == MEM_WRITE) {
			d->color_mode = (idata >> CRMFB_MODE_TYP_SHIFT) & 7;
		}
		break;

	case CRMFB_CMAP_FIFO:		// 0x58000
		break;

	case CRMFB_CURSOR_POS:		// 0x70000
		if (writeflag == MEM_WRITE)
			d->cursor_pos = idata;
		else
			odata = d->cursor_pos;
		break;
		
	case CRMFB_CURSOR_CONTROL:	// 0x70004
		if (writeflag == MEM_WRITE)
			d->cursor_control = idata;
		else
			odata = d->cursor_control;
		break;
		
	case CRMFB_CURSOR_CMAP0:	// 0x70008
		if (writeflag == MEM_WRITE)
			d->cursor_cmap0 = idata;
		else
			odata = d->cursor_cmap0;
		break;
		
	case CRMFB_CURSOR_CMAP1:	// 0x7000c
		if (writeflag == MEM_WRITE)
			d->cursor_cmap1 = idata;
		else
			odata = d->cursor_cmap1;
		break;
		
	case CRMFB_CURSOR_CMAP2:	// 0x70010
		if (writeflag == MEM_WRITE)
			d->cursor_cmap2 = idata;
		else
			odata = d->cursor_cmap2;
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
			int color_index = (relative_addr & 0x3ff) / 4;
			if (writeflag == MEM_WRITE)
				d->palette[color_index] = idata;
			else
				odata = d->palette[color_index];
			break;
		}

		/*  Gamma correction at 0x60000 .. 0x603ff:  */
		if (relative_addr >= CRMFB_GMAP && relative_addr <= CRMFB_GMAP + 0x3ff) {
			/*  ignore gamma correction for now  */
			break;
		}

		/*  Cursor bitmap at 0x78000 ..:  */
		if (relative_addr >= CRMFB_CURSOR_BITMAP && relative_addr <= CRMFB_CURSOR_BITMAP + 0xff) {
			if (len != 4) {
				printf("unimplemented CRMFB_CURSOR_BITMAP len %i\n", (int)len);
			}

			int index = (relative_addr & 0xff) / 4;
			if (writeflag == MEM_WRITE)
				d->cursor_bitmap[index] = idata;
			else
				odata = d->cursor_bitmap[index];
			break;
		}

//#ifdef GBE_DEBUG
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_gbe: unimplemented write to address "
			    "0x%llx, data=0x%llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_gbe: unimplemented read from address "
			    "0x%llx ]\n", (long long)relative_addr);

		// exit(1);
//#endif
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
	d->bitdepth = GBE_DEFAULT_BITDEPTH;
	
	// My O2 says 0x300ae001 here (while running).
	d->ctrlstat = CRMFB_CTRLSTAT_INTERNAL_PCLK |
			CRMFB_CTRLSTAT_GPIO6_INPUT |
			CRMFB_CTRLSTAT_GPIO5_INPUT |
			CRMFB_CTRLSTAT_GPIO4_INPUT |
			CRMFB_CTRLSTAT_GPIO4_SENSE |
			CRMFB_CTRLSTAT_GPIO3_INPUT |
			(CRMFB_CTRLSTAT_CHIPID_MASK & 1);

	// Grayscale palette, most likely overwritten immediately by the
	// guest operating system.
	for (int i = 0; i < 256; ++i)
		d->palette[i] = (i<<24) + (i<<16) + (i<<8);

	d->fb_data = dev_fb_init(machine, mem, FAKE_GBE_FB_ADDRESS,
	    VFB_GENERIC, d->xres, d->yres, d->xres, d->yres, 24, "SGI GBE");

	memory_device_register(mem, "sgi_gbe", baseaddr, DEV_SGI_GBE_LENGTH,
	    dev_sgi_gbe_access, d, DM_DEFAULT, NULL);
	machine_add_tickfunction(machine, dev_sgi_gbe_tick, d, 19);

	dev_sgi_re_init(mem, 0x15001000, d);
	dev_sgi_de_init(mem, 0x15002000, d);
	dev_sgi_mte_init(mem, 0x15003000, d);
	dev_sgi_de_status_init(mem, 0x15004000, d);
}


/****************************************************************************/


/*
 *  horrible_getputpixel():
 *
 *  This routine gets/puts a pixel in one of the tiles, from the perspective of
 *  the rendering/drawing engine. Given x and y, it figures out which tile
 *  number it is, and then finally does a slow read/write to get/put the pixel
 *  at the correct sub-coordinates within the tile.
 *
 *  Tiles are always 512 _bytes_ wide, and 128 pixels high. For 32-bit color
 *  modes, for example, that means 128 x 128 pixels.
 */
void horrible_getputpixel(bool put, struct cpu* cpu, struct sgi_gbe_data* d, int dst_mode, int x, int y, uint32_t* color)
{
	// TODO: The get/put pixel methods should not rely on the "back end"
	// stuff. Figure out how to detect the Linux tweaked/linear mode
	// without reading GBE specific registers or values.
	int linear = d->width_in_tiles == 1 && d->partial_pixels == 0;

	// dst_mode (see NetBSD's crmfbreg.h):
	// #define DE_MODE_TLB_A           0x00000000
	// #define DE_MODE_TLB_B           0x00000400
	// #define DE_MODE_TLB_C           0x00000800
	// #define DE_MODE_LIN_A           0x00001000
	// #define DE_MODE_LIN_B           0x00001400
	uint32_t mode = dst_mode & 0x7;

	uint16_t* tlb = NULL;
	
	switch (mode) {
	case 0:	tlb = d->re_tlb_a;
		break;
	case 1:	tlb = d->re_tlb_b;
		break;
	case 2:	tlb = d->re_tlb_c;
		break;
	default:fatal("unimplemented dst_mode\n");
		exit(1);
	}

	if (linear) {
		fatal("sgi gbe horrible_getputpixel called in linear mode?!\n");
		exit(1);
	}

	if (x < 0 || y < 0 || x >= d->xres || y >= d->yres)
		return;

	// TODO: Non 8-bit modes!
	int tilewidth_in_pixels = 512;
	
	int tile_nr_x = x / tilewidth_in_pixels;
	int tile_nr_y = y / 128;

	int w = 2048 / 512; // d->width_in_tiles + (d->partial_pixels > 0 ? 1 : 0);
	int tile_nr = tile_nr_y * w + tile_nr_x;

	// The highest bit seems to be set for a "valid" tile pointer.
	uint32_t tileptr = tlb[tile_nr] << 16;

	// printf("dst_mode %i, tile_nr = %i,  tileptr = 0x%llx\n", dst_mode, tile_nr, (long long)tileptr);

	if (!(tileptr & 0x80000000)) {
		fatal("sgi gbe horrible_getputpixel: unexpected non-set high bit of tileptr?\n");
		exit(1);
	}
	
	tileptr &= 0x7fffffff;

	y %= 128;
	int xofs = (x % tilewidth_in_pixels) * d->bitdepth / 8;

	uint8_t buf[4];
	if (put) {
		buf[0] = *color;
		buf[1] = buf[2] = buf[3] = *color;	// TODO

		cpu->memory_rw(cpu, cpu->mem, tileptr + 512 * y + xofs,
		    buf, d->bitdepth / 8,
		    MEM_WRITE, NO_EXCEPTIONS | PHYSICAL);
	} else {
		cpu->memory_rw(cpu, cpu->mem, tileptr + 512 * y + xofs,
		    buf, d->bitdepth / 8,
		    MEM_READ, NO_EXCEPTIONS | PHYSICAL);

		*color = buf[0];
	}
}


/*
 *  SGI "re", NetBSD sources describes it as a "rendering engine".
 */

DEVICE_ACCESS(sgi_re)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0;

	idata = memory_readmax64(cpu, data, len);

	relative_addr += 0x1000;

	if (relative_addr >= CRIME_RE_TLB_A && relative_addr < CRIME_RE_TLB_B) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TLB_A\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x1ff) >> 3) & 0xff;
			if (tlbi & 3) {
				// TODO: openbsd writes sequences that I didn't
				// expect...
				return 1;
			}
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tlb_a[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tlb_a[%i] = 0x%04x\n", tlbi, d->re_tlb_a[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TLB_A\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_TLB_B && relative_addr < CRIME_RE_TLB_C) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TLB_B\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x1ff) >> 3) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tlb_b[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tlb_b[%i] = 0x%04x\n", tlbi, d->re_tlb_b[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TLB_B\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_TLB_C && relative_addr < CRIME_RE_TLB_C + 0x200) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TLB_C\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x1ff) >> 3) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tlb_c[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tlb_c[%i] = 0x%04x\n", tlbi, d->re_tlb_c[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TLB_C\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_TEX && relative_addr < CRIME_RE_TEX + 0xe0) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_TEX\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0xff) >> 3) & 0xff;
			for (size_t hwi = 0; hwi < len; hwi += sizeof(uint16_t)) {
				d->re_tex[tlbi] = data[hwi]*256 + data[hwi+1];
				debug("d->re_tex[%i] = 0x%04x\n", tlbi, d->re_tex[tlbi]);
				tlbi++;
			}
		} else {
			fatal("TODO: read from CRIME_RE_TEX\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_LINEAR_A && relative_addr < CRIME_RE_LINEAR_A + 0x80) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_LINEAR_A\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x7f) >> 2) & 0x1f;
			d->re_linear_a[tlbi] = idata;
			debug("d->re_linear_a[%i] = 0x%08x\n", tlbi, d->re_linear_a[tlbi]);
		} else {
			fatal("TODO: read from CRIME_RE_LINEAR_A\n");
			exit(1);
		}
	} else if (relative_addr >= CRIME_RE_LINEAR_B && relative_addr < CRIME_RE_LINEAR_B + 0x80) {
		if (len != 8) {
			fatal("TODO: unimplemented len=%i for CRIME_RE_LINEAR_B\n", len);
			exit(1);
		}

		if (writeflag == MEM_WRITE) {
			int tlbi = ((relative_addr & 0x7f) >> 2) & 0x1f;
			d->re_linear_b[tlbi] = idata;
			debug("d->re_linear_b[%i] = 0x%08x\n", tlbi, d->re_linear_b[tlbi]);
		} else {
			fatal("TODO: read from CRIME_RE_LINEAR_B\n");
			exit(1);
		}
	} else {
		if (writeflag == MEM_WRITE)
			fatal("[ sgi_re: unimplemented write to "
			    "address 0x%llx, data=0x%016llx ]\n",
			    (long long)relative_addr, (long long)idata);
		else
			fatal("[ sgi_re: unimplemented read from address"
			    " 0x%llx ]\n", (long long)relative_addr);
		exit(1);
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
	} else if (len == 4) {
		if (writeflag == MEM_WRITE)
			d->de_reg[regnr] = idata;
		else
			odata = d->de_reg[regnr];
	} else {
		fatal("sgi_de: len = %i not implemented\n", len);
		exit(1);
	}

	switch (relative_addr) {

	case CRIME_DE_MODE_SRC:
		debug("[ sgi_de: %s CRIME_DE_MODE_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_MODE_DST:
		debug("[ sgi_de: %s CRIME_DE_MODE_DST: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_CLIPMODE:
		debug("[ sgi_de: %s CRIME_DE_CLIPMODE: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_CLIPMODE: 0x%016llx ]\n", idata);
		break;

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

	case CRIME_DE_WINOFFSET_SRC:
		debug("[ sgi_de: %s CRIME_DE_WINOFFSET_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_WINOFFSET_SRC: 0x%016llx ]\n", idata);
		break;

	case CRIME_DE_WINOFFSET_DST:
		debug("[ sgi_de: %s CRIME_DE_WINOFFSET_DST: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		if (writeflag == MEM_WRITE && idata != 0)
			fatal("[ sgi_de: TODO: non-zero CRIME_DE_WINOFFSET_DST: 0x%016llx ]\n", idata);
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

	case CRIME_DE_XFER_ADDR_SRC:
		debug("[ sgi_de: %s CRIME_DE_XFER_ADDR_SRC: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_STEP_X:
		debug("[ sgi_de: %s CRIME_DE_XFER_STEP_X: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_DE_XFER_STEP_Y:
		debug("[ sgi_de: %s CRIME_DE_XFER_STEP_Y: 0x%016llx ]\n",
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
		uint32_t drawmode = d->de_reg[(CRIME_DE_DRAWMODE - 0x2000) / sizeof(uint32_t)];
		uint32_t dst_mode = d->de_reg[(CRIME_DE_MODE_DST - 0x2000) / sizeof(uint32_t)];
		uint32_t src_mode = d->de_reg[(CRIME_DE_MODE_SRC - 0x2000) / sizeof(uint32_t)];
		uint32_t fg = d->de_reg[(CRIME_DE_FG - 0x2000) / sizeof(uint32_t)] & 255;
		uint32_t bg = d->de_reg[(CRIME_DE_BG - 0x2000) / sizeof(uint32_t)] & 255;
		uint32_t pattern = d->de_reg[(CRIME_DE_STIPPLE_PAT - 0x2000) / sizeof(uint32_t)];
		//uint32_t stipple_mode = d->de_reg[(CRIME_DE_STIPPLE_MODE - 0x2000) / sizeof(uint32_t)];
		uint32_t rop = d->de_reg[(CRIME_DE_ROP - 0x2000) / sizeof(uint32_t)];

		uint32_t x1 = (d->de_reg[(CRIME_DE_X_VERTEX_0 - 0x2000) / sizeof(uint32_t)]
		    >> 16) & 0xfff;
		uint32_t y1 = d->de_reg[(CRIME_DE_X_VERTEX_0 - 0x2000) / sizeof(uint32_t)]& 0xfff;
		uint32_t x2 = (d->de_reg[(CRIME_DE_X_VERTEX_1 - 0x2000) / sizeof(uint32_t)]
		    >> 16) & 0xfff;
		uint32_t y2 = d->de_reg[(CRIME_DE_X_VERTEX_1 - 0x2000) / sizeof(uint32_t)]& 0xfff;
		size_t x, y;

		// TODO: Take drawmode, rop, bg, and planemask into account etc.
		// rop 12 = xor netbsd cursor?

		debug("[ sgi_de: STARTING DRAWING COMMAND: op = 0x%08x,"
		    " x1=%i y1=%i x2=%i y2=%i fg=0x%x bg=0x%x pattern=0x%08x ]\n",
		    op, x1, y1, x2, y2, fg, bg, pattern);

		int src_x = -1, src_y = -1;
		if (drawmode & DE_DRAWMODE_XFER_EN) {
			// Used by the PROM to scroll up the command window.
			uint32_t addr_src = d->de_reg[(CRIME_DE_XFER_ADDR_SRC - 0x2000) / sizeof(uint32_t)];
			uint32_t strd_src = d->de_reg[(CRIME_DE_XFER_STRD_SRC - 0x2000) / sizeof(uint32_t)];
			uint32_t step_x = d->de_reg[(CRIME_DE_XFER_STEP_X - 0x2000) / sizeof(uint32_t)];
			uint32_t step_y = d->de_reg[(CRIME_DE_XFER_STEP_Y - 0x2000) / sizeof(uint32_t)];
			uint32_t addr_dst = d->de_reg[(CRIME_DE_XFER_ADDR_DST - 0x2000) / sizeof(uint32_t)];
			uint32_t strd_dst = d->de_reg[(CRIME_DE_XFER_STRD_DST - 0x2000) / sizeof(uint32_t)];

			src_x = (addr_src >> 16) & 0xfff;
			src_y = addr_src & 0xfff;

			if (step_x != 1 || step_y != 1) {
				fatal("[ sgi_de: unimplemented XFER addr_src=0x%x strd_src=0x%x step_x=0x%x step_y=0x%x "
					"addr_dst=0x%x strd_dst=0x%x ]\n",
					addr_src, strd_src, step_x, step_y, addr_dst, strd_dst);

				exit(1);
			}
		}

		int dx = op & DE_PRIM_RL ? -1 :  1;
		int dy = op & DE_PRIM_TB ?  1 : -1;

		if (x2 < x1) {
			int tmp = x1; x1 = x2; x2 = tmp;
		}
		if (y2 < y1) {
			int tmp = y1; y1 = y2; y2 = tmp;
		}
		
		switch (op & 0xff000000) {
		case DE_PRIM_LINE:
			/*
			 *  Used by the PROM to draw text characters and
			 *  icons on the screen.
			 */
			if (x2-x1 <= 15)
				pattern <<= 16;

			x=x1; y=y1;
			while (x <= x2 && y <= y2) {
				if (pattern & 0x80000000UL)
					horrible_getputpixel(true, cpu, d, (dst_mode & 0x00001c00) >> 10, x, y, &fg);

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
			if (drawmode & DE_DRAWMODE_XFER_EN) {
				// Pixel colors copied from another source.
				// TODO: Actually take top-to-bottom and left-to-right
				// settings into account. Right now it's kind of ignored.
				if (dx < 0) { src_x -= (x2-x1); dx = 1; }
				if (dy < 0) { src_y -= (y2-y1); dy = 1; }
				int saved_src_x = src_x;
				for (y=y1; y<=y2; y+=1) {
					src_x = saved_src_x;
					for (x = x1; x <= x2; x+=1) {
						uint32_t color;
						horrible_getputpixel(false, cpu, d, (src_mode & 0x00001c00) >> 10, src_x, src_y, &color);
						if (drawmode & DE_DRAWMODE_ROP && rop == OPENGL_LOGIC_OP_COPY_INVERTED)
							color = 255 - color;

						horrible_getputpixel(true, cpu, d, (dst_mode & 0x00001c00) >> 10, x, y, &color);
						src_x += dx;
					}
					src_y += dy;
				}
			} else {
				// Plain color.
				for (y=y1; y<=y2; y++)
					for (x = x1; x <= x2; ++x) {
						// TODO: Most likely not correct, but it allows
						// both NetBSD and the PROM to work for now...
						uint32_t color = fg;
						if (drawmode & DE_DRAWMODE_OPAQUE_STIP)
							color = (pattern & 0x80000000UL) ? fg : bg;

						horrible_getputpixel(true, cpu, d, (dst_mode & 0x00001c00) >> 10, x, y, &color);
						pattern <<= 1;
					}
			}
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
 *  If the relative address has the 0x0800 (CRIME_DE_START) flag set, it means
 *  "go ahead with the transfer". Otherwise, it is just reads and writes of the
 *  registers.
 */

DEVICE_ACCESS(sgi_mte)
{
	struct sgi_gbe_data *d = (struct sgi_gbe_data *) extra;
	uint64_t idata = 0, odata = 0, fill_addr;
	int regnr;
	bool startFlag = relative_addr & CRIME_DE_START ? true : false;

	relative_addr &= ~CRIME_DE_START;

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
		debug("[ sgi_mte: %s CRIME_MTE_SRC_Y_STEP: 0x%016llx ]\n",
		    writeflag == MEM_WRITE ? "write to" : "read from",
		    writeflag == MEM_WRITE ? (long long)idata : (long long)odata);
		break;

	case CRIME_MTE_DST_Y_STEP:
		debug("[ sgi_mte: %s CRIME_MTE_DST_Y_STEP: 0x%016llx ]\n",
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
		int32_t dst_y_step = d->mte_reg[(CRIME_MTE_DST_Y_STEP - 0x3000) / sizeof(uint32_t)];
		uint64_t dstlen = dst1 - dst0 + 1;
		unsigned char zerobuf[ZERO_CHUNK_LEN];
		int depth = (mode & MTE_MODE_DEPTH_MASK) >> MTE_DEPTH_SHIFT;
		int src = (mode & MTE_MODE_SRC_BUF_MASK) >> MTE_SRC_TLB_SHIFT;
		uint32_t bytemask = d->mte_reg[(CRIME_MTE_BYTEMASK - 0x3000) / sizeof(uint32_t)];
		uint32_t bg = d->mte_reg[(CRIME_MTE_BG - 0x3000) / sizeof(uint32_t)];

		// TODO: copy from src0/src1?

		debug("[ sgi_mte: STARTING TRANSFER: mode=0x%08x dst0=0x%016llx,"
		    " dst1=0x%016llx (length 0x%llx), dst_y_step=%i bg=0x%x, bytemask=0x%x ]\n",
		    mode,
		    (long long)dst0, (long long)dst1,
		    (long long)dstlen, dst_y_step, (int)bg, (int)bytemask);

		if (dst_y_step != 0 && dst_y_step != 1) {
			fatal("[ sgi_mte: TODO! unimplemented dst_y_step %i ]", dst_y_step);
			exit(1);
		}

		if (depth != MTE_DEPTH_8) {
			fatal("[ sgi_mte: unimplemented MTE_DEPTH_x ]");
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

		int dst_tlb = (mode & MTE_MODE_DST_BUF_MASK) >> MTE_DST_TLB_SHIFT;
		
		switch (dst_tlb) {
		case MTE_TLB_A:
		case MTE_TLB_B:
		case MTE_TLB_C:
			// Used by NetBSD's crmfb_fill_rect. It puts graphical
			// coordinates in dst0 and dst1.
			{
				int x1 = dst0 >> 12;
				int y1 = dst0 & 0xfff;
				int x2 = dst1 >> 12;
				int y2 = dst1 & 0xfff;
				x1 /= (d->bitdepth / 8);
				x2 /= (d->bitdepth / 8);
				for (int y = y1; y <= y2; ++y)
					for  (int x = x1; x <= x2; ++x)
						horrible_getputpixel(true, cpu, d, dst_tlb, x, y, &bg);
			}
			break;
		case MTE_TLB_LIN_A:
			// Used by the PROM to zero-fill memory (?).

			debug("[ sgi_mte: TODO STARTING TRANSFER: mode=0x%08x dst0=0x%016llx,"
			    " dst1=0x%016llx (length 0x%llx), dst_y_step=%i bg=0x%x, bytemask=0x%x ]\n",
			    mode,
			    (long long)dst0, (long long)dst1,
			    (long long)dstlen, dst_y_step, (int)bg, (int)bytemask);

			if (bytemask != 0xffffffff) {
				fatal("unimplemented MTE bytemask 0x%08x\n", (int)bytemask);
				exit(1);
			}

			memset(zerobuf, bg, dstlen < sizeof(zerobuf) ? dstlen : sizeof(zerobuf));
			fill_addr = dst0;
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
			break;
		default:
			fatal("[ sgi_mte: TODO! unimplemented dst_tlb 0x%x ]", dst_tlb);
			return 1;
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


