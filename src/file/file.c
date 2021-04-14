/*
 *  Copyright (C) 2003-2021  Anders Gavare.  All rights reserved.
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
 *  This module contains functions which load executable images into (emulated)
 *  memory. File formats recognized so far are:
 *
 *	android		Android boot.img format
 *	a.out		traditional old-style Unix binary format
 *	Mach-O		MacOS X format, etc.
 *	ecoff		old format used by Ultrix, Windows NT, IRIX, etc
 *	srec		Motorola SREC format
 *	raw		raw binaries, "address:[skiplen:[entrypoint:]]filename"
 *	ELF		32-bit and 64-bit ELFs
 *
 *  If a file is not of one of the above mentioned formats, it is assumed
 *  to be symbol data generated by 'nm' or 'nm -S'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "cpu.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "symbol.h"


extern int quiet_mode;
extern int verbose;


/*
 *  This should be increased by every routine here that actually loads an
 *  executable file into memory.  (For example, loading a symbol file should
 *  NOT increase this.)
 */
static int n_executables_loaded = 0;


#include "thirdparty/exec_elf.h"		/*  Ugly; needed for ELFDATA2LSB etc.  */

#define	unencode(var,dataptr,typ)	{				\
		int Wi;  unsigned char Wb;				\
		unsigned char *Wp = (unsigned char *) dataptr;		\
		int Wlen = sizeof(typ);					\
		var = 0;						\
		for (Wi=0; Wi<Wlen; Wi++) {				\
			if (encoding == ELFDATA2LSB)			\
				Wb = Wp[Wlen-1 - Wi];			\
			else						\
				Wb = Wp[Wi];				\
			if (Wi == 0 && (Wb & 0x80)) {			\
				var --;	/*  set var to -1 :-)  */	\
				var <<= 8;				\
			}						\
			var |= Wb;					\
			if (Wi < Wlen-1)				\
				var <<= 8;				\
		}							\
	}


#include "file_android.c"
#include "file_aout.c"
#include "file_ecoff.c"
#include "file_elf.c"
#include "file_macho.c"
#include "file_raw.c"
#include "file_srec.c"


/*
 *  file_n_executables_loaded():
 *
 *  Returns the number of executable files loaded into emulated memory.
 */
int file_n_executables_loaded(void)
{
	return n_executables_loaded;
}


/*
 *  file_load():
 *
 *  Sense the file format of a file (ELF, a.out, ecoff), and call the
 *  right file_load_XXX() function.  If the file isn't of a recognized
 *  binary format, assume that it contains symbol definitions.
 *
 *  If the filename doesn't exist, try to treat the name as
 *   "address:filename" and load the file as a raw binary.
 */
