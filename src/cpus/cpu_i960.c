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
 *
 *  Disassembly of i960CA should work.
 *
 *  TODO: Almost everything else.
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


// TODO: #define DYNTRANS_DELAYSLOT ?

#define DYNTRANS_32
#include "tmp_i960_head.c"


// Register conventions according to
// https://people.cs.clemson.edu/~mark/subroutines/i960.html

static const char* i960_regnames[N_I960_REGS] = {
	"pfp",		// r0 = previous frame pointer
	"sp",		// r1 = stack pointer
	"rip",		// r2 = return instruction pointer
	"r3", "r4", "r5", "r6", "r7",
	"r8", "r9", "r10", "r11", "r12",
	"r13", "r14", "r15",

	"g0", "g1", "g2", "g3",	// parameters 0-3; return words 0-3
	"g4", "g5", "g6", "g7", // parameters 4-7; temporaries
	"g8", "g9", "g10", "g11", "g12",	// preserved accross call
	"g13",		// structure return pointer
	"g14",		// argument block pointer; leaf return address (HW)
	"fp" 		// g15 = frame pointer (16-byte aligned HW)
};

struct reg_instruction {
	int opcode;
	const char* mnemonic;
	bool has_src1;
	bool has_src2;
	bool has_dst;
	bool has_src3;	// true if the dst/src field is used as a source
};

struct reg_instruction reg_instructions[] = {
	{ 0x580, "notbit",      true,  true,  true,  false },
	{ 0x581, "and",         true,  true,  true,  false },
	{ 0x582, "andnot",      true,  true,  true,  false },
	{ 0x583, "setbit",      true,  true,  true,  false },
	{ 0x584, "notand",      true,  true,  true,  false },
	{ 0x586, "xor",         true,  true,  true,  false },
	{ 0x587, "or",          true,  true,  true,  false },
	{ 0x588, "nor",         true,  true,  true,  false },
	{ 0x589, "xnor",        true,  true,  true,  false },
	{ 0x58a, "not",         true,  false, true,  false },
	{ 0x58b, "ornot",       true,  true,  true,  false },
	{ 0x58c, "clrbit",      true,  true,  true,  false },
	{ 0x58d, "notor",       true,  true,  true,  false },
	{ 0x58e, "nand",        true,  true,  true,  false },
	{ 0x58f, "alterbit",    true,  true,  true,  false },

	{ 0x590, "addo",        true,  true,  true,  false },
	{ 0x591, "addi",        true,  true,  true,  false },
	{ 0x592, "subo",        true,  true,  true,  false },
	{ 0x593, "subi",        true,  true,  true,  false },
	{ 0x598, "shro",        true,  true,  true,  false },
	{ 0x59a, "shrdi",       true,  true,  true,  false },
	{ 0x59b, "shri" ,       true,  true,  true,  false },
	{ 0x59c, "shlo",        true,  true,  true,  false },
	{ 0x59d, "rotate",      true,  true,  true,  false },
	{ 0x59e, "shli",        true,  true,  true,  false },

	{ 0x5a0, "cmpo",        true,  true,  false, false },
	{ 0x5a1, "cmpi",        true,  true,  false, false },
	{ 0x5a2, "concmpo",     true,  true,  false, false },
	{ 0x5a3, "concmpi",     true,  true,  false, false },
	{ 0x5a4, "cmpinco",     true,  true,  true,  false },
	{ 0x5a5, "cmpinci",     true,  true,  true,  false },
	{ 0x5a6, "cmpdeco",     true,  true,  true,  false },
	{ 0x5a7, "cmpdeci",     true,  true,  true,  false },

	{ 0x5ac, "scanbyte",    true,  true,  false, false },
	{ 0x5ae, "chkbit",      true,  true,  false, false },

	{ 0x5b0, "addc",        true,  true,  true,  false },
	{ 0x5b2, "subc",        true,  true,  true,  false },

	{ 0x5cc, "mov",         true,  false, true,  false },
	{ 0x5d8, "eshro",       true,  true,  true,  false },
	{ 0x5dc, "movl",        true,  false, true,  false },
	{ 0x5ec, "movt",        true,  false, true,  false },
	{ 0x5fc, "movq",        true,  false, true,  false },

	{ 0x630, "sdma",        true,  true,  true,  true  },
	{ 0x631, "udma",        false, false, false, false },

