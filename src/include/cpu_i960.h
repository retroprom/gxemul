#ifndef	CPU_I960_H
#define	CPU_I960_H

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
 *  Intel 80960 (i960) CPU definitions.
 */

#include "misc.h"
#include "interrupt.h"


struct cpu_family;


#define	I960_N_IC_ARGS			3
#define	I960_INSTR_ALIGNMENT_SHIFT	2
#define	I960_IC_ENTRIES_SHIFT		10
#define	I960_IC_ENTRIES_PER_PAGE	(1 << I960_IC_ENTRIES_SHIFT)
#define	I960_PC_TO_IC_ENTRY(a)		(((a)>>I960_INSTR_ALIGNMENT_SHIFT) \
					& (I960_IC_ENTRIES_PER_PAGE-1))
#define	I960_ADDR_TO_PAGENR(a)		((a) >> (I960_IC_ENTRIES_SHIFT \
					+ I960_INSTR_ALIGNMENT_SHIFT))

DYNTRANS_MISC_DECLARATIONS(i960,I960,uint32_t)

#define	I960_MAX_VPH_TLB_ENTRIES		128


// TODO: differently named registers? (register windows)
#define	N_I960_REGS		32
#define	N_I960_SFRS		32
#define	I960_G0			16	// offset of register g0 (first parameter register)


struct i960_cpu {
	/*
	 *  Control registers.
	 *
	 *  NOTE: The program counter is called "ip" on the i960. The
	 *  long name for the i960 pc register is to separate it from
	 *  cpu->pc (the program counter).
	 */
	uint32_t		ac;		// Arithmetic control
	uint32_t		i960_pc;	// Process control
	uint32_t		tc;		// Trace control

	/*  General purpose registers: r and g registers  */
	uint32_t		r[N_I960_REGS];

	/*  sfrs:  */
	int			nr_of_valid_sfrs;
	uint32_t		sfr[N_I960_SFRS];

	/*  Current interrupt assertion:  */
	int			irq_asserted;


	/*
	 *  Instruction translation cache, internal TLB structure, and 32-bit
	 *  virtual -> physical -> host address translation arrays.
	 */
	DYNTRANS_ITC(i960)
	VPH_TLBS(i960,I960)
	VPH32(i960,I960)
};


/*  cpu_i960.c:  */
int i960_cpu_instruction_has_delayslot(struct cpu *cpu, unsigned char *ib);
int i960_run_instr(struct cpu *cpu);
void i960_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void i960_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void i960_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int i960_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
void i960_cpu_family_init(struct cpu_family *);
void i960_exception(struct cpu *cpu, int vector, int is_trap);

/*  memory_i960.c:  */
int i960_translate_v2p(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);


#endif	/*  CPU_I960_H  */
