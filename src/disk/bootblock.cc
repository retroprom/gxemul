/*
 *  Copyright (C) 2003-2020  Anders Gavare.  All rights reserved.
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
 *  Bootblock handling:
 *
 *	o)  For some machines (e.g. DECstation or the Dreamcast), it is
 *	    possible to load a bootblock from a fixed location on disk, and
 *	    simply execute it in memory.
 *
 *	o)  For booting from generic CDROM ISO9660 images, a filename of
 *	    a file to load must be supplied (the kernel filename). It is
 *	    loaded, possibly gunzipped, and then executed as if it was a
 *	    separate file.
 *
 *  TODO: This module needs some cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "diskimage.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "netinet/in.h"
#include "unistd.h"

#include "thirdparty/bootblock.h"


static const char *diskimage_types[] = DISKIMAGE_TYPES;


/*
 *  load_bootblock():
 *
 *  For some emulation modes, it is possible to boot from a harddisk image by
 *  loading a bootblock from a specific disk offset into memory, and executing
 *  that, instead of requiring a separate kernel file.  It is then up to the
 *  bootblock to load a kernel.
 *
 *  Returns 1 on success, 0 on failure.
 */
int load_bootblock(struct machine *m, struct cpu *cpu,
	int *n_loadp, char ***load_namesp)
{
	int boot_disk_type = 0, n_blocks, res, readofs, iso_type, retval = 0;
	unsigned char minibuf[0x20];
	unsigned char *bootblock_buf;
	uint64_t bootblock_offset, base_offset;
	uint64_t bootblock_loadaddr, bootblock_pc;

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = DEFAULT_TMP_DIR;

	int boot_disk_id = diskimage_bootdev(m, &boot_disk_type);
	if (boot_disk_id < 0)
		return 0;

	base_offset = diskimage_get_baseoffset(m, boot_disk_id, boot_disk_type);

	switch (m->machine_type) {

	case MACHINE_DREAMCAST:
		if (!diskimage_is_a_cdrom(cpu->machine, boot_disk_id,
		    boot_disk_type)) {
			fatal("The Dreamcast emulation mode can only boot"
			    " from CD images, not from other disk types.\n");
			exit(1);
		}

		CHECK_ALLOCATION(bootblock_buf = (unsigned char *) malloc(32768));

		debug("loading Dreamcast IP.BIN from %s id %i to 0x8c008000\n",
		    diskimage_types[boot_disk_type], boot_disk_id);

		res = diskimage_access(m, boot_disk_id, boot_disk_type,
		    0, base_offset, bootblock_buf, 0x8000);
		if (!res) {
			fatal("Couldn't read the first 32 KB from the disk image. Aborting.\n");
			return 0;
		}

		if (strncmp((char *)bootblock_buf, "SEGA ", 5) != 0) {
			fatal("This is not a Dreamcast IP.BIN header.\n");
			free(bootblock_buf);
			return 0;
		}

		/*
		 *  Store IP.BIN at 0x8c008000, and set entry point.
		 *
		 *  Note: The boot block contains several executable parts:
		 *    Offset 0x0300-36ff:  SEGA logo code
		 *    Offset 0x3800-5FFF:  Bootstrap code 1
		 *    Offset 0x6000-7FFF:  Bootstrap code 2
		 *
		 *  (See http://mc.pp.se/dc/ip.bin.html for details.)
		 *
		 *  So one way to boot could be to set initial PC to 0x8c008300,
		 *  but that would only run the SEGA logo code and then return
		 *  to... well, to nothing.
		 *
		 *  Instead, initial PC is set to 0x8c000140, which triggers
		 *  software PROM emulation, which in turn:
		 *    a) calls 0x8c008300 to show the logo, and
		 *    b) calls 0x8c00b800 to set up registers etc and
		 *       this code will hopefully jump to 0x8c010000.
		 *
		 *  This mimics the behavior of the real Dreamcast.
		 */
		store_buf(cpu, 0x8c008000, (char *)bootblock_buf, 32768);
		cpu->pc = 0x8c000140;	// see src/promemul/dreamcast.cc for details

		/*  Remember the name of the file to boot (1ST_READ.BIN):  */
		if (cpu->machine->boot_kernel_filename == NULL ||
		    cpu->machine->boot_kernel_filename[0] == '\0') {
			int i = 0x60;
			while (i < 0x70) {
				if (bootblock_buf[i] == ' ')
					bootblock_buf[i] = 0;
				i ++;
			}
			CHECK_ALLOCATION(cpu->machine->boot_kernel_filename =
			    strdup((char *)bootblock_buf + 0x60));
		}

		debug("Dreamcast boot filename: %s (to be loaded to 0x8c010000)\n",
		    cpu->machine->boot_kernel_filename);

		free(bootblock_buf);

		break;

	case MACHINE_PMAX:
		/*
		 *  The first few bytes of a disk contains information about
		 *  where the bootblock(s) are located. (These are all 32-bit
		 *  little-endian words.)
		 *
		 *  Offset 0x10 = load address
		 *         0x14 = initial PC value
		 *         0x18 = nr of 512-byte blocks to read
		 *         0x1c = offset on disk to where the bootblocks
		 *                are (in 512-byte units)
		 *         0x20 = nr of blocks to read...
		 *         0x24 = offset...
		 *
		 *  nr of blocks to read and offset are repeated until nr of
		 *  blocks to read is zero.
		 *
		 *  TODO: Make use of src/include/thirdparty/bootblock.h now
		 *  that it is in the tree, rather than using hardcoded numbers.
		 */
		res = diskimage_access(m, boot_disk_id, boot_disk_type, 0, 0,
		    minibuf, sizeof(minibuf));

		bootblock_loadaddr = minibuf[0x10] + (minibuf[0x11] << 8)
		  + (minibuf[0x12] << 16) + ((uint64_t)minibuf[0x13] << 24);

		/*  Convert loadaddr to uncached:  */
		if ((bootblock_loadaddr & 0xf0000000ULL) != 0x80000000 &&
		    (bootblock_loadaddr & 0xf0000000ULL) != 0xa0000000) {
			fatal("\nWARNING! Weird load address 0x%08" PRIx32
			    " for SCSI id %i.\n\n",
			    (uint32_t)bootblock_loadaddr, boot_disk_id);
			if (bootblock_loadaddr == 0) {
				fatal("I'm assuming that this is _not_ a "
				    "DEC bootblock.\nAre you sure you are"
				    " booting from the correct disk?\n");
				exit(1);
			}
		}

		bootblock_loadaddr &= 0x0fffffffULL;
		bootblock_loadaddr |= 0xffffffffa0000000ULL;

		bootblock_pc = minibuf[0x14] + (minibuf[0x15] << 8)
		  + (minibuf[0x16] << 16) + ((uint64_t)minibuf[0x17] << 24);

		bootblock_pc &= 0x0fffffffULL;
		bootblock_pc |= 0xffffffffa0000000ULL;
		cpu->pc = bootblock_pc;

		debug("DEC boot: loadaddr=0x%08" PRIx32", pc=0x%08" PRIx32,
		    (uint32_t) bootblock_loadaddr, (uint32_t) bootblock_pc);

		readofs = 0x18;

		for (;;) {
			res = diskimage_access(m, boot_disk_id, boot_disk_type,
			    0, readofs, minibuf, sizeof(minibuf));
			if (!res) {
				fatal("Couldn't read the disk image. "
				    "Aborting.\n");
				return 0;
			}

			n_blocks = minibuf[0] + (minibuf[1] << 8)
			  + (minibuf[2] << 16) + ((uint64_t)minibuf[3] << 24);

			bootblock_offset = (minibuf[4] + (minibuf[5] << 8) +
			  (minibuf[6]<<16) + ((uint64_t)minibuf[7]<<24)) * 512;

			if (n_blocks < 1)
				break;

			debug(readofs == 0x18? ": %i" : " + %i", n_blocks);

			if (n_blocks * 512 > 65536)
				fatal("\nWARNING! Unusually large bootblock "
				    "(%i bytes)\n\n", n_blocks * 512);

			CHECK_ALLOCATION(bootblock_buf = (unsigned char *) malloc(n_blocks*512));

			res = diskimage_access(m, boot_disk_id, boot_disk_type,
			    0, bootblock_offset, bootblock_buf, n_blocks * 512);
			if (!res) {
				fatal("WARNING: could not load bootblocks from"
				    " disk offset 0x%llx\n",
				    (long long)bootblock_offset);
			}

			store_buf(cpu, bootblock_loadaddr,
			    (char *)bootblock_buf, n_blocks * 512);

			bootblock_loadaddr += 512*n_blocks;
			free(bootblock_buf);
			readofs += 8;
		}

		debug(readofs == 0x18? ": no blocks?\n" : " blocks\n");
		return 1;

	case MACHINE_SGI:
		/*
		 *  The first few bytes of a disk contains an "SGI boot block".
		 *  The data in the boot block are all big-endian.
		 *
		 *  See src/include/thirdparty/bootblock.h (from NetBSD) for
		 *  details.
		 */
		struct sgi_boot_block sgi_boot_block;
		uint8_t *sgi_bb_ptr = (uint8_t*)&sgi_bb_ptr;

		res = diskimage_access(m, boot_disk_id, boot_disk_type, 0, 0,
		    (unsigned char*)&sgi_boot_block, sizeof(sgi_boot_block));

		uint32_t magic = ntohl(sgi_boot_block.magic);
		if (magic != SGI_BOOT_BLOCK_MAGIC) {
			fatal("SGI boot block: wrong magic! (Not a SGI bootable disk image?)\n");
			return 0;
		}

		uint16_t sgi_root = ntohs(sgi_boot_block.root);
		uint16_t sgi_swap = ntohs(sgi_boot_block.swap);
		char sgi_bootfile[sizeof(sgi_boot_block.bootfile) + 1];

		memset(sgi_bootfile, 0, sizeof(sgi_bootfile));
		{
			size_t j = 0;
			for (size_t i = 0; i < sizeof(sgi_boot_block.bootfile); ++i) {
				char c = sgi_boot_block.bootfile[i];
				if (c < 32)
					break;
				sgi_bootfile[j++] = c;
			}
		}

		debug("SGI boot block:\n");
		debug_indentation(DEBUG_INDENTATION);
		debug("root partition: %i\n", sgi_root);
		debug("swap partition: %i\n", sgi_swap);
		debug("bootfile: %s\n", sgi_bootfile);

		// TODO: this should be in sync with what's in arcbios.cc,
		// in one direction or the other. Hardcoded for now...
		// or sashARCS or sash64? or ip3xboot (for NetBSD)? take from boot arg on command line?
		// maybe parse arguments:  "sash path()/unix"  <- first arg means OSLoader,
		// the rest mean argument passed to the OSLoader? When booting from
		// disk.
		const char* osloaderA = "sash";
		const char* osloaderB = "ip3xboot";
		string found_osloader;
		int32_t found_osloader_block = -1;
		int32_t found_osloader_bytes = -1;

		debug("voldir:\n");
		debug_indentation(DEBUG_INDENTATION);
		for (size_t vi = 0; vi < SGI_BOOT_BLOCK_MAXVOLDIRS; ++vi) {
			char voldir_name[sizeof(sgi_boot_block.voldir[0].name) + 1];
			int32_t voldir_block = ntohl(sgi_boot_block.voldir[vi].block);
			int32_t voldir_bytes = ntohl(sgi_boot_block.voldir[vi].bytes);

			memset(voldir_name, 0, sizeof(voldir_name));
			size_t j = 0;
			for (size_t i = 0; i < sizeof(sgi_boot_block.voldir[vi].name); ++i) {
				char c = sgi_boot_block.voldir[vi].name[i];
				if (c < 32)
					break;
				voldir_name[j++] = c;
			}

			if (voldir_name[0]) {
				bool found = false;
				if (strcmp(voldir_name, osloaderA) == 0 ||
				    strcmp(voldir_name, osloaderB) == 0) {
					found = true;
					found_osloader_block = voldir_block;
					found_osloader_bytes = voldir_bytes;
					found_osloader = voldir_name;
				}

				const char* found_suffix = found? " [FOUND OSLoader]" : "";
				debug("name: %s (%i bytes, block %i)%s\n",
					voldir_name, voldir_bytes, voldir_block, found_suffix);
			}
		}
		if (found_osloader_block < 1 || found_osloader_bytes < 512) {
			fatal("OSLoader \"%s\" (or \"%s\") NOT found in SGI voldir\n", osloaderA, osloaderB);
			return 0;
		}
		if (found_osloader_bytes & 511) {
			found_osloader_bytes |= 511;
			found_osloader_bytes++;
		}
		debug_indentation(-DEBUG_INDENTATION);

		debug("partitions:\n");
		debug_indentation(DEBUG_INDENTATION);
		for (size_t pi = 0; pi < SGI_BOOT_BLOCK_MAXPARTITIONS; ++pi) {
			int32_t partitions_blocks = ntohl(sgi_boot_block.partitions[pi].blocks);
			int32_t partitions_first = ntohl(sgi_boot_block.partitions[pi].first);
			int32_t partitions_type = ntohl(sgi_boot_block.partitions[pi].type);

			if (partitions_blocks != 0)
				debug("partition %i: %i blocks at %i (type %i)\n",
					pi, partitions_blocks, partitions_first, partitions_type);
		}
		debug_indentation(-DEBUG_INDENTATION);

		// Read OSLoader binary into emulated RAM. (Typically "sash.")
		uint64_t diskoffset = found_osloader_block * 512;
		debug("Loading voldir entry \"%s\", 0x%x bytes from disk offset 0x%x\n",
			found_osloader.c_str(), found_osloader_bytes, diskoffset);

		CHECK_ALLOCATION(bootblock_buf = (unsigned char *) malloc(found_osloader_bytes));

		res = diskimage_access(m, boot_disk_id, boot_disk_type,
		    0, diskoffset, bootblock_buf, found_osloader_bytes);
		if (!res) {
			fatal("WARNING: could not load \"%s\" from disk offset 0x%llx\n",
			    found_osloader.c_str(), (long long)diskoffset);
		}

		// Put loaded binary into a temp file, and make sure it is
		// loaded later by the regular file loader.
		char* tmpfname;
		CHECK_ALLOCATION(tmpfname = (char *) malloc(300));
		snprintf(tmpfname, 300, "%s/gxemul.XXXXXXXXXXXX", tmpdir);
		int tmpfile_handle = mkstemp(tmpfname);
		if (tmpfile_handle < 0) {
			fatal("could not create %s\n", tmpfname);
			exit(1);
		}

		if (write(tmpfile_handle, bootblock_buf, found_osloader_bytes) != found_osloader_bytes) {
			fatal("could not write to %s\n", tmpfname);
			perror("write");
			exit(1);
		}

		close(tmpfile_handle);
		free(bootblock_buf);

		debug("extracted %i bytes into %s\n", found_osloader_bytes, tmpfname);

		/*  Add the temporary filename to the load_namesp array:  */
		(*n_loadp)++;
		char **new_array;
		CHECK_ALLOCATION(new_array = (char **) malloc(sizeof(char *) * (*n_loadp)));
		memcpy(new_array, *load_namesp, sizeof(char *) * (*n_loadp));
		*load_namesp = new_array;

		/*  This adds a Backspace char in front of the filename; this
		    is a special hack which causes the file to be removed once
		    it has been loaded.  */
		CHECK_ALLOCATION(tmpfname = (char *) realloc(tmpfname, strlen(tmpfname) + 2));
		memmove(tmpfname + 1, tmpfname, strlen(tmpfname) + 1);
		tmpfname[0] = 8;

		(*load_namesp)[*n_loadp - 1] = strdup(tmpfname);

		free(tmpfname);

		debug_indentation(-DEBUG_INDENTATION);
		return 1;
	}


	/*
	 *  Try reading a kernel manually from the disk. The code here
	 *  does not rely on machine-dependent boot blocks etc.
	 */
	/*  ISO9660: (0x800 bytes at 0x8000 + base_offset)  */
	CHECK_ALLOCATION(bootblock_buf = (unsigned char *) malloc(0x800));
	res = diskimage_access(m, boot_disk_id, boot_disk_type,
	    0, base_offset + 0x8000, bootblock_buf, 0x800);
	if (!res) {
		fatal("Couldn't read the ISO header from the disk image. Aborting.\n");
		return 0;
	}

	iso_type = 0;
	if (strncmp((char *)bootblock_buf+1, "CD001", 5) == 0)
		iso_type = 1;
	if (strncmp((char *)bootblock_buf+1, "CDW01", 5) == 0)
		iso_type = 2;
	if (strncmp((char *)bootblock_buf+1, "CDROM", 5) == 0)
		iso_type = 3;

	if (iso_type != 0) {
		/*
		 *  If the user specified a kernel name, then load it from
		 *  disk.
		 */
		if (cpu->machine->boot_kernel_filename == NULL ||
		    cpu->machine->boot_kernel_filename[0] == '\0')
			fatal("\nISO9660 filesystem, but no kernel "
			    "specified? (Use the -j option.)\n");
		else
			retval = iso_load_bootblock(m, cpu, boot_disk_id,
			    boot_disk_type, iso_type, bootblock_buf,
			    n_loadp, load_namesp);
	}

	if (retval != 0)
		goto ret_ok;

	/*  Apple parition table:  */
	res = diskimage_access(m, boot_disk_id, boot_disk_type,
	    0, 0x0, bootblock_buf, 0x800);
	if (!res) {
		fatal("Couldn't read the disk image. Aborting.\n");
		return 0;
	}
	if (bootblock_buf[0x000] == 'E' && bootblock_buf[0x001] == 'R' &&
	    bootblock_buf[0x200] == 'P' && bootblock_buf[0x201] == 'M') {
		if (cpu->machine->boot_kernel_filename == NULL ||
		    cpu->machine->boot_kernel_filename[0] == '\0')
			fatal("\nApple partition table, but no kernel "
			    "specified? (Use the -j option.)\n");
		else
			retval = apple_load_bootblock(m, cpu, boot_disk_id,
			    boot_disk_type, n_loadp, load_namesp);
	}

ret_ok:
	free(bootblock_buf);
	return retval;
}