	{ 0x640, "spanbit",     true,  false, true,  false },
	{ 0x641, "scanbit",     true,  false, true,  false },
	{ 0x645, "modac",       true,  true,  true,  true  },

	{ 0x650, "modify",      true,  true,  true,  true  },
	{ 0x651, "extract",     true,  true,  true,  true  },
	{ 0x654, "modtc",       true,  true,  true,  true  },
	{ 0x655, "modpc",       true,  true,  true,  true  },
	{ 0x659, "sysctl",      true,  true,  true,  true  },

	{ 0x660, "calls",       true,  false, false, false },
	{ 0x66b, "mark",        false, false, false, false },
	{ 0x66c, "fmark",       false, false, false, false },
	{ 0x66d, "flushreg",    false, false, false, false },
	{ 0x66f, "syncf",       false, false, false, false },

	{ 0x670, "emul",        true,  true,  true,  false },
	{ 0x671, "ediv",        true,  true,  true,  false },

	{ 0x701, "mulo",        true,  true,  true,  false },
	{ 0x708, "remo",        true,  true,  true,  false },
	{ 0x70b, "divo",        true,  true,  true,  false },

	{ 0x741, "muli",        true,  true,  true,  false },
	{ 0x748, "remi",        true,  true,  true,  false },
	{ 0x749, "modi",        true,  true,  true,  false },
	{ 0x74b, "divi",        true,  true,  true,  false },

	{ 0,     NULL,          false, false, false, false }
};


