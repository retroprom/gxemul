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
 *  RISC-V instructions.
 */


#define SYNCH_PC                {                                       \
                int low_pc = ((size_t)ic - (size_t)cpu->cd.riscv.cur_ic_page) \
                    / sizeof(struct riscv_instr_call);                   \
                cpu->pc &= ~((RISCV_IC_ENTRIES_PER_PAGE-1)               \
                    << RISCV_INSTR_ALIGNMENT_SHIFT);                     \
                cpu->pc += (low_pc << RISCV_INSTR_ALIGNMENT_SHIFT);      \
        }


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*
 *  addi: Add immediate.
 *
 *  arg[0] = pointer to rd
 *  arg[1] = pointer to rs
 *  arg[2] = (int32_t) immediate value
 */
X(addi)
{
	reg(ic->arg[0]) = (MODE_uint_t) (
	    (MODE_uint_t)reg(ic->arg[1]) + (MODE_int_t)(int32_t)ic->arg[2] );
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((RISCV_IC_ENTRIES_PER_PAGE-1) << RISCV_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (RISCV_IC_ENTRIES_PER_PAGE << RISCV_INSTR_ALIGNMENT_SHIFT);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;

	/*
	 *  Find the new physpage and update translation pointers.
	 *
	 *  Note: This may cause an exception, if e.g. the new page is
	 *  not accessible.
	 */
	quick_pc_to_pointers(cpu);
}


/*****************************************************************************/


/*
 *  riscv_instr_to_be_translated():
 *
 *  Translate an instruction word into a riscv_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	uint8_t ib[sizeof(uint32_t)];
	bool cross_page_instruction = false;

	/*  Figure out the (virtual) address of the instruction:  */
	uint64_t low_pc = ((size_t)ic - (size_t)cpu->cd.riscv.cur_ic_page)
	    / sizeof(struct riscv_instr_call);

	uint64_t addr = cpu->pc & ~((RISCV_IC_ENTRIES_PER_PAGE-1)
	    << RISCV_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << RISCV_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = (MODE_int_t)addr;
	addr &= ~((1 << RISCV_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	uint8_t* page;
#ifdef MODE32
	page = cpu->cd.riscv.host_load[(uint32_t)addr >> 12];
#else
	{
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.riscv.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page = l3->host_load[x3];
	}
#endif

	if (page != NULL) {
		// fatal("PAGE TRANSLATION HIT!\n");
		memcpy(ib, page + (addr & 0xffe), sizeof(uint16_t));
	} else {
		// fatal("PAGE TRANSLATION MISS!\n");
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(uint16_t), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	int instr_length_in_bytes = sizeof(uint16_t);
	uint32_t iw;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8);
	else
		iw = ib[1] + (ib[0]<<8);

	if ((iw & 3) == 3) {
		// At least 2 16-bit chunks. Read the second 16-bit chunk:

		uint8_t* page2;
		uint64_t addr2 = addr + sizeof(uint16_t);

		cross_page_instruction = (addr2 & 0xffe) == 0x000;

#ifdef MODE32
		page2 = cpu->cd.riscv.host_load[(uint32_t)addr2 >> 12];
#else
		const uint32_t mask1 = (1 << DYNTRANS_L1N) - 1;
		const uint32_t mask2 = (1 << DYNTRANS_L2N) - 1;
		const uint32_t mask3 = (1 << DYNTRANS_L3N) - 1;
		uint32_t x1 = (addr2 >> (64-DYNTRANS_L1N)) & mask1;
		uint32_t x2 = (addr2 >> (64-DYNTRANS_L1N-DYNTRANS_L2N)) & mask2;
		uint32_t x3 = (addr2 >> (64-DYNTRANS_L1N-DYNTRANS_L2N-
		    DYNTRANS_L3N)) & mask3;
		struct DYNTRANS_L2_64_TABLE *l2 = cpu->cd.riscv.l1_64[x1];
		struct DYNTRANS_L3_64_TABLE *l3 = l2->l3[x2];
		page2 = l3->host_load[x3];
#endif

		if (page2 != NULL) {
			// fatal("PAGE2 TRANSLATION HIT!\n");
			memcpy(&ib[2], page2 + (addr2 & 0xffe), sizeof(uint16_t));
		} else {
			// fatal("PAGE2 TRANSLATION MISS!\n");
			if (!cpu->memory_rw(cpu, cpu->mem, addr2, &ib[2],
			    sizeof(uint16_t), MEM_READ, CACHE_INSTRUCTION)) {
				fatal("to_be_translated(): read failed: TODO\n");
				goto bad;
			}
		}

		uint16_t iw2;
		if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
			iw2 = ib[2] + (ib[3]<<8);
		else
			iw2 = ib[3] + (ib[2]<<8);

		iw |= (iw2 << 16);

		if (((iw >> 2) & 7) == 7) {
			fatal("longer than 32-bit instruction: TODO\n");
			goto bad;
		}

		instr_length_in_bytes = sizeof(uint32_t);
	}


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

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
				ic->f = instr(nop);
			else if (rs1rd == 0)
				goto bad;
			else {
				ic->f = instr(addi);
				ic->arg[0] = (size_t)&cpu->cd.riscv.x[rs1rd];
				ic->arg[1] = (size_t)&cpu->cd.riscv.x[rs1rd];
				ic->arg[2] = nzimm;
			}
			break;
		default:
			goto bad;
		}

	} else if (instr_length_in_bytes == sizeof(uint32_t)) {
		goto bad;
	} else {
		goto bad;
	}


	if (cross_page_instruction) {
		debugmsg_cpu(cpu, SUBSYS_CPU, "", VERBOSITY_ERROR,
		    "TODO: RISC-V cross page instruction");
		goto bad;
	}

#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

