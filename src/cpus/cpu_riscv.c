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
 *  RISC-V CPU emulation.
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


#define DYNTRANS_DUALMODE_32

#include "tmp_riscv_head.c"

void riscv_pc_to_pointers(struct cpu *);
void riscv32_pc_to_pointers(struct cpu *);
void riscv_cpu_functioncall_trace(struct cpu *cpu, int n_args);

void riscv_irq_interrupt_assert(struct interrupt *interrupt);
void riscv_irq_interrupt_deassert(struct interrupt *interrupt);


/*
 *  riscv_cpu_new():
 *
 *  Create a new RISC-V cpu object by filling the CPU struct.
 *  Return 1 on success, 0 if cpu_type_name isn't a valid processor model.
 */
int riscv_cpu_new(struct cpu *cpu, struct memory *mem,
	struct machine *machine, int cpu_id, char *cpu_type_name)
{
	int i;

	/*  TODO: Check cpu_type_name in a better way.  */
	if (strcmp(cpu_type_name, "riscv") != 0)
		return 0;

	cpu->run_instr = riscv_run_instr;
	cpu->memory_rw = riscv_memory_rw;
	cpu->update_translation_table = riscv_update_translation_table;
	cpu->invalidate_translation_caches = riscv_invalidate_translation_caches;
	cpu->invalidate_code_translation = riscv_invalidate_code_translation;

	cpu->name            = strdup(cpu_type_name);

	cpu->byte_order      = EMUL_BIG_ENDIAN;

	// TODO
	cpu->is_32bit        = 1;
	cpu->vaddr_mask = 0x00000000ffffffffULL;

	cpu->is_32bit        = 0;
	cpu->vaddr_mask = 0xffffffffffffffffULL;


	/*
	 *  Add register names as settings:
	 */

	CPU_SETTINGS_ADD_REGISTER64("pc", cpu->pc);

	for (i=0; i<N_RISCV_REGS; i++) {
		char name[10];
		snprintf(name, sizeof(name), "x%i", i);

		// TODO: skip x0 and add using the symbolic API names?
		CPU_SETTINGS_ADD_REGISTER64(name, cpu->cd.riscv.x[i]);
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
		templ.interrupt_assert = riscv_irq_interrupt_assert;
		templ.interrupt_deassert = riscv_irq_interrupt_deassert;
		interrupt_handler_register(&templ);
	}

	return 1;
}


/*
 *  riscv_cpu_dumpinfo():
 */
void riscv_cpu_dumpinfo(struct cpu *cpu, bool verbose)
{
	debugmsg(SUBSYS_MACHINE, "cpu", VERBOSITY_INFO,
	    "%s",
	    cpu->name);
}


/*
 *  riscv_cpu_list_available_types():
 *
 *  Print a list of available riscv CPU types.
 */
void riscv_cpu_list_available_types(void)
{
	debug("riscv\n");
}


/*
 *  riscv_cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *  
 *  gprs: set to non-zero to dump GPRs and some special-purpose registers.
 *  coprocs: set bit 0..3 to dump registers in coproc 0..3.
 */
void riscv_cpu_register_dump(struct cpu *cpu, int gprs, int coprocs)
{
	if (gprs) {
		uint64_t offset;
		const char *symbol = get_symbol_name(&cpu->machine->symbol_context, cpu->pc, &offset);

		debug("cpu%i:  pc  = ", cpu->cpu_id);

		if (cpu->is_32bit)
			debug("0x%08" PRIx32, (uint32_t) cpu->pc);
		else
			debug("0x%016" PRIx64, (uint64_t) cpu->pc);

		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		int n_regs_per_line = cpu->is_32bit ? 4 : 2;

		for (int i = 0; i < N_RISCV_REGS; i++) {
			if ((i % n_regs_per_line) == 0)
				debug("cpu%i:", cpu->cpu_id);

			debug("  ");

			if (i == 0) {
				debug("      ");

				if (cpu->is_32bit)
					debug("          ");
				else
					debug("                  ");
			} else {
				debug("x%-2i = ", i);

				if (cpu->is_32bit)
					debug("0x%08" PRIx32, (uint32_t) cpu->cd.riscv.x[i]);
				else
					debug("0x%016" PRIx64, (uint64_t) cpu->cd.riscv.x[i]);
			}

			if ((i % n_regs_per_line) == n_regs_per_line-1)
				debug("\n");
		}
	}
}


