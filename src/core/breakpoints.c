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
#include "debugger.h"
#include "breakpoints.h"
#include "machine.h"
#include "misc.h"
#include "symbol.h"


static void breakpoint_init(struct address_breakpoint *bp, const char *string, uint64_t addr)
{
	memset(bp, 0, sizeof(struct address_breakpoint));

	size_t breakpoint_buf_len = strlen(string) + 1;
	CHECK_ALLOCATION(bp->string = (char *) malloc(breakpoint_buf_len));
	strlcpy(bp->string, string, breakpoint_buf_len);

	bp->addr = addr;
	bp->every_n_hits = 1;
	bp->total_hit_count = 0;
	bp->current_hit_count = 0;
	bp->break_execution = true;
}


/*
 *  breakpoints_show():
 */
void breakpoints_show(struct machine *m, size_t i)
{
	struct address_breakpoint *bp = &m->breakpoints.addr_bp[i];

	printf("  bp %zi: 0x", i);
	if (m->cpus[0]->is_32bit)
		printf("%08" PRIx32, (uint32_t) bp->addr);
	else
		printf("%016" PRIx64, (uint64_t) bp->addr);

	if (bp->string != NULL)
		printf(" (%s%s%s)", color_symbol_ptr(), bp->string, color_normal_ptr());

	if (bp->total_hit_count > 0)
		printf("\thits: %lli", (long long)bp->total_hit_count);

	if (bp->every_n_hits == 0) {
		printf("\t(just count)");
	} else if (bp->every_n_hits != 1) {
		printf("\t(current hits: %lli, %s every %lli hits)",
		    (long long)bp->current_hit_count,
		    bp->break_execution ? "break" : "print",
		    (long long)bp->every_n_hits);
	} else {
		printf("\t(%s on each hit)",
		    bp->break_execution ? "break" : "print");
	}

	printf("\n");
}


/*
 *  breakpoints_show_all():
 */
void breakpoints_show_all(struct machine *m)
{
	for (size_t i = 0; i < m->breakpoints.n_addr_bp; i++)
		breakpoints_show(m, i);
}


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


/*
 *  breakpoints_add_without_lookup():
 *
 *  Add a breakpoint string to the machine, without looking up address. Used
 *  from main(). Later, breakpoints_parse_all will be called to convert these
 *  to actual addresses.
 */
void breakpoints_add_without_lookup(struct machine *machine, const char *str)
{
	int n = machine->breakpoints.n_addr_bp + 1;

	size_t newsize = sizeof(struct address_breakpoint) * n;

	CHECK_ALLOCATION(machine->breakpoints.addr_bp = (struct address_breakpoint *)
	    realloc(machine->breakpoints.addr_bp, newsize));

	breakpoint_init(&machine->breakpoints.addr_bp[n-1], str, 0);

	machine->breakpoints.n_addr_bp = n;
}


/*
 *  breakpoints_add():
 */
bool breakpoints_add(struct machine *m, const char *string)
{
	size_t i = m->breakpoints.n_addr_bp;

	uint64_t tmp;
	int res = debugger_parse_expression(m, string, 0, &tmp);
	if (!res) {
		printf("Couldn't parse '%s'\n", string);
		return false;
	}

	CHECK_ALLOCATION(m->breakpoints.addr_bp = (struct address_breakpoint *) realloc(
	    m->breakpoints.addr_bp, sizeof(struct address_breakpoint) *
	   (m->breakpoints.n_addr_bp + 1)));

	struct address_breakpoint *bp = &m->breakpoints.addr_bp[i];

	breakpoint_init(bp, string, tmp);

	m->breakpoints.n_addr_bp ++;

	/*  Clear translations:  */
	for (int j = 0; j < m->ncpus; j++)
		if (m->cpus[j]->translation_cache != NULL)
			cpu_create_or_reset_tc(m->cpus[j]);

	return true;
}


/*
 *  breakpoints_delete():
 */
void breakpoints_delete(struct machine *m, size_t i)
{
	if (i >= m->breakpoints.n_addr_bp) {
		printf("Invalid breakpoint nr %i. Use 'breakpoint "
		    "show' to see the current breakpoints.\n", (int)i);
		return;
	}

	free(m->breakpoints.addr_bp[i].string);

	for (size_t j = i; j < m->breakpoints.n_addr_bp - 1; j++)
		m->breakpoints.addr_bp[j] = m->breakpoints.addr_bp[j+1];

	m->breakpoints.n_addr_bp --;

	/*  Clear translations:  */
	for (int j = 0; j < m->ncpus; j++)
		if (m->cpus[j]->translation_cache != NULL)
			cpu_create_or_reset_tc(m->cpus[j]);
}



