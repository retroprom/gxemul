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
 *  $Id: cp_removeblocks.c,v 1.4 2004-07-11 00:42:37 debug Exp $
 *
 *  This program copies a file, but only those blocks that are not zero-
 *  filled.  Typical usage would be if you have a harddisk image stored
 *  as a file, which has all its zeroed blocks explicitly saved on disk,
 *  and would like to save space.
 *
 *  Example:  You download a file called diskimage.gz from somewhere,
 *            run gunzip on it, and the resulting file diskimage is
 *            1 GB large.  ls -l reports the file size as 1 GB, and
 *            so does 'du -k diskimage'.  If a lot of the space used
 *            by diskimage is actually zeroes, those parts do not need
 *            to actually be saved. By running this program on diskimage:
 *
 *                ./cp_removeblocks diskimage diskimage_compact
 *
 *            you will get a file with the same functionality, but possibly
 *            using less disk space.  ('ls -l diskimage_compact' should
 *            return the same size as for diskimage, but 'du -k' will
 *            print only how many kb the file takes up on your disk.)
 *
 *	      You don't even need to gunzip the file to be 1 GB first,
 *            you can pipe it through cp_removeblocks directly.
 *
 *		  gunzip -c file.gz | ./cp_removeblocks /dev/stdin output
 *
 *	      but then the filesize might be wrong. If it is, then you might
 *	      want to write the last byte manually using 'dd' or such.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define	BSIZE	512


int main(int argc, char *argv[])
{
	FILE *f1, *f2;
	unsigned char buf[BSIZE];
	off_t len;
	off_t in_pos = 0;
	int i, res;

	if (argc != 3) {
		fprintf(stderr, "usage: %s infile outfile\n", argv[0]);
		exit(1);
	}

	f1 = fopen(argv[1], "r");
	f2 = fopen(argv[2], "w");

	while (!feof(f1)) {
		len = fread(buf, 1, BSIZE, f1);

		if (len > 0) {
			/*  Check for data in buf:  */
			for (i=0; i<len; i++)
				if (buf[i])
					break;

			if (i < len) {
				fseek(f2, in_pos, SEEK_SET);
				fwrite(buf, 1, len, f2);
			}

			in_pos += len;
		}
	}

	/*
	 *  Copy the last byte explicitly.
	 *  (This causes f2 to get the correct file size.)
	 */
	res = fseek(f1, -1, SEEK_END);
	if (res != 0)
		perror("fseek(f1)");

	in_pos = ftell(f1);
	if (in_pos > 0) {
		res = fseek(f2, in_pos - 1, SEEK_SET);
		if (res != 0)
			perror("fseek(f2)");

		fread(&buf[0], 1, 1, f1);
		fwrite(&buf[0], 1, 1, f2);
	}

	fclose(f1);
	fclose(f2);

	return 0;
}

