#ifndef RISCV_CPUCOMPONENT_H
#define	RISCV_CPUCOMPONENT_H

/*
 *  Copyright (C) 2019-2020  Anders Gavare.  All rights reserved.
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
 */

// COMPONENT(riscv_cpu)

#include <iomanip>
#include "CPUDyntransComponent.h"


#define	N_RISCV_XREGS		32

// Each 16-bit part of an instruction is called a "parcel".
#define	RISCV_MAX_PARCELS	12


#define	RISCV_EXTENSION_C	(1 << 1)
#define	RISCV_EXTENSION_I	(1 << 2)


/***********************************************************************/


/**
 * \brief A Component representing a RISC-V processor.
 */
class RISCV_CPUComponent
	: public CPUDyntransComponent
{
public:
	/**
	 * \brief Constructs a RISCV_CPUComponent.
	 */
	RISCV_CPUComponent();

	/**
	 * \brief Creates a RISCV_CPUComponent.
	 */
	static refcount_ptr<Component> Create(const ComponentCreateArgs& args);

	static string GetAttribute(const string& attributeName);

	virtual void ResetState();

	virtual bool PreRunCheckForComponent(GXemul* gxemul);

	virtual size_t DisassembleInstruction(uint64_t vaddr, vector<string>& result);


	/********************************************************************/

	static void RunUnitTests(int& nSucceeded, int& nFailures);

protected:
	virtual bool CheckVariableWrite(StateVariable& var, const string& oldValue);

	virtual bool VirtualToPhysical(uint64_t vaddr, uint64_t& paddr, bool& writable);

	virtual string VirtualAddressAsString(uint64_t vaddr)
	{
		stringstream ss;
		ss.flags(std::ios::hex | std::ios::right);
		ss << std::setfill('0') << std::setw(16) << (uint64_t)vaddr;
		return ss.str();
	}

	virtual uint64_t PCtoInstructionAddress(uint64_t pc);

	virtual int FunctionTraceArgumentCount();
	virtual int64_t FunctionTraceArgument(int n);
	virtual bool FunctionTraceReturnImpl(int64_t& retval);

	virtual int GetDyntransICshift() const;
	virtual void (*GetDyntransToBeTranslated())(CPUDyntransComponent*, DyntransIC*);

	virtual void ShowRegisters(GXemul* gxemul, const vector<string>& arguments) const;

private:
	// TODO: Instructions.
	// DECLARE_DYNTRANS_INSTR(b);
	// DECLARE_DYNTRANS_INSTR(mov_lit_reg);

	void Translate(uint16_t iwords[], int nparcels, struct DyntransIC* ic);
	DECLARE_DYNTRANS_INSTR(ToBeTranslated);

private:
	/*
	 * State:
	 */
	string		m_model;

	uint64_t	m_extensions;

	uint64_t	m_x[N_RISCV_XREGS];
};


#endif	// RISCV_CPUCOMPONENT_H