/*
 *  riscv_cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void riscv_cpu_tlbdump(struct cpu* cpu, int rawflag)
{
}


/*
 *  riscv_irq_interrupt_assert():
 *  riscv_irq_interrupt_deassert():
 */
void riscv_irq_interrupt_assert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.riscv.irq_asserted = 1;
}
void riscv_irq_interrupt_deassert(struct interrupt *interrupt)
{
	struct cpu *cpu = (struct cpu *) interrupt->extra;
	cpu->cd.riscv.irq_asserted = 0;
}


/*
 *  riscv_exception():
 *
 *  Cause an exception.
 */
void riscv_exception(struct cpu *cpu, int vector, int is_trap)
{
	debugmsg_cpu(cpu, SUBSYS_EXCEPTION, "", VERBOSITY_ERROR,
	    "riscv_exception(): TODO");

	cpu->running = 0;

	riscv_pc_to_pointers(cpu);
}


/*
 *  riscv_cpu_disassemble_instr():
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
int riscv_cpu_disassemble_instr(struct cpu *cpu, unsigned char *ib,
        int running, uint64_t dumpaddr)
{
	if (running)
		dumpaddr = cpu->pc;

	uint64_t offset;
	const char *symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	if (cpu->is_32bit)
		debug("%08" PRIx32": ", (uint32_t) dumpaddr);
	else
		debug("%016" PRIx64": ", (uint64_t) dumpaddr);

	int instr_length_in_bytes = sizeof(uint16_t);
	uint32_t iw;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8);
	else
		iw = ib[1] + (ib[0]<<8);

	if ((iw & 3) == 3) {
		// At least 2 16-bit chunks.
		uint16_t iw2;
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iw2 = ib[2] + (ib[3]<<8);
		else
			iw2 = ib[3] + (ib[2]<<8);

		iw |= (iw2 << 16);

		if (((iw >> 2) & 7) == 7) {
			debug("longer than 32-bit instruction: TODO\n");
			return sizeof(uint32_t);	// actually wrong... :-(
		}

		instr_length_in_bytes = sizeof(uint32_t);
	}

	if (instr_length_in_bytes == sizeof(uint16_t))
		debug("%04x    ", (uint32_t) iw);
	else
		debug("%08" PRIx32, (uint32_t) iw);

	debug(!running && cpu->pc == dumpaddr ? " <- " : "    ");

	if (instr_length_in_bytes == sizeof(uint16_t)) {
		uint w13 = (iw >> 13) & 7;
		uint w0 = (iw >> 0) & 3;
		uint op = (w13 << 2) | w0;

		uint rs1rd = (iw >> 7) & 31;
		uint64_t nzimm5 = ((iw & (1 << 12)) ? -1 : 0) << 5;
		uint64_t nzimm = nzimm5 | ((iw >> 2) & 31);

		switch (op) {
		case 1:
			if (rs1rd == 0 && nzimm == 0)
				debug("nop");
			else if (rs1rd == 0)
				debug("c.addi\tTODO: rs1rd = 0 but nzimm = %lli?", (long long) nzimm);
			else
				debug("c.addi\tTODO reg %i, %lli", rs1rd, (long long) nzimm);
			break;
		default:
			debug("UNIMPLEMENTED compressed op %i", op);
		}

	} else if (instr_length_in_bytes == sizeof(uint32_t)) {
		debug("TODO: 32-bit wide instruction words");
	} else {
		debug("TODO");
	}
	
	debug("\n");

	return instr_length_in_bytes;
}


#include "tmp_riscv_tail.c"


