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
 *  Intel 80960 (i960) CPU emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "cpu.h"
#include "float_emul.h"
#include "interrupt.h"
#include "machine.h"
#include "memory.h"
#include "misc.h"
#include "settings.h"
#include "symbol.h"


#define DYNTRANS_32
#include "tmp_i960_head.c"


void i960_pc_to_pointers(struct cpu *);
void i960_cpu_functioncall_trace(struct cpu *cpu, int n_args);

void i960_irq_interrupt_assert(struct interrupt *interrupt);
void i960_irq_interrupt_deassert(struct interrupt *interrupt);


/*
 *  i960_cpu_new():
 *
 *  Create a new 80960 cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid i960 processor model.
 */
int i960_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i;

	/*  TODO: Check cpu_type_name in a better way.  */
	if (strcmp(cpu_type_name, "i960") != 0)
		return 0;

	cpu->run_instr = i960_run_instr;
	cpu->memory_rw = i960_memory_rw;
	cpu->update_translation_table = i960_update_translation_table;
	cpu->invalidate_translation_caches = i960_invalidate_translation_caches;
	cpu->invalidate_code_translation = i960_invalidate_code_translation;

	cpu->name            = strdup(cpu_type_name);
	cpu->is_32bit        = 1;
	cpu->byte_order      = EMUL_BIG_ENDIAN;

	cpu->vaddr_mask = 0x00000000ffffffffULL;


	/*
	 *  Add register names as settings:
	 */

	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);

	for (i=0; i<N_I960_REGS; i++) {
		char name[10];
		snprintf(name, sizeof(name), "r%i", i);
		CPU_SETTINGS_ADD_REGISTER32(name, cpu->cd.i960.r[i]);
	}


	/*  Register the CPU interrupt pin:  */
	{
		struct interrupt templ;
		char name[50];
		snprintf(name, sizeof(name), "%s", cpu->path);

		memset(&templ, 0, sizeof(templ));
		templ.line = 0;
		templ.name = name;
		templ.extra = cpu;
		templ.interrupt_assert = i960_irq_interrupt_assert;
		templ.interrupt_deassert = i960_irq_interrupt_deassert;
		interrupt_handler_register(&templ);
	}

	return 1;
}


/*
 *  i960_cpu_dumpinfo():
 */
void i960_cpu_dumpinfo(struct cpu *cpu, bool verbose)
{
	debugmsg(SUBSYS_MACHINE, "cpu", VERBOSITY_INFO,
	    "%s",
	    cpu->name);
}


/*
 *  i960_cpu_list_available_types():
 *
 *  Print a list of available i960 CPU types.
 */
void i960_cpu_list_available_types(void)
{
}


/*
 *  i960_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void i960_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	char *symbol;
	uint64_t offset;
	int i, x = cpu->cpu_id;

	if (gprs) {
		symbol = get_symbol_name(&cpu->machine->symbol_context,
		    cpu->pc, &offset);
		debug("cpu%i:  pc  = 0x%08" PRIx32, x, (uint32_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		for (i=0; i<N_I960_REGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);
			if (i == 0)
				debug("                  ");
			else
				debug("  r%-2i = 0x%08" PRIx32,
				    i, cpu->cd.i960.r[i]);
			if ((i % 4) == 3)
				debug("\n");
		}
	}
}


/*
 *  i960_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void i960_cpu_tlbdump(struct cpu* cpu, int rawflag)
{
}


/*
 *  i960_irq_interrupt_assert():
 *  i960_irq_interrupt_deassert():
 */
void i960_irq_interrupt_assert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.i960.irq_asserted = 1;
}
void i960_irq_interrupt_deassert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.i960.irq_asserted = 0;
}


/*
 *  i960_exception():
 *
 *  Cause an exception.
 */
void i960_exception(struct cpu *cpu, int vector, int is_trap)
{
	debugmsg_cpu(cpu, SUBSYS_EXCEPTION, "", VERBOSITY_ERROR,
	    "i960_exception(): TODO");

	cpu->running = 0;

	i960_pc_to_pointers(cpu);
}


/*
 *  i960_cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 *              
 *  If running is 1, cpu->pc should be the address of the instruction.
 *
 *  If running is 0, things that depend on the runtime environment (eg.
 *  register contents) will not be shown, and dumpaddr will be used instead of
 *  cpu->pc for relative addresses.
 */                     
int i960_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr)
{
	debugmsg_cpu(cpu, SUBSYS_CPU, "", VERBOSITY_ERROR,
	    "i960_cpu_disassemble_instr(): TODO");

	return sizeof(uint32_t);
}


#include "tmp_i960_tail.c"


