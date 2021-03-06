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
 *
 *
 *  Common routines for CPU emulation. (Not specific to any CPU type.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <string.h>

#include "cpu.h"
#include "emul.h"
#include "machine.h"
#include "memory.h"
#include "settings.h"
#include "timer.h"


extern size_t dyntrans_cache_size;

static struct cpu_family *first_cpu_family = NULL;


/*
 *  cpu_new():
 *
 *  Create a new cpu object.  Each family is tried in sequence until a
 *  CPU family recognizes the cpu_type_name.
 *
 *  If there was no match, NULL is returned. Otherwise, a pointer to an
 *  initialized cpu struct is returned.
 */
struct cpu *cpu_new(struct memory *mem, struct machine *machine,
        int cpu_id, char *name)
{
	char *cpu_type_name;
	char tmpstr[30];

	if (name == NULL) {
		debugmsg(SUBSYS_CPU, "", VERBOSITY_ERROR, "cpu name = NULL?");
		return NULL;
	}

	CHECK_ALLOCATION(cpu_type_name = strdup(name));

	struct cpu *cpu = (struct cpu *) zeroed_alloc(sizeof(struct cpu));

	CHECK_ALLOCATION(cpu->path = (char *) malloc(strlen(machine->path) + 15));
	snprintf(cpu->path, strlen(machine->path) + 15,
	    "%s.cpu[%i]", machine->path, cpu_id);

	cpu->memory_rw  = NULL;
	cpu->name       = cpu_type_name;
	cpu->mem        = mem;
	cpu->machine    = machine;
	cpu->cpu_id     = cpu_id;
	cpu->byte_order = EMUL_UNDEFINED_ENDIAN;
	cpu->running    = false;

	/*  Create settings, and attach to the machine:  */
	cpu->settings = settings_new();
	snprintf(tmpstr, sizeof(tmpstr), "cpu[%i]", cpu_id);
	settings_add(machine->settings, tmpstr, 1,
	    SETTINGS_TYPE_SUBSETTINGS, 0, cpu->settings);

	settings_add(cpu->settings, "name", 0, SETTINGS_TYPE_STRING,
	    SETTINGS_FORMAT_STRING, (void *) &cpu->name);
	settings_add(cpu->settings, "running", 0, SETTINGS_TYPE_BOOL,
	    SETTINGS_FORMAT_YESNO, (void *) &cpu->running);

	cpu_create_or_reset_tc(cpu);

	struct cpu_family *fp = first_cpu_family;

	while (fp != NULL) {
		if (fp->cpu_new != NULL) {
			if (fp->cpu_new(cpu, mem, machine, cpu_id, cpu_type_name))
				break;
		}

		fp = fp->next;
	}

	if (fp == NULL) {
		debugmsg(SUBSYS_CPU, "", VERBOSITY_ERROR,
		    "unknown cpu type '%s'", cpu_type_name);
		free(cpu);
		return NULL;
	}

	cpu->cpu_family = fp;

	if (cpu->memory_rw == NULL) {
		debugmsg_cpu(cpu, SUBSYS_CPU, "", VERBOSITY_ERROR,
		    "memory_rw == NULL");
		free(cpu);
		return NULL;
	}

	fp->init_tables(cpu);

	if (cpu->byte_order == EMUL_UNDEFINED_ENDIAN) {
		debugmsg_cpu(cpu, SUBSYS_CPU, "endianness", VERBOSITY_ERROR,
		    "Internal bug: Endianness not set!");
		free(cpu);
		return NULL;
	}
	
	if (cpu->vaddr_mask == 0) {
		if (cpu->is_32bit)
			cpu->vaddr_mask = 0x00000000ffffffffULL;
		else
			cpu->vaddr_mask = (int64_t)-1;

		debugmsg_cpu(cpu, SUBSYS_CPU, "vaddr_mask", VERBOSITY_DEBUG,
		    "Warning: vaddr_mask should be set in the CPU family's "
		    "cpu_new()! Assuming 0x%16llx", (long long)cpu->vaddr_mask);
	}

	return cpu;
}


/*
 *  cpu_destroy():
 *
 *  Destroy a cpu object.
 */
void cpu_destroy(struct cpu *cpu)
{
	settings_remove(cpu->settings, "name");
	settings_remove(cpu->settings, "running");

	/*  Remove any remaining level-1 settings:  */
	settings_remove_all(cpu->settings);

	settings_destroy(cpu->settings);

	if (cpu->path != NULL)
		free(cpu->path);

	/*  TODO: This assumes that zeroed_alloc() actually succeeded
	    with using mmap(), and not malloc()!  */
	munmap((void *)cpu, sizeof(struct cpu));
}


/*
 *  cpu_tlbdump():
 *
 *  Called from the debugger to dump the TLB in a readable format.
 *  x is the cpu number to dump, or -1 to dump all CPUs.
 *                                              
 *  If rawflag is nonzero, then the TLB contents isn't formated nicely,
 *  just dumped.
 */
void cpu_tlbdump(struct cpu *cpu, int rawflag)
{
	if (cpu->cpu_family == NULL || cpu->cpu_family->tlbdump == NULL)
		fatal("cpu_tlbdump(): NULL\n");
	else
		cpu->cpu_family->tlbdump(cpu, rawflag);
}


/*
 *  cpu_disassemble_instr():
 *
 *  Convert an instruction word into human readable format, for instruction
 *  tracing.
 */
int cpu_disassemble_instr(struct machine *m, struct cpu *cpu,
	unsigned char *instr, bool running, uint64_t addr)
{
	if (cpu->cpu_family == NULL || cpu->cpu_family->disassemble_instr == NULL) {
		fatal("cpu_disassemble_instr(): NULL\n");
		return 0;
	}

	if (running)
		addr = cpu->pc;

	bool show_symbolic_function_name = true;

	/*  Special hack for M88K userspace:  */
	if (cpu->cpu_family->arch == ARCH_M88K &&
	    !(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE))
		show_symbolic_function_name = false;

	uint64_t offset;
	const char *symbol = get_symbol_name(&cpu->machine->symbol_context, addr, &offset);
	if (symbol != NULL && offset == 0 && show_symbolic_function_name) {
		if (running && !cpu->machine->show_trace_tree)
			cpu_functioncall_print(cpu);
		else
			debug("<%s>\n", symbol);
	}

	if (cpu->machine->ncpus > 1 && running)
		debug("cpu%i: ", cpu->cpu_id);

	return cpu->cpu_family->disassemble_instr(cpu, instr, running, addr);
}


/*                       
 *  cpu_register_dump():
 *
 *  Dump cpu registers in a relatively readable format.
 *
 *  gprs: set to non-zero to dump GPRs. (CPU dependent.)
 *  coprocs: set bit 0..x to dump registers in coproc 0..x. (CPU dependent.)
 */
void cpu_register_dump(struct machine *m, struct cpu *cpu,
	int gprs, int coprocs)
{
	if (cpu->cpu_family == NULL || cpu->cpu_family->register_dump == NULL)
		fatal("cpu_register_dump(): NULL\n");
	else
		cpu->cpu_family->register_dump(cpu, gprs, coprocs);
}


/*
 *  cpu_functioncall_print():
 *
 *  Like trace, but used to print function name and argument during
 *  disassembl rather than showing a trace tree.
 */
void cpu_functioncall_print(struct cpu *cpu)
{
	int old = cpu->trace_tree_depth;
	cpu->trace_tree_depth = 0;
	cpu_functioncall_trace(cpu, cpu->pc);
	cpu->trace_tree_depth = old;
}


/*
 *  cpu_functioncall_trace():
 *
 *  This function should be called if machine->show_trace_tree is enabled, and
 *  a function call is being made. f contains the address of the function.
 */
void cpu_functioncall_trace(struct cpu *cpu, uint64_t f)
{
	bool show_symbolic_function_name = true;
	int i, n_args = -1;
	char *symbol;
	uint64_t offset;

	/*  Special hack for M88K userspace:  */
	if (cpu->cpu_family->arch == ARCH_M88K &&
	    !(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE))
		show_symbolic_function_name = false;

	if (cpu->machine->ncpus > 1)
		fatal("cpu%i:\t", cpu->cpu_id);

	if (cpu->trace_tree_depth > 100)
		cpu->trace_tree_depth = 100;
	for (i=0; i<cpu->trace_tree_depth; i++)
		fatal("  ");

	cpu->trace_tree_depth ++;

	fatal("<");
	symbol = get_symbol_name_and_n_args(&cpu->machine->symbol_context,
	    f, &offset, &n_args);
	if (symbol != NULL && show_symbolic_function_name && offset == 0)
		fatal("%s", symbol);
	else {
		if (cpu->is_32bit)
			fatal("0x%" PRIx32, (uint32_t) f);
		else
			fatal("0x%" PRIx64, (uint64_t) f);
	}
	fatal("(");

	if (cpu->cpu_family->functioncall_trace != NULL)
		cpu->cpu_family->functioncall_trace(cpu, n_args);

	fatal(")>\n");
}


/*
 *  cpu_functioncall_trace_return():
 *
 *  This function should be called if machine->show_trace_tree is enabled, and
 *  a function is being returned from.
 *
 *  TODO: Print return value? This could be implemented similar to the
 *  cpu->functioncall_trace function call above.
 */
void cpu_functioncall_trace_return(struct cpu *cpu)
{
	cpu->trace_tree_depth --;
	if (cpu->trace_tree_depth < 0)
		cpu->trace_tree_depth = 0;
}


/*
 *  cpu_create_or_reset_tc():
 *
 *  Create the translation cache in memory (ie allocate memory for it), if
 *  necessary, and then reset it to an initial state.
 */
void cpu_create_or_reset_tc(struct cpu *cpu)
{
	size_t s = dyntrans_cache_size + DYNTRANS_CACHE_MARGIN;

	if (cpu->translation_cache == NULL)
		cpu->translation_cache = (unsigned char *) zeroed_alloc(s);

	/*  Create an empty table at the beginning of the translation cache:  */
	memset(cpu->translation_cache, 0, sizeof(uint32_t)
	    * N_BASE_TABLE_ENTRIES);

	cpu->translation_cache_cur_ofs =
	    N_BASE_TABLE_ENTRIES * sizeof(uint32_t);

	/*
	 *  There might be other translation pointers that still point to
	 *  within the translation_cache region. Let's invalidate those too:
	 */
	if (cpu->invalidate_code_translation != NULL)
		cpu->invalidate_code_translation(cpu, 0, INVALIDATE_ALL);
}


/*
 *  cpu_break_out_of_dyntrans_loop():
 */
void cpu_break_out_of_dyntrans_loop(struct cpu *cpu)
{
	cpu->n_translated_instrs |= N_BREAK_OUT_OF_DYNTRANS_LOOP;
}


/*
 *  cpu_dumpinfo():
 *
 *  Dumps info about a CPU using debug(). "cpu0: CPUNAME, running" (or similar)
 *  is outputed, and it is up to CPU dependent code to complete the line.
 */
void cpu_dumpinfo(struct machine *m, struct cpu *cpu, bool verbose)
{
	char cpuname[100];
	snprintf(cpuname, sizeof(cpuname), "cpu%i", cpu->cpu_id);

	if (verbose)
		debugmsg(SUBSYS_MACHINE, cpuname, VERBOSITY_INFO,
		    "%s", cpu->running? "running" : "stopped");

	if (cpu->cpu_family == NULL || cpu->cpu_family->dumpinfo == NULL) {
		debugmsg(SUBSYS_MACHINE, cpuname, VERBOSITY_ERROR,
		    "cpu_dumpinfo(): NULL");
	} else {
		if (verbose)
			debug_indentation(1);

		cpu->cpu_family->dumpinfo(cpu, verbose);

		if (verbose)
			debug_indentation(-1);
	}
}


/*
 *  cpu_list_available_types():
 *
 *  Print a list of available CPU types for each cpu family.
 */
void cpu_list_available_types(void)
{
	struct cpu_family *fp = first_cpu_family;

	if (fp == NULL) {
		debug("No CPUs defined!\n");
		return;
	}

	while (fp != NULL) {
		debug("%s:\n", fp->name);
		debug_indentation(1);
		if (fp->list_available_types != NULL)
			fp->list_available_types();
		else
			debug("(internal error: list_available_types"
			    " = NULL)\n");
		debug_indentation(-1);

		fp = fp->next;
	}
}


/*
 *  cpu_print_pc_indicator_in_disassembly():
 *
 *  Helper which shows an arrow indicating the current instruction, during
 *  disassembly.
 */
void cpu_print_pc_indicator_in_disassembly(struct cpu *cpu, bool running, uint64_t dumpaddr)
{
	if (!running && cpu->pc == dumpaddr) {
		color_pc_indicator();
		debug(" <- ");
		color_normal();
	} else {
		debug("    ");
	}
}


/*
 *  cpu_show_cycles():
 *
 *  If show_nr_of_instructions is on, then print a line to stdout about how
 *  many instructions/cycles have been executed so far.
 */
void cpu_show_cycles(struct machine *machine, uint64_t total_elapsed_ms)
{
	struct cpu *cpu = machine->cpus[machine->bootstrap_cpu];

	char buf[2048];
	buf[0] = 0;

	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
	    "%" PRIi64" instrs", (int64_t) cpu->ninstrs);

	if (total_elapsed_ms != 0) {
		uint64_t avg = cpu->ninstrs * 1000 / total_elapsed_ms;

		if (cpu->has_been_idling) {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "; idling");
			cpu->has_been_idling = false;
		} else {
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			    "; instrs/sec=%" PRIi64, avg);
		}
	}

	uint64_t offset;
	const char* symbol = get_symbol_name(&machine->symbol_context, cpu->pc, &offset);

	if (cpu->is_32bit)
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		    "; pc=0x%08" PRIx32, (uint32_t) cpu->pc);
	else
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		    "; pc=0x%016" PRIx64, (uint64_t) cpu->pc);

	/*  Special hack for M88K userland:  (Don't show symbols.)  */
	if (cpu->cpu_family->arch == ARCH_M88K &&
	    !(cpu->cd.m88k.cr[M88K_CR_PSR] & M88K_PSR_MODE))
		symbol = NULL;

	if (symbol != NULL)
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		    " %s<%s>", color_normal_ptr(), symbol);

	if (!cpu->running)
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
		    ", stopped");

	debugmsg_cpu(cpu, SUBSYS_STARTUP, "", VERBOSITY_WARNING, "%s", buf);
}