static const char *regname_or_literal(int reg, int m, int s)
{
	// Regular g or r registers
	if (m == 0 && s == 0)
		return i960_regnames[reg];

	static char buf[32];

	if (m != 0 && s == 0) {
		// Literal
		snprintf(buf, sizeof(buf), "%i", reg);
	} else if (m == 0 && s != 0) {
		// Special Function Register
		snprintf(buf, sizeof(buf), "sfr%i", reg);
	} else {
		snprintf(buf, sizeof(buf), "reserved%i", reg);
	}
	
	return buf;
}


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
	/*  TODO: Check cpu_type_name in a better way.  */
	if (strcmp(cpu_type_name, "i960Jx") != 0 && strcmp(cpu_type_name, "i960CA") != 0)
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

	if (strcmp(cpu_type_name, "i960CA") == 0)
		cpu->cd.i960.nr_of_valid_sfrs = 3;


	/*
	 *  Add register names as settings:
	 */

	settings_add(cpu->settings, "ip", 1, SETTINGS_TYPE_UINT64,
            SETTINGS_FORMAT_HEX32, (void *) &cpu->pc);

	CPU_SETTINGS_ADD_REGISTER32("ac", cpu->cd.i960.ac);
	CPU_SETTINGS_ADD_REGISTER32("pc", cpu->cd.i960.i960_pc);
	CPU_SETTINGS_ADD_REGISTER32("tc", cpu->cd.i960.tc);

	for (int i = 0; i < N_I960_REGS; i++) {
		CPU_SETTINGS_ADD_REGISTER32(i960_regnames[i], cpu->cd.i960.r[i]);
	}

	for (int i = 0; i < cpu->cd.i960.nr_of_valid_sfrs; i++) {
		char name[20];
		snprintf(name, sizeof(name), "sfr%i", i);
		CPU_SETTINGS_ADD_REGISTER32(name, cpu->cd.i960.sfr[i]);
	}


	/*  Register the CPU interrupt pin:  */
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
	debug("i960CA\ti960Jx\n");
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
		debug("cpu%i:  ip  = 0x%08" PRIx32, x, (uint32_t)cpu->pc);
		debug("  <%s>\n", symbol != NULL? symbol : " no symbol ");

		debug("cpu%i:  ac  = 0x%08" PRIx32 "\n", x, cpu->cd.i960.ac);
		debug("cpu%i:  pc  = 0x%08" PRIx32 "\n", x, cpu->cd.i960.i960_pc);
		debug("cpu%i:  tc  = 0x%08" PRIx32 "\n", x, cpu->cd.i960.tc);

		for (i=0; i<N_I960_REGS; i++) {
			if ((i % 4) == 0)
				debug("cpu%i:", x);

			debug("  %-3s = 0x%08" PRIx32,
			    i960_regnames[i], cpu->cd.i960.r[i]);

			if ((i % 4) == 3)
				debug("\n");
		}
	}

	if (coprocs) {
		for (i = 0; i < cpu->cd.i960.nr_of_valid_sfrs; i++) {
			debug("cpu%i:  sfr%i = 0x%08" PRIx32 "\n", x, i, cpu->cd.i960.sfr[i]);
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

	cpu->running = false;

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
	if (running)
		dumpaddr = cpu->pc;

	uint64_t offset;
	const char *symbol = get_symbol_name(&cpu->machine->symbol_context,
	    dumpaddr, &offset);
	if (symbol != NULL && offset == 0)
		debug("<%s>\n", symbol);

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i:\t", cpu->cpu_id);

	debug("%08" PRIx32": ", (uint32_t) dumpaddr);

	uint32_t iw;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	const int opcode = iw >> 24;

	const int REG_src_dst  = (iw >> 19) & 0x1f;
	const int REG_src2     = (iw >> 14) & 0x1f;
	const int REG_m3       = (iw >> 13) & 0x1;
	const int REG_m2       = (iw >> 12) & 0x1;
	const int REG_m1       = (iw >> 11) & 0x1;
	const int REG_opcode2  = (iw >> 7) & 0xf;
	const int REG_sfr2     = (iw >> 6) & 0x1;
	const int REG_sfr1     = (iw >> 5) & 0x1;
	const int REG_src1     = (iw >> 0) & 0x1f;
	
	const int COBR_src_dst = (iw >> 19) & 0x1f;
	const int COBR_src_2   = (iw >> 14) & 0x1f;
	const int COBR_m1      = (iw >> 13) & 0x1;
	const int COBR_disp    = (iw >> 2) & 0x7ff;
	const int COBR_t       = (iw >> 1) & 0x1;
	const int COBR_s2      = (iw >> 0) & 0x1;
	
	const int CTRL_disp    = (iw >> 2) & 0x3fffff;
	const int CTRL_t       = (iw >> 1) & 0x1;

	// const int MEMA_src_dst = (iw >> 19) & 0x1f; Same as MEMB_src_dst
	const int MEMA_abase   = (iw >> 14) & 0x1f;
	const int MEMA_md      = (iw >> 13) & 0x1;
	// const int MEMA_zero    = (iw >> 12) & 0x1;  0 for MEMA, 1 for MEMB
	const int MEMA_offset  = (iw >> 0) & 0xfff;

	const int MEMB_src_dst = (iw >> 19) & 0x1f;
	const int MEMB_abase   = (iw >> 14) & 0x1f;
	const int MEMB_mode    = (iw >> 10) & 0xf;
	const int MEMB_scale   = (iw >> 7) & 0x7;
	// const int MEMB_sfr     = (iw >> 5) & 0x3;  Should be 00?
	const int MEMB_index   = (iw >> 0) & 0x1f;

	bool is_64bit_long_instruction = false;
	uint32_t iw2 = 0;

	if (opcode >= 0x80 && iw & 0x1000) {
		/*  Only some MEMB instructions have displacement words:  */
		int mode = (iw >> 10) & 0xf;
		if (mode == 0x5 || mode >= 0xc)
			is_64bit_long_instruction = true;		
	}

	debug("%08 " PRIx32, (uint32_t) iw);

	if (is_64bit_long_instruction) {
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iw2 = ib[4] + (ib[5]<<8) + (ib[6]<<16) + (ib[7]<<24);
		else
			iw2 = ib[7] + (ib[6]<<8) + (ib[5]<<16) + (ib[4]<<24);

		debug(" %08" PRIx32, (uint32_t) iw2);
	} else {
		debug("         ");
	}

	cpu_print_pc_indicator_in_disassembly(cpu, running, dumpaddr);

	if (opcode >= 0x08 && opcode <= 0x1f) {
		/*  CTRL:  */
		const char* mnemonics[] = {
				"b",			/*  0x08  */
				"call",			/*  0x09  */
				"ret",			/*  0x0a  */
				"bal",			/*  0x0b  */
				"unknown_ctrl_0x0c",	/*  0x0c  */
				"unknown_ctrl_0x0d",	/*  0x0d  */
				"unknown_ctrl_0x0e",	/*  0x0e  */
				"unknown_ctrl_0x0f",	/*  0x0f  */
				"bno",			/*  0x10  */
				"bg",			/*  0x11  */
				"be",			/*  0x12  */
				"bge",			/*  0x13  */
				"bl",			/*  0x14  */
				"bne",			/*  0x15  */
				"ble",			/*  0x16  */
				"bo",			/*  0x17  */
				"faultno",		/*  0x18  */
				"faultg",		/*  0x19  */
				"faulte",		/*  0x1a  */
				"faultge",		/*  0x1b  */
				"faultl",		/*  0x1c  */
				"faultne",		/*  0x1d  */
				"faultle",		/*  0x1e  */
				"faulto"		/*  0x1f  */
			};

		debug("%s", mnemonics[opcode - 0x08]);
		if (CTRL_t)
			debug(".f");

		bool hasDisplacement = opcode < 0x18 && opcode != 0x0a;
		if (hasDisplacement) {
			uint32_t disp = CTRL_disp << 2;
			if (disp & 0x00800000)
				disp |= 0xff000000;

			uint32_t addr = dumpaddr + disp;

			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    addr, &offset);
			if (symbol != NULL)
				debug("\t0x%08x\t; <%s>", addr, symbol);
			else
				debug("\t0x%08x", addr);
		}
	} else if (opcode >= 0x20 && opcode <= 0x3f) {
		/*  COBR:  */
		const char* mnemonics[] = {
				"testno",		/*  0x20  */
				"testg",		/*  0x21  */
				"teste",		/*  0x22  */
				"testge",		/*  0x23  */
				"testl",		/*  0x24  */
				"testne",		/*  0x25  */
				"testle",		/*  0x26  */
				"testo",		/*  0x27  */

				"unknown_cobr_0x28",	/*  0x28  */
				"unknown_cobr_0x29",	/*  0x29  */
				"unknown_cobr_0x2a",	/*  0x2a  */
				"unknown_cobr_0x2b",	/*  0x2b  */
				"unknown_cobr_0x2c",	/*  0x2c  */
				"unknown_cobr_0x2d",	/*  0x2d  */
				"unknown_cobr_0x2e",	/*  0x2e  */
				"unknown_cobr_0x2f",	/*  0x2f  */

				"bbc",			/*  0x30  */
				"cmpobg",		/*  0x31  */
				"cmpobe",		/*  0x32  */
				"cmpobge",		/*  0x33  */
				"cmpobl",		/*  0x34  */
				"cmpobne",		/*  0x35  */
				"cmpobne",		/*  0x36  */
				"bbs",			/*  0x37  */

				"cmpibno",		/*  0x38  */
				"cmpibg",		/*  0x39  */
				"cmpibe",		/*  0x3a  */
				"cmpibge",		/*  0x3b  */
				"cmpibl",		/*  0x3c  */
				"cmpibne",		/*  0x3d  */
				"cmpible",		/*  0x3e  */
				"cmpibo",		/*  0x3f  */
			};

		debug("%s", mnemonics[opcode - 0x20]);
		if (COBR_t)
			debug(".f");

		bool src1isBitpos = opcode == 0x30 || opcode == 0x37;

		if (opcode <= 0x27) {
			debug("\t%s", regname_or_literal(COBR_src_dst, 0, COBR_s2));
		} else {
			uint32_t targ = COBR_disp << 2;
			if (targ & 0x00001000)
				targ |= 0xffffe000;
			targ += dumpaddr;

			debug("\t%s", regname_or_literal(COBR_src_dst, src1isBitpos ? 1 : COBR_m1, 0));
			debug(",%s", regname_or_literal(COBR_src_2, 0, COBR_s2));

			symbol = get_symbol_name(&cpu->machine->symbol_context,
			    targ, &offset);
			if (symbol != NULL)
				debug(",0x%08x\t; <%s>", targ, symbol);
			else
				debug(",0x%08x", targ);
		}
	} else if (opcode >= 0x58 && opcode <= 0x7f) {
		/*  REG:  */
		struct reg_instruction *rinstr = NULL;
		for (int i = 0; ; ++i) {
			if (reg_instructions[i].mnemonic == NULL)
				break;
			if (reg_instructions[i].opcode == (opcode << 4) + REG_opcode2) {
				rinstr = &reg_instructions[i];
				break;
			}
		}

		bool has_src1 = true, has_src2 = true, has_dst = true, has_src3 = false;

		if (rinstr == NULL) {
			debug("unknown_reg_0x%02x:0x%x", opcode, REG_opcode2);
		} else {
			debug("%s", rinstr->mnemonic);
			has_src1 = rinstr->has_src1;
			has_src2 = rinstr->has_src2;
			has_dst = rinstr->has_dst;
			has_src3 = rinstr->has_src3;
		}

		if (has_src1)
			debug("\t%s", regname_or_literal(REG_src1, REG_m1, REG_sfr1));

		if (has_src2) {
			if (has_src1)
				debug(",");

			debug("%s", regname_or_literal(REG_src2, REG_m2, REG_sfr2));
		}
		
		if (has_dst) {
			if (has_src1 || has_src2)
				debug(",");

			if (REG_m3) {
				/*
				 *  The manual for i960CA says (when M3 = 1):
				 *
				 *  "src/dst is a literal when used as a source
				 *   or a special function register when used
				 *   as a destination. M3 may not be 1 when
				 *   src/dst is used both as a source and 
				 *   destination in an instruction (atmod,
				 *   modify, extract, modpc)."
				 */
				if (has_src3)
					debug("%s", regname_or_literal(REG_src_dst, 1, 0));
				else
					debug("%s", regname_or_literal(REG_src_dst, 0, 1));
			} else
				debug("%s", regname_or_literal(REG_src_dst, 0, 0));
		}
	} else if (opcode >= 0x80 && opcode <= 0xcf) {
		/*  MEM:  */
		
		/*  NOTE: These are for i960CA. When implementing support for
		    other CPU variants, include an enum indicating which CPU
		    it is for so that a warning can be printed for instructions
		    that will cause faults on another CPU.  */
		const char* mnemonics[] = {
			"ldob",			/*  0x80  */
			"unknown_mem_0x81",	/*  0x81 BiiN ldvob  */
			"stob",			/*  0x82  */
			"unknown_mem_0x83",	/*  0x83 BiiN stvob  */
			"bx",			/*  0x84  */
			"balx",			/*  0x85  */
			"callx",		/*  0x86  */
			"unknown_mem_0x87",	/*  0x87  */

			"ldos",			/*  0x88  */
			"unknown_mem_0x89",	/*  0x89 BiiN ldvos  */
			"stos",			/*  0x8a  */
			"unknown_mem_0x8b",	/*  0x8b BiiN stvos  */
			"lda",			/*  0x8c  */
			"unknown_mem_0x8d",	/*  0x8d  */
			"unknown_mem_0x8e",	/*  0x8e  */
			"unknown_mem_0x8f",	/*  0x8f  */

			"ld",			/*  0x90  */
			"unknown_mem_0x91",	/*  0x91 BiiN ldv  */
			"st",			/*  0x92  */
			"unknown_mem_0x93",	/*  0x93 Biin stv  */
			"unknown_mem_0x94",	/*  0x94  */
			"unknown_mem_0x95",	/*  0x95  */
			"unknown_mem_0x96",	/*  0x96  */
			"unknown_mem_0x97",	/*  0x97  */

			"ldl",			/*  0x98  */
			"unknown_mem_0x99",	/*  0x99 BiiN ldvl  */
			"stl",			/*  0x9a  */
			"unknown_mem_0x9b",	/*  0x9b BiiN stvl  */
			"unknown_mem_0x9c",	/*  0x9c  */
			"unknown_mem_0x9d",	/*  0x9d  */
			"unknown_mem_0x9e",	/*  0x9e  */
			"unknown_mem_0x9f",	/*  0x9f  */

			"ldt",			/*  0xa0  */
			"unknown_mem_0xa1",	/*  0xa1 BiiN ldvt  */
			"stt",			/*  0xa2  */
			"unknown_mem_0xa3",	/*  0xa3 Biin stvt  */
			"unknown_mem_0xa4",	/*  0xa4  */
			"unknown_mem_0xa5",	/*  0xa5  */
			"unknown_mem_0xa6",	/*  0xa6  */
			"unknown_mem_0xa7",	/*  0xa7  */

			"unknown_mem_0xa8",	/*  0xa8  */
			"unknown_mem_0xa9",	/*  0xa9  */
			"unknown_mem_0xaa",	/*  0xaa  */
			"unknown_mem_0xab",	/*  0xab  */
			"unknown_mem_0xac",	/*  0xac  */
			"unknown_mem_0xad",	/*  0xad  */
			"unknown_mem_0xae",	/*  0xae  */
			"unknown_mem_0xaf",	/*  0xaf  */

			"ldq",			/*  0xb0  */
			"unknown_mem_0xb1",	/*  0xb1 BiiN ldvq  */
			"stq",			/*  0xb2  */
			"unknown_mem_0xb3",	/*  0xb3 BiiN stvq  */
			"unknown_mem_0xb4",	/*  0xb4  */
			"unknown_mem_0xb5",	/*  0xb5  */
			"unknown_mem_0xb6",	/*  0xb6  */
			"unknown_mem_0xb7",	/*  0xb7  */

			"unknown_mem_0xb8",	/*  0xb8  */
			"unknown_mem_0xb9",	/*  0xb9  */
			"unknown_mem_0xba",	/*  0xba  */
			"unknown_mem_0xbb",	/*  0xbb  */
			"unknown_mem_0xbc",	/*  0xbc  */
			"unknown_mem_0xbd",	/*  0xbd  */
			"unknown_mem_0xbe",	/*  0xbe  */
			"unknown_mem_0xbf",	/*  0xbf  */

			"ldib",			/*  0xc0  */
			"unknown_mem_0xc1",	/*  0xc1 BiiN ldvib  */
			"stib",			/*  0xc2  */
			"unknown_mem_0xc3",	/*  0xc3 Biin stvib  */
			"unknown_mem_0xc4",	/*  0xc4  */
			"unknown_mem_0xc5",	/*  0xc5  */
			"unknown_mem_0xc6",	/*  0xc6  */
			"unknown_mem_0xc7",	/*  0xc7  */

			"ldis",			/*  0xc8  */
			"unknown_mem_0xc9",	/*  0xc9 BiiN ldvis  */
			"stis",			/*  0xca  */
			"unknown_mem_0xcb",	/*  0xcb BiiN stvis  */
			"unknown_mem_0xcc",	/*  0xcc  */
			"unknown_mem_0xcd",	/*  0xcd  */
			"unknown_mem_0xce",	/*  0xce  */
			"unknown_mem_0xcf",	/*  0xcf  */
			
			/*  BiiN:
				d0 = ldm
				d1 = ldvm
				d2 = stm
				d3 = stvm
				d8 = ldml
				d9 = ldvml
				da = stml
				db = stvml */
			};

		debug("%s\t", mnemonics[opcode - 0x80]);

		bool usesDst = opcode != 0x84 && opcode != 0x86;
		bool isStore = !!(opcode & 2);

		if (usesDst && isStore)
			debug("%s,", regname_or_literal(MEMB_src_dst, 0, 0));

		if (iw & 0x1000) {
			/*  MEMB:  */
			int scale = 1 << MEMB_scale;
			switch (MEMB_mode) {
			case 0x4:
				debug("(%s)", regname_or_literal(MEMB_abase, 0, 0));
				break;
			case 0x5:
				{
					uint32_t offset32 = iw2 + 8;
					debug("0x%x(ip)", offset32);
				}
				break;
			case 0x7:
				// (reg1)[reg2 * scale]
				debug("(%s)", regname_or_literal(MEMB_abase, 0, 0));
				debug("[%s*%i]", regname_or_literal(MEMB_index, 0, 0), scale);
				break;
			case 0xc:
			case 0xd:
				{
					uint32_t offset32 = iw2;
					debug("0x%x", offset32);
					if (MEMB_mode == 0xd)
						debug("(%s)", regname_or_literal(MEMB_abase, 0, 0));
				}
				break;
			case 0xe:
			case 0xf:
				{
					uint32_t offset32 = iw2;
					debug("0x%x", offset32);
					if (MEMB_mode == 0xf)
						debug("(%s)", regname_or_literal(MEMB_abase, 0, 0));

					debug("[%s*%i]", regname_or_literal(MEMB_index, 0, 0), scale);
				}
				break;
			default:
				debug("unimplemented MEMB mode!");
			}
		} else {
			/*  MEMA:  */
			debug("0x%x", MEMA_offset);

			if (MEMA_md)
				debug("(%s)", regname_or_literal(MEMA_abase, 0, 0));
		}

		if (usesDst && !isStore)
			debug(",%s", regname_or_literal(MEMB_src_dst, 0, 0));
	} else if (iw == 0) {
		debug("--");
	} else {
		debug("unimplemented opcode %i", opcode);
	}

	debug("\n");

	return is_64bit_long_instruction ? sizeof(uint64_t) : sizeof(uint32_t);
}


#include "tmp_i960_tail.c"


