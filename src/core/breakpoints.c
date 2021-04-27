/*
 *  Copyright (C) 2021  Anders Gavare.  All rights reserved.
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
 *  Helper functions for breakpoint handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "breakpoints.h"
#include "machine.h"
#include "misc.h"
#include "symbol.h"


/*
 *  breakpoints_parse_all():
 *
 *  Take the strings breakpoint_string[] and convert to addresses
 *  (and store them in breakpoint_addr[]).
 */
void breakpoints_parse_all(struct machine *m)
{
	for (size_t i = 0; i < m->breakpoints.n_addr_bp; i++) {
		bool string_flag = false;
		uint64_t dp = strtoull(m->breakpoints.addr_bp[i].string, NULL, 0);

		/*
		 *  If conversion resulted in 0, then perhaps it is a
		 *  symbol:
		 */
		if (dp == 0) {
			uint64_t addr;
			int res = get_symbol_addr(&m->symbol_context,
			    m->breakpoints.addr_bp[i].string, &addr);
			if (!res) {
				fprintf(stderr,
				    "ERROR! Breakpoint '%s' could not be"
					" parsed\n",
				    m->breakpoints.addr_bp[i].string);
				exit(1);
			} else {
				dp = addr;
				string_flag = true;
			}
		}

		/*
		 *  TODO:  It would be nice if things like   symbolname+0x1234
		 *  were automatically converted into the correct address.
		 */

		if (m->cpus[0]->cpu_family->arch == ARCH_MIPS) {
			if ((dp >> 32) == 0 && ((dp >> 31) & 1))
				dp |= 0xffffffff00000000ULL;
		}

		m->breakpoints.addr_bp[i].addr = dp;

		debugmsg(SUBSYS_STARTUP, "breakpoints", VERBOSITY_INFO,
		    "%zi: 0x%" PRIx64 " (%s)", i, dp,
		    string_flag ? m->breakpoints.addr_bp[i].string : "unknown");
	}
}



