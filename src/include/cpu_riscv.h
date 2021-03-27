#ifndef	CPU_RISCV_H
#define	CPU_RISCV_H

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
 *  RISC-V CPU definitions.
 */

#include "misc.h"
#include "interrupt.h"


struct cpu_family;

// The RISC-V instruction stream is treated as having instructions of variable
// length, in multiples of 16 bits (RISCV_INSTR_ALIGNMENT_SHIFT = 1).
#define	RISCV_N_IC_ARGS			3
#define	RISCV_INSTR_ALIGNMENT_SHIFT	1
#define	RISCV_IC_ENTRIES_SHIFT		11
#define	RISCV_IC_ENTRIES_PER_PAGE	(1 << RISCV_IC_ENTRIES_SHIFT)
#define	RISCV_PC_TO_IC_ENTRY(a)		(((a)>>RISCV_INSTR_ALIGNMENT_SHIFT) \
					& (RISCV_IC_ENTRIES_PER_PAGE-1))
#define	RISCV_ADDR_TO_PAGENR(a)		((a) >> (RISCV_IC_ENTRIES_SHIFT \
					+ RISCV_INSTR_ALIGNMENT_SHIFT))

#define	RISCV_L2N		17
#define	RISCV_L3N		18

#define	RISCV_MAX_VPH_TLB_ENTRIES	192

DYNTRANS_MISC_DECLARATIONS(riscv,RISCV,uint64_t)
DYNTRANS_MISC64_DECLARATIONS(riscv,RISCV,uint8_t)

#define	N_RISCV_REGS		32


struct riscv_cpu {
	/*  General purpose registers:  */
	uint32_t		x[N_RISCV_REGS];

	/*  Destination scratch register for non-nop instructions with destination x0:  */
	uint32_t		zero_scratch;

	/*  Current interrupt assertion:  */
	int			irq_asserted;


	/*
	 *  Instruction translation cache, internal TLB structure, and 32-bit
	 *  virtual -> physical -> host address translation arrays.
	 */
	DYNTRANS_ITC(riscv)
	VPH_TLBS(riscv,RISCV)
	VPH32(riscv,RISCV)
	VPH64(riscv,RISCV)
};


/*  cpu_riscv.c:  */
int riscv_cpu_instruction_has_delayslot(struct cpu *cpu, unsigned char *ib);
int riscv_run_instr(struct cpu *cpu);
void riscv_update_translation_table(struct cpu *cpu, uint64_t vaddr_page,
	unsigned char *host_page, int writeflag, uint64_t paddr_page);
void riscv_invalidate_translation_caches(struct cpu *cpu, uint64_t, int);
void riscv_invalidate_code_translation(struct cpu *cpu, uint64_t, int);
int riscv_memory_rw(struct cpu *cpu, struct memory *mem, uint64_t vaddr,
	unsigned char *data, size_t len, int writeflag, int cache_flags);
void riscv_cpu_family_init(struct cpu_family *);
void riscv_exception(struct cpu *cpu, int vector, int is_trap);

/*  memory_riscv.c:  */
int riscv_translate_v2p(struct cpu *cpu, uint64_t vaddr,
	uint64_t *return_addr, int flags);


#endif	/*  CPU_RISCV_H  */
