#ifndef	BREAKPOINTS_H
#define	BREAKPOINTS_H

/*
 *  Copyright (C) 2005-2021  Anders Gavare.  All rights reserved.
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
 */

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "misc.h"

struct address_breakpoint {
	char		*string;
	uint64_t	addr;

	uint64_t	total_hit_count;
	uint64_t	current_hit_count;

	bool		break_execution;
	uint64_t	every_n_hits;
};

struct breakpoints {
	size_t				n_addr_bp;
	struct address_breakpoint	*addr_bp;
};


struct machine;

void breakpoints_show(struct machine *, size_t i);
void breakpoints_show_all(struct machine *);
void breakpoints_parse_all(struct machine *);
void breakpoints_add_without_lookup(struct machine *, const char *);
bool breakpoints_add(struct machine *, const char *string);
void breakpoints_delete(struct machine *machine, size_t i);


#endif	/*  BREAKPOINTS_H  */
