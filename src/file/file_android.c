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
 *  COMMENT: Android boot.img file support
 */

/*  Note: Included from file.c.  */


struct android_header {
	uint8_t		magic[8];

	uint32_t	kernel_size;
	uint32_t	kernel_addr;
	uint32_t	ramdisk_size;
	uint32_t	ramdisk_addr;
	uint32_t	second_size;
	uint32_t	second_addr;

	uint32_t	tags_addr;
	uint32_t	page_size;

	uint32_t	header_version;	/*  0: Pre-Android-9   1: Android 9 or higher  */
	uint32_t	os_version;

	uint8_t		name[16];
	uint8_t		cmdline[512];
	uint32_t	id[8];
	uint8_t		extra_cmdline[1024];

	/*  Android 9 or higher:  */
	uint32_t	recovery_dtbo_size;
	uint64_t	recovery_dtbo_offset;
	uint32_t	header_size;
};


/*
 *  file_load_android():
 *
 *  Loads an Android boot.img file into the emulated memory.  The entry point
 *  is stored in the specified CPU's registers.
 *
 *  See https://source.android.com/devices/bootloader/recovery-image and
 *  https://source.android.com/devices/bootloader/boot-image-header for more
 *  details.
 */
static void file_load_android(struct machine *m, struct memory *mem,
	char *filename, int flags,
	uint64_t *entrypointp, int arch, int *byte_orderp)
{
	FILE *f;
	int encoding = ELFDATA2LSB;
	uint32_t page_size;
	uint32_t kernel_size, kernel_addr, kernel_pages;
	uint32_t ramdisk_size, ramdisk_addr, ramdisk_pages;
	uint32_t second_size, second_addr, second_pages;
	struct android_header android_header;
	unsigned char buf[65536];

	f = fopen(filename, "r");
	if (f == NULL) {
		perror(filename);
		exit(1);
	}

	int hlen = fread(&android_header, 1, sizeof(android_header), f);
	if (hlen != sizeof(android_header)) {
		fprintf(stderr, "%s: not a complete Android boot.img file\n",
		    filename);
		exit(1);
	}

	unencode(page_size, &android_header.page_size, uint32_t);
	debug("Android boot.img format, page size 0x%x\n", page_size);

	unencode(kernel_size, &android_header.kernel_size, uint32_t);
	unencode(kernel_addr, &android_header.kernel_addr, uint32_t);
	kernel_pages = (kernel_size + (page_size - 1)) / page_size;
	if (kernel_size > 0) {
		debug("kernel: 0x%x bytes (%i pages) at addr 0x%08x\n", kernel_size, kernel_pages, kernel_addr);

		fseek(f, page_size * 1, SEEK_SET);

		uint32_t len_to_load = kernel_size;
		uint32_t vaddr = kernel_addr;

		while (len_to_load != 0) {
			int len = len_to_load > sizeof(buf) ? sizeof(buf) : len_to_load;
			int len_read = fread(buf, 1, len, f);
			if (len != len_read) {
				fprintf(stderr, "could not read from %s\n", filename);
				exit(1);
			}

			/*  printf("fread len=%i vaddr=%x buf[0..]=%02x %02x %02x\n",
			    (int)len, (int)vaddr, buf[0], buf[1], buf[2]);  */

			int len2 = 0;
			uint64_t vaddr1 = vaddr &
			    ((1 << BITS_PER_MEMBLOCK) - 1);
			uint64_t vaddr2 = (vaddr +
			    len) & ((1 << BITS_PER_MEMBLOCK) - 1);
			if (vaddr2 < vaddr1) {
				len2 = len - vaddr2;
				m->cpus[0]->memory_rw(m->cpus[0], mem, vaddr,
				    &buf[0], len2, MEM_WRITE, NO_EXCEPTIONS);
			}
			m->cpus[0]->memory_rw(m->cpus[0], mem, vaddr + len2,
			    &buf[len2], len-len2, MEM_WRITE, NO_EXCEPTIONS);

			vaddr += len;
			len_to_load -= len;
		}
	} else {
		fatal("kernel_size = 0?\n");
		exit(1);
	}

	unencode(ramdisk_size, &android_header.ramdisk_size, uint32_t);
	unencode(ramdisk_addr, &android_header.ramdisk_addr, uint32_t);
	ramdisk_pages = (ramdisk_size + (page_size - 1)) / page_size;
	if (ramdisk_size > 0) {
		debug("ramdisk: 0x%x bytes (%i pages) at addr 0x%08x\n", ramdisk_size, ramdisk_pages, ramdisk_addr);

		fseek(f, page_size * (1 + kernel_pages), SEEK_SET);

		uint32_t len_to_load = ramdisk_size;
		uint32_t vaddr = ramdisk_addr;

		while (len_to_load != 0) {
			int len = len_to_load > sizeof(buf) ? sizeof(buf) : len_to_load;
			int len_read = fread(buf, 1, len, f);
			if (len != len_read) {
				fprintf(stderr, "could not read from %s\n", filename);
				exit(1);
			}

			/*  printf("fread len=%i vaddr=%x buf[0..]=%02x %02x %02x\n",
			    (int)len, (int)vaddr, buf[0], buf[1], buf[2]);  */

			int len2 = 0;
			uint64_t vaddr1 = vaddr &
			    ((1 << BITS_PER_MEMBLOCK) - 1);
			uint64_t vaddr2 = (vaddr +
			    len) & ((1 << BITS_PER_MEMBLOCK) - 1);
			if (vaddr2 < vaddr1) {
				len2 = len - vaddr2;
				m->cpus[0]->memory_rw(m->cpus[0], mem, vaddr,
				    &buf[0], len2, MEM_WRITE, NO_EXCEPTIONS);
			}
			m->cpus[0]->memory_rw(m->cpus[0], mem, vaddr + len2,
			    &buf[len2], len-len2, MEM_WRITE, NO_EXCEPTIONS);

			vaddr += len;
			len_to_load -= len;
		}
	}

	unencode(second_size, &android_header.second_size, uint32_t);
	unencode(second_addr, &android_header.second_addr, uint32_t);
	second_pages = (second_size + (page_size - 1)) / page_size;
	if (second_size > 0) {
		debug("second: 0x%x bytes (%i pages) at addr 0x%08x\n", second_size, second_pages, second_addr);

		fseek(f, page_size * (1 + kernel_pages + ramdisk_pages), SEEK_SET);

		uint32_t len_to_load = second_size;
		uint32_t vaddr = second_addr;

		while (len_to_load != 0) {
			int len = len_to_load > sizeof(buf) ? sizeof(buf) : len_to_load;
			int len_read = fread(buf, 1, len, f);
			if (len != len_read) {
				fprintf(stderr, "could not read from %s\n", filename);
				exit(1);
			}

			/*  printf("fread len=%i vaddr=%x buf[0..]=%02x %02x %02x\n",
			    (int)len, (int)vaddr, buf[0], buf[1], buf[2]);  */

			int len2 = 0;
			uint64_t vaddr1 = vaddr &
			    ((1 << BITS_PER_MEMBLOCK) - 1);
			uint64_t vaddr2 = (vaddr +
			    len) & ((1 << BITS_PER_MEMBLOCK) - 1);
			if (vaddr2 < vaddr1) {
				len2 = len - vaddr2;
				m->cpus[0]->memory_rw(m->cpus[0], mem, vaddr,
				    &buf[0], len2, MEM_WRITE, NO_EXCEPTIONS);
			}
			m->cpus[0]->memory_rw(m->cpus[0], mem, vaddr + len2,
			    &buf[len2], len-len2, MEM_WRITE, NO_EXCEPTIONS);

			vaddr += len;
			len_to_load -= len;
		}
	}

	fclose(f);

	*entrypointp = (int32_t)kernel_addr;

	if (encoding == ELFDATA2LSB)
		*byte_orderp = EMUL_LITTLE_ENDIAN;
	else
		*byte_orderp = EMUL_BIG_ENDIAN;

	n_executables_loaded ++;
}

