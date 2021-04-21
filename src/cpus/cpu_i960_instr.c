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
 *  Intel 80960 (i960) instructions.
 *
 *  Individual functions should keep track of cpu->n_translated_instrs.
 *  (If no instruction was executed, then it should be decreased. If, say, 4
 *  instructions were combined into one function and executed, then it should
 *  be increased by 3.)
 */


#define SYNCH_PC                {                                       \
                int low_pc_ = ((size_t)ic - (size_t)cpu->cd.i960.cur_ic_page) \
                    / sizeof(struct i960_instr_call);                   \
                cpu->pc &= ~((I960_IC_ENTRIES_PER_PAGE-1)               \
                    << I960_INSTR_ALIGNMENT_SHIFT);                     \
                cpu->pc += (low_pc_ << I960_INSTR_ALIGNMENT_SHIFT);      \
        }

#define	ABORT_EXECUTION	  {	SYNCH_PC;				\
				cpu->cd.i960.next_ic = &nothing_call;	\
				cpu->running = false; }


/*
 *  nop:  Do nothing.
 */
X(nop)
{
}


/*****************************************************************************/


X(end_of_page)
{
	/*  Update the PC:  (offset 0, but on the next page)  */
	cpu->pc &= ~((I960_IC_ENTRIES_PER_PAGE-1) <<
	    I960_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (I960_IC_ENTRIES_PER_PAGE << I960_INSTR_ALIGNMENT_SHIFT);

	/*  end_of_page doesn't count as an executed instruction:  */
	cpu->n_translated_instrs --;

	/*
	 *  Find the new physpage and update translation pointers.
	 *
	 *  Note: This may cause an exception, if e.g. the new page is
	 *  not accessible.
	 */
	quick_pc_to_pointers(cpu);

	/*  Simple jump to the next page (if we are lucky):  */
	if (cpu->delay_slot == NOT_DELAYED)
		return;

	/*
	 *  If we were in a delay slot, and we got an exception while doing
	 *  quick_pc_to_pointers, then return. The function which called
	 *  end_of_page should handle this case.
	 */
	if (cpu->delay_slot == EXCEPTION_IN_DELAY_SLOT)
		return;

	/*
	 *  Tricky situation; the delay slot is on the next virtual page.
	 *  Calling to_be_translated will translate one instruction manually,
	 *  execute it, and then discard it.
	 */
	/*  fatal("[ end_of_page: delay slot across page boundary! ]\n");  */

	instr(to_be_translated)(cpu, cpu->cd.i960.next_ic);

	/*  The instruction in the delay slot has now executed.  */
	/*  fatal("[ end_of_page: back from executing the delay slot, %i ]\n",
	    cpu->delay_slot);  */

	/*  Find the physpage etc of the instruction in the delay slot
	    (or, if there was an exception, the exception handler):  */
	quick_pc_to_pointers(cpu);
}


X(end_of_page2)
{
	/*  Synchronize PC on the _second_ instruction on the next page:  */
	int low_pc = ((size_t)ic - (size_t)cpu->cd.i960.cur_ic_page)
	    / sizeof(struct i960_instr_call);
	cpu->pc &= ~((I960_IC_ENTRIES_PER_PAGE-1)
	    << I960_INSTR_ALIGNMENT_SHIFT);
	cpu->pc += (low_pc << I960_INSTR_ALIGNMENT_SHIFT);

	if (low_pc < 0 || low_pc > ((I960_IC_ENTRIES_PER_PAGE+1)
	    << I960_INSTR_ALIGNMENT_SHIFT)) {
		printf("[ end_of_page2: HUH? low_pc=%i, cpu->pc = %08"
		    PRIx32" ]\n", low_pc, (uint32_t) cpu->pc);
	}

	/*  This doesn't count as an executed instruction.  */
	cpu->n_translated_instrs --;

	quick_pc_to_pointers(cpu);

	if (cpu->delay_slot == NOT_DELAYED)
		return;

	debugmsg_cpu(cpu, SUBSYS_CPU, "i960", VERBOSITY_ERROR,
	    "end_of_page2: fatal error, we're in a delay slot");
	ABORT_EXECUTION;
}


/*****************************************************************************/


/*
 *  i960_instr_to_be_translated():
 *
 *  Translate an instruction word into a i960_instr_call. ic is filled in with
 *  valid data for the translated instruction, or a "nothing" instruction if
 *  there was a translation failure. The newly translated instruction is then
 *  executed.
 */
X(to_be_translated)
{
	/*  Figure out the (virtual) address of the instruction:  */
	uint32_t low_pc = ((size_t)ic - (size_t)cpu->cd.i960.cur_ic_page)
	    / sizeof(struct i960_instr_call);

	uint32_t addr = cpu->pc & ~((I960_IC_ENTRIES_PER_PAGE-1)
	    << I960_INSTR_ALIGNMENT_SHIFT);
	addr += (low_pc << I960_INSTR_ALIGNMENT_SHIFT);
	cpu->pc = (uint32_t)addr;
	addr &= ~((1 << I960_INSTR_ALIGNMENT_SHIFT) - 1);

	/*  Read the instruction word from memory:  */
	uint8_t* page = cpu->cd.i960.host_load[(uint32_t)addr >> 12];

	unsigned char ib[4];

	if (page != NULL) {
		/*  fatal("TRANSLATION HIT!\n");  */
		memcpy(ib, page + (addr & 0xffc), sizeof(ib));
	} else {
		/*  fatal("TRANSLATION MISS!\n");  */
		if (!cpu->memory_rw(cpu, cpu->mem, addr, ib,
		    sizeof(ib), MEM_READ, CACHE_INSTRUCTION)) {
			fatal("to_be_translated(): read failed: TODO\n");
			goto bad;
		}
	}

	uint32_t iw;

	if (cpu->byte_order == EMUL_LITTLE_ENDIAN)
		iw = ib[0] + (ib[1]<<8) + (ib[2]<<16) + (ib[3]<<24);
	else
		iw = ib[3] + (ib[2]<<8) + (ib[1]<<16) + (ib[0]<<24);

	const int opcode = iw >> 24;


#define DYNTRANS_TO_BE_TRANSLATED_HEAD
#include "cpu_dyntrans.c"
#undef  DYNTRANS_TO_BE_TRANSLATED_HEAD


	/*
	 *  Translate the instruction:
	 */

	switch (opcode) {

	default:goto bad;
	}


#define	DYNTRANS_TO_BE_TRANSLATED_TAIL
#include "cpu_dyntrans.c" 
#undef	DYNTRANS_TO_BE_TRANSLATED_TAIL
}

