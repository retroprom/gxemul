/*
 *  Copyright (C) 2018  Anders Gavare.  All rights reserved.
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <iomanip>

#include "ComponentFactory.h"
#include "GXemul.h"
#include "components/I960_CPUComponent.h"


I960_CPUComponent::I960_CPUComponent()
	: CPUDyntransComponent("i960_cpu", "i960")
{
	m_frequency = 25e6;
	m_isBigEndian = false;

	ResetState();

//	AddVariable("hi", &m_hi);
//	AddVariable("lo", &m_lo);

	// TODO: This only registers using the new ABI names. How should
	// this be handled? Custom "aliasing" variables?
//	for (size_t i=0; i<N_I960_REGS; i++)
//		AddVariable(regname(i, m_abi), &m_gpr[i]);
}


refcount_ptr<Component> I960_CPUComponent::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["model"] = "i960CA";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;

	refcount_ptr<Component> cpu = new I960_CPUComponent();
//	if (!cpu->SetVariableValue("model", "\"" + settings["model"] + "\""))
//		return NULL;

	return cpu;
}


void I960_CPUComponent::ResetState()
{
	m_pageSize = 4096;

	for (size_t i=0; i<N_I960_REGS; i++)
		m_r[i] = 0;

	m_pc = 0;

	CPUDyntransComponent::ResetState();
}


bool I960_CPUComponent::PreRunCheckForComponent(GXemul* gxemul)
{
	if (m_pc & 0x3) {
		gxemul->GetUI()->ShowDebugMessage(this, "the pc register"
		    " can not have bit 0 or 1 set!\n");
		return false;
	}

	return CPUDyntransComponent::PreRunCheckForComponent(gxemul);
}


bool I960_CPUComponent::CheckVariableWrite(StateVariable& var, const string& oldValue)
{
	// UI* ui = GetUI();

	return CPUDyntransComponent::CheckVariableWrite(var, oldValue);
}


void I960_CPUComponent::ShowRegisters(GXemul* gxemul, const vector<string>& arguments) const
{
	stringstream ss;

	ss.flags(std::ios::hex);
	ss << std::setfill('0');

	// Yuck, this is horrible. Is there some portable way to put e.g.
	// std::setw(16) into an object, and just pass that same object several
	// times?

	ss << "pc=";
	ss << std::setw(8);
	ss << (uint32_t)m_pc;

	string symbol = GetSymbolRegistry().LookupAddress(m_pc, true);
	if (symbol != "")
		ss << " <" << symbol << ">";
	ss << "\n";
/*
	ss << "hi=";
	if (is32bit)
		ss << std::setw(8);
	else
		ss << std::setw(16);
	ss << Trunc3264(m_hi, is32bit) << " lo=";
	if (is32bit)
		ss << std::setw(8);
	else
		ss << std::setw(16);
	ss << Trunc3264(m_lo, is32bit) << "\n";

	for (size_t i=0; i<N_I960_REGS; i++) {
		ss << regname(i, m_abi) << "=";
		if (is32bit)
			ss << std::setw(8);
		else
			ss << std::setw(16);
		ss << Trunc3264(m_gpr[i], is32bit);
		if ((i&3) == 3)
			ss << "\n";
		else
			ss << " ";
	}
*/
	gxemul->GetUI()->ShowDebugMessage(ss.str());
}


int I960_CPUComponent::FunctionTraceArgumentCount()
{
	// TODO
	return 8;
}


int64_t I960_CPUComponent::FunctionTraceArgument(int n)
{
	// TODO
	return m_r[n];
}


bool I960_CPUComponent::FunctionTraceReturnImpl(int64_t& retval)
{
	// TODO
	retval = m_r[0];
	return true;
}


int I960_CPUComponent::GetDyntransICshift() const
{
	// 4 bytes per instruction means 2 bits shift.
	return 2;
}


void (*I960_CPUComponent::GetDyntransToBeTranslated())(CPUDyntransComponent*, DyntransIC*)
{
	return instr_ToBeTranslated;
}


bool I960_CPUComponent::VirtualToPhysical(uint64_t vaddr, uint64_t& paddr,
	bool& writable)
{
	paddr = vaddr;
	writable = true;
	return true;
}


uint64_t I960_CPUComponent::PCtoInstructionAddress(uint64_t pc)
{
	return pc;
}