/*
 *  cpu_run_init():
 *
 *  Prepare to run instructions on all CPUs in this machine. (This function
 *  should only need to be called once for each machine.)
 */
void cpu_run_init(struct machine *machine)
{
	int i;

	if (machine->ncpus == 0) {
		printf("Machine with no CPUs? TODO.\n");
		exit(1);
	}

	for (i=0; i<machine->ncpus; i++)
		machine->cpus[i]->ninstrs = 0;
}


/*
 *  add_cpu_family():
 *
 *  Allocates a cpu_family struct and calls an init function for the
 *  family to fill in reasonable data and pointers.
 */
static void add_cpu_family(void (*family_init)(struct cpu_family *), int arch)
{
	struct cpu_family *fp;

	CHECK_ALLOCATION(fp = (struct cpu_family *) malloc(sizeof(struct cpu_family)));
	memset(fp, 0, sizeof(struct cpu_family));

	family_init(fp);

	fp->arch = arch;
	fp->next = NULL;

	/*  Add last in family chain:  */
	struct cpu_family* tmp = first_cpu_family;
	if (tmp == NULL) {
		first_cpu_family = fp;
	} else {
		while (tmp->next != NULL)
			tmp = tmp->next;
		tmp->next = fp;
	}
}


/*
 *  cpu_init():
 *
 *  Should be called before any other cpu_*() function.
 *
 *  This function calls add_cpu_family() for each processor architecture.
 *  ADD_ALL_CPU_FAMILIES is defined in the config.h file generated by the
 *  configure script.
 */
void cpu_init(void)
{
	ADD_ALL_CPU_FAMILIES;
}

