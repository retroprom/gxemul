#ifndef	DISKIMAGE_H
#define	DISKIMAGE_H

/*
 *  Copyright (C) 2003 by Anders Gavare.  All rights reserved.
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
 *  $Id: diskimage.h,v 1.5 2004-01-11 23:54:37 debug Exp $
 *
 *  Generic disk image functions.  (See diskimage.c for more info.)
 */

#include <sys/types.h>

int diskimage_add(char *fname);
int64_t diskimage_getsize(int disk_id);
int diskimage_scsicommand(int disk_id, unsigned char *buf, int len, unsigned char **return_buf_ptr, int *return_len);
int diskimage_access(int disk_id, int writeflag, off_t offset, unsigned char *buf, size_t len);
int diskimage_exist(int disk_id);
void diskimage_dump_info(void);

/*  SCSI commands:  */
#define	SCSICMD_TEST_UNIT_READY	0x00
#define	SCSICMD_INQUIRY		0x12

/*  SCSI block device commands:  */
#define	SCSIBLOCKCMD_READ_CAPACITY	0x25

#endif	/*  DISKIMAGE_H  */