void file_load(struct machine *machine, struct memory *mem,
	char *filename, uint64_t *entrypointp,
	int arch, uint64_t *gpp, int *byte_orderp, uint64_t *tocp)
{
	int old_quiet_mode;
	FILE *f;
	const char *tmpdir = getenv("TMPDIR") == NULL?
	    DEFAULT_TMP_DIR : getenv("TMPDIR");
	unsigned char buf[12];
	unsigned char buf2[2];
	char tmpname[400];
	size_t len, len2, i;
	off_t size;

	if (byte_orderp == NULL) {
		fprintf(stderr, "file_load(): byte_order == NULL\n");
		exit(1);
	}

	if (!arch) {
		fprintf(stderr, "file_load(): FATAL ERROR: no arch?\n");
		exit(1);
	}

	if (mem == NULL || filename == NULL) {
		fprintf(stderr, "file_load(): mem or filename is NULL\n");
		exit(1);
	}

	/*  Skip configuration files:  */
	if (filename[0] == '@')
		return;

	debugmsg(SUBSYS_MACHINE, "file", VERBOSITY_INFO, "loading %s%s", filename, verbose >= 2? ":" : "");
	debug_indentation(1);

	old_quiet_mode = quiet_mode;
	if (verbose < 1)
		quiet_mode = 1;

	f = fopen(filename, "r");
	if (f == NULL) {
		file_load_raw(machine, mem, filename, entrypointp);
		goto ret;
	}

	fseek(f, 0, SEEK_END);
	size = ftello(f);
	fseek(f, 0, SEEK_SET);

	memset(buf, 0, sizeof(buf));
	len = fread(buf, 1, sizeof(buf), f);
	fseek(f, 510, SEEK_SET);
	len2 = fread(buf2, 1, sizeof(buf2), f);
	fclose(f);

	if (len < (signed int)sizeof(buf)) {
		fprintf(stderr, "\nThis file is too small to contain "
		    "anything useful\n");
		exit(1);
	}

	/*  Is it an ELF?  */
	if (buf[0] == 0x7f && buf[1]=='E' && buf[2]=='L' && buf[3]=='F') {
		file_load_elf(machine, mem, filename,
		    entrypointp, arch, gpp, byte_orderp, tocp);
		goto ret;
	}

	/*  Is it an Android boot.img?  */
	if (memcmp("ANDROID!", &buf[0], 8) == 0) {
		file_load_android(machine, mem, filename, 0,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}

	/*  Is it an a.out?  */
	if (buf[0]==0x00 && buf[1]==0x8b && buf[2]==0x01 && buf[3]==0x07) {
		/*  MIPS a.out  */
		file_load_aout(machine, mem, filename, 0,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if ((buf[0]==0x0d && buf[1]==0x01 && buf[2]==0x00 && buf[3]==0x00) ||
	    (buf[0]==0x00 && buf[1]==0x00 && buf[2]==0x01 && buf[3]==0x0d)) {
		/*  i960 b.out  */
		machine->cpus[0]->byte_order = buf[0] == 0x0d ? EMUL_LITTLE_ENDIAN : EMUL_BIG_ENDIAN;
		file_load_aout(machine, mem, filename,
		    AOUT_FLAG_I960_BOUT, entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x00 && buf[1]==0x87 && buf[2]==0x01 && buf[3]==0x08) {
		/*  M68K a.out  */
		file_load_aout(machine, mem, filename,
		    AOUT_FLAG_VADDR_ZERO_HACK  /*  for OpenBSD/mac68k  */,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x00 && buf[1]==0x99 && buf[2]==0x01 && buf[3]==0x07) {
		/*  OpenBSD/M88K a.out  */
		file_load_aout(machine, mem, filename, AOUT_FLAG_DATA_AT_END_MAY_BE_OMITTED,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x00 && buf[1]==0x99 && buf[2]==0x01 && buf[3]==0x0b) {
		/*  OpenBSD/M88K a.out  */
		file_load_aout(machine, mem, filename, AOUT_FLAG_FROM_BEGINNING,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x00 && buf[1]==0x8f && buf[2]==0x01 && buf[3]==0x0b) {
		/*  ARM a.out  */
		file_load_aout(machine, mem, filename, AOUT_FLAG_FROM_BEGINNING,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x00 && buf[1]==0x86 && buf[2]==0x01 && buf[3]==0x0b) {
		/*  i386 a.out (old OpenBSD and NetBSD etc)  */
		file_load_aout(machine, mem, filename, AOUT_FLAG_FROM_BEGINNING,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x01 && buf[1]==0x03 && buf[2]==0x01 && buf[3]==0x07) {
		/*  SPARC a.out (old 32-bit NetBSD etc)  */
		file_load_aout(machine, mem, filename, AOUT_FLAG_NO_SIZES,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}
	if (buf[0]==0x00 && buf[2]==0x00 && buf[8]==0x7a && buf[9]==0x75) {
		/*  DEC OSF1 on MIPS:  */
		file_load_aout(machine, mem, filename,
		    AOUT_FLAG_DECOSF1 | AOUT_FLAG_DATA_AT_END_MAY_BE_OMITTED,
		    entrypointp, arch, byte_orderp);
		goto ret;
	}

	/*
	 *  Is it a Mach-O file?
	 */
	if (buf[0] == 0xfe && buf[1] == 0xed && buf[2] == 0xfa &&
	    (buf[3] == 0xce || buf[3] == 0xcf)) {
		file_load_macho(machine, mem, filename, entrypointp,
		    arch, byte_orderp, buf[3] == 0xcf, 0);
		goto ret;
	}
	if ((buf[0] == 0xce || buf[0] == 0xcf) && buf[1] == 0xfa &&
	    buf[2] == 0xed && buf[3] == 0xfe) {
		file_load_macho(machine, mem, filename, entrypointp,
		    arch, byte_orderp, buf[0] == 0xcf, 1);
		goto ret;
	}

	/*
	 *  Is it an ecoff?
	 *
	 *  TODO: What's the deal with the magic value's byte order? Sometimes
	 *  it seems to be reversed for BE when compared to LE, but not always?
	 */
	if (buf[0]+256*buf[1] == ECOFF_MAGIC_MIPSEB ||
	    buf[0]+256*buf[1] == ECOFF_MAGIC_MIPSEL ||
	    buf[0]+256*buf[1] == ECOFF_MAGIC_MIPSEB2 ||
	    buf[0]+256*buf[1] == ECOFF_MAGIC_MIPSEL2 ||
	    buf[0]+256*buf[1] == ECOFF_MAGIC_MIPSEB3 ||
	    buf[0]+256*buf[1] == ECOFF_MAGIC_MIPSEL3 ||
	    buf[1]+256*buf[0] == ECOFF_MAGIC_MIPSEB ||
	    buf[1]+256*buf[0] == ECOFF_MAGIC_MIPSEL ||
	    buf[1]+256*buf[0] == ECOFF_MAGIC_MIPSEB2 ||
	    buf[1]+256*buf[0] == ECOFF_MAGIC_MIPSEL2 ||
	    buf[1]+256*buf[0] == ECOFF_MAGIC_MIPSEB3 ||
	    buf[1]+256*buf[0] == ECOFF_MAGIC_MIPSEL3) {
		file_load_ecoff(machine, mem, filename, entrypointp,
		    arch, gpp, byte_orderp);
		goto ret;
	}

	/*  Is it a Motorola SREC file?  */
	if ((buf[0]=='S' && buf[1]>='0' && buf[1]<='9')) {
		file_load_srec(machine, mem, filename, entrypointp);
		goto ret;
	}

	/*  gzipped files are not supported:  */
	if (buf[0]==0x1f && buf[1]==0x8b) {
		fprintf(stderr, "\nYou need to gunzip the file before you"
		    " try to use it.\n");
		exit(1);
	}

	if (size > 24000000) {
		fprintf(stderr, "\nThis file is very large (%lli bytes)\n",
		    (long long)size);
		fprintf(stderr, "Are you sure it is a kernel and not a disk "
		    "image? (Use the -d option.)\n");
		exit(1);
	}

	if (size == 1474560)
		fprintf(stderr, "Hm... this file is the size of a 1.44 MB "
		    "floppy image. Maybe you forgot the\n-d switch?\n");

	/*
	 *  Ugly hack for Dreamcast:  When booting from a Dreamcast CDROM
	 *  image, a temporary file is extracted into /tmp/gxemul.*, but this
	 *  is a "scrambled" raw binary. This code unscrambles it, and loads
	 *  it as a raw binary.
	 */
	snprintf(tmpname, sizeof(tmpname), "%s/gxemul.", tmpdir);
	if (machine->machine_type == MACHINE_DREAMCAST &&
	    strncmp(filename, tmpname, strlen(tmpname)) == 0) {
		char *tmp_filename = (char *) malloc(strlen(filename) + 100);
		snprintf(tmp_filename, strlen(filename) + 100,
		    "%s.descrambled", filename);
		debug("descrambling into %s\n", tmp_filename);
		dreamcast_descramble(filename, tmp_filename);

		snprintf(tmp_filename, strlen(filename) + 100,
		    "0x8c010000:%s.descrambled", filename);
		debug("loading descrambled Dreamcast binary\n");
		file_load_raw(machine, mem, tmp_filename, entrypointp);

		snprintf(tmp_filename, strlen(filename) + 100,
		    "%s.descrambled", filename);
		remove(tmp_filename);
		free(tmp_filename);

		/*  Hack: Start a "boot from CDROM" sequence:  */
		*entrypointp = 0x8c000140;
		goto ret;
	}

	/*
	 *  Last resort:  symbol definitions from nm (or nm -S):
	 *
	 *  If the buf contains typical 'binary' characters, then print
	 *  an error message and quit instead of assuming that it is a
	 *  symbol file.
	 */
	for (i=0; i<(signed)sizeof(buf); i++)
		if (buf[i] < 32 && buf[i] != '\t' &&
		    buf[i] != '\n' && buf[i] != '\r' &&
		    buf[i] != '\f') {
			fprintf(stderr, "\nThe file format of '%s' is "
			    "unknown.\n\n ", filename);
			for (i=0; i<(signed)sizeof(buf); i++)
				fprintf(stderr, " %02x", buf[i]);

			if (len2 == 2 && buf2[0] == 0x55 && buf2[1] == 0xaa)
				fprintf(stderr, "\n\nIt has a PC-style "
				    "bootsector marker.");

			fprintf(stderr, "\n\nPossible explanations:\n\n"
			    "  o)  If this is a disk image, you forgot '-d' "
			    "on the command line.\n"
			    "  o)  You are attempting to load a raw binary "
			    "into emulated memory,\n"
			    "      but forgot to add the address prefix.\n"
			    "  o)  This is an unsupported binary format.\n\n");
			exit(1);
		}

	symbol_readfile(&machine->symbol_context, filename);

ret:
	debug_indentation(-1);
	quiet_mode = old_quiet_mode;
}