size_t I960_CPUComponent::DisassembleInstruction(uint64_t vaddr, size_t maxLen,
	unsigned char *instruction, vector<string>& result)
{
	const size_t instrSize = sizeof(uint32_t);

	if (maxLen < instrSize) {
		assert(false);
		return 0;
	}

	// Read the instruction word:
	uint32_t instructionWord = ((uint32_t *) (void *) instruction)[0];
	if (m_isBigEndian)
		instructionWord = BE32_TO_HOST(instructionWord);
	else
		instructionWord = LE32_TO_HOST(instructionWord);

	const uint32_t iword = instructionWord;

	bool isMEMBinstruction = false;

	uint32_t displacementWord = ((uint32_t *) (void *) instruction)[1];
	if (m_isBigEndian)
		displacementWord = BE32_TO_HOST(displacementWord);
	else
		displacementWord = LE32_TO_HOST(displacementWord);

	// ... and add it to the result:
	{
		stringstream ss;
		ss.flags(std::ios::hex);
		ss << std::setfill('0') << std::setw(8) << (uint32_t) iword;
		if (isMEMBinstruction)
			ss << " " << (uint32_t) displacementWord;
		else
			ss << "         ";
		result.push_back(ss.str());
	}

	const int opcode = iword >> 24;
	
	const int REG_src_dst  = (iword >> 19) & 0x1f;
	const int REG_src2     = (iword >> 14) & 0x1f;
	const int REG_m3       = (iword >> 13) & 0x1;
	const int REG_m2       = (iword >> 12) & 0x1;
	const int REG_m1       = (iword >> 11) & 0x1;
	const int REG_opcode2  = (iword >> 7) & 0xf;
	const int REG_sfr      = (iword >> 5) & 0x3;
	const int REG_src1     = (iword >> 0) & 0x1f;
	
	const int COBR_src_1   = (iword >> 19) & 0x1f;
	const int COBR_src_2   = (iword >> 14) & 0x1f;
	const int COBR_m1      = (iword >> 13) & 0x1;
	const int COBR_disp    = (iword >> 2) & 0x7ff;
	const int COBR_sfr     = (iword >> 0) & 0x3;
	
	const int CTRL_disp    = (iword >> 2) & 0x3fffff;
	const int CTRL_sfr     = (iword >> 0) & 0x3;

	const int MEMA_src_dst = (iword >> 19) & 0x1f;
	const int MEMA_abase   = (iword >> 14) & 0x1f;
	const int MEMA_md      = (iword >> 13) & 0x1;
	const int MEMA_zero    = (iword >> 12) & 0x1;
	const int MEMA_offset  = (iword >> 0) & 0xfff;

	const int MEMB_src_dst = (iword >> 19) & 0x1f;
	const int MEMB_abase   = (iword >> 14) & 0x1f;
	const int MEMB_mode    = (iword >> 10) & 0xf;
	const int MEMB_scale   = (iword >> 7) & 0x7;
	const int MEMB_sfr     = (iword >> 5) & 0x3;
	const int MEMB_index   = (iword >> 0) & 0x1f;

	stringstream ss;
	
	switch (opcode) {

	/*
	 *  REG:
	 */

	/*
	 *  COBR:
	 */

	/*
	 *  CTRL:
	 */

	/*
	 *  MEMA:
	 */

	case 0x8c:	// lda
		{
			ss << "lda";
		}
		break;

	/*
	 *  MEMB:
	 */

	default:
		ss << "unimplemented: " << opcode;
		break;
	}

	result.push_back(ss.str());

	return instrSize;
}


string I960_CPUComponent::GetAttribute(const string& attributeName)
{
	if (attributeName == "stable")
		return "yes";

	if (attributeName == "description")
		return "Intel i960 processor.";

	return Component::GetAttribute(attributeName);
}


/*****************************************************************************/



/*
DYNTRANS_INSTR(I960_CPUComponent,multu)
{
	DYNTRANS_INSTR_HEAD(I960_CPUComponent)

	uint32_t a = REG64(ic->arg[1]), b = REG64(ic->arg[2]);
	uint64_t res = (uint64_t)a * (uint64_t)b;

	cpu->m_lo = (int32_t)res;
	cpu->m_hi = (int32_t)(res >> 32);
}


DYNTRANS_INSTR(I960_CPUComponent,slt)
{
	REG64(ic->arg[0]) = (int64_t)REG64(ic->arg[1]) < (int64_t)REG64(ic->arg[2]);
}
*/


/*****************************************************************************/


void I960_CPUComponent::Translate(uint32_t iword, struct DyntransIC* ic)
{
	// bool singleInstructionLeft = (m_executedCycles == m_nrOfCyclesToExecute - 1);
	UI* ui = GetUI();	// for debug messages

	unsigned int opcode = iword >> 24;

	switch (opcode) {

	default:
		if (ui != NULL) {
			stringstream ss;
			ss.flags(std::ios::hex);
			ss << "unimplemented opcode 0x" << opcode;
			ui->ShowDebugMessage(this, ss.str());
		}
	}
}


DYNTRANS_INSTR(I960_CPUComponent,ToBeTranslated)
{
	DYNTRANS_INSTR_HEAD(I960_CPUComponent)

	cpu->DyntransToBeTranslatedBegin(ic);

	uint32_t iword;
	if (cpu->DyntransReadInstruction(iword))
		cpu->Translate(iword, ic);

	cpu->DyntransToBeTranslatedDone(ic);
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

#include "ComponentFactory.h"

static void Test_I960_CPUComponent_Create()
{
	refcount_ptr<Component> cpu = ComponentFactory::CreateComponent("i960_cpu");
	UnitTest::Assert("component was not created?", !cpu.IsNULL());

	const StateVariable * p = cpu->GetVariable("pc");
	UnitTest::Assert("cpu has no pc state variable?", p != NULL);
	UnitTest::Assert("initial pc", p->ToString(), "0");
}

static void Test_I960_CPUComponent_Disassembly_Basic()
{
	refcount_ptr<Component> i960_cpu =
	    ComponentFactory::CreateComponent("i960_cpu");
	CPUComponent* cpu = i960_cpu->AsCPUComponent();

	vector<string> result;
	size_t len;
	unsigned char instruction[sizeof(uint32_t)];
	// This assumes that the default endianness is BigEndian...
	instruction[0] = 0x27;
	instruction[1] = 0xbd;
	instruction[2] = 0xff;
	instruction[3] = 0xd8;

	len = cpu->DisassembleInstruction(0x12345678, sizeof(uint32_t),
	    instruction, result);

	UnitTest::Assert("disassembled instruction was wrong length?", len, 4);
	UnitTest::Assert("disassembly result incomplete?", result.size(), 3);
	UnitTest::Assert("disassembly result[0]", result[0], "27bdffd8");
	UnitTest::Assert("disassembly result[1]", result[1], "addiu");
	UnitTest::Assert("disassembly result[2]", result[2], "sp,sp,-40");
}

UNITTESTS(I960_CPUComponent)
{
	UNITTEST(Test_I960_CPUComponent_Create);
	UNITTEST(Test_I960_CPUComponent_Disassembly_Basic);
}

#endif
