/*
 *  Copyright (C) 2019  Anders Gavare.  All rights reserved.
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
#include "components/RISCV_CPUComponent.h"


RISCV_CPUComponent::RISCV_CPUComponent()
	: CPUDyntransComponent("riscv_cpu", "RISCV")
{
	m_frequency = 25e6;
	m_isBigEndian = false;

	m_model = "RV64G";
	m_extensions = RISCV_EXTENSION_I;

	ResetState();

	AddVariable("model", &m_model);

	for (size_t i = 0; i < N_RISCV_XREGS; i++) {
		AddVariable(RISCV_regnames[i], &m_x[i]);
	}
}


refcount_ptr<Component> RISCV_CPUComponent::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["model"] = "RV64G";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;

	refcount_ptr<Component> cpu = new RISCV_CPUComponent();
	if (!cpu->SetVariableValue("model", "\"" + settings["model"] + "\""))
		return NULL;

	return cpu;
}


void RISCV_CPUComponent::ResetState()
{
	m_pageSize = 4096;

	for (size_t i=0; i<N_RISCV_XREGS; i++)
		m_x[i] = 0;

	m_pc = 0;

	CPUDyntransComponent::ResetState();
}


bool RISCV_CPUComponent::PreRunCheckForComponent(GXemul* gxemul)
{
	if (m_pc & 0x1) {
		gxemul->GetUI()->ShowDebugMessage(this, "the pc register"
		    " can not have bit 0 set!\n");
		return false;
	}

	return CPUDyntransComponent::PreRunCheckForComponent(gxemul);
}


bool RISCV_CPUComponent::CheckVariableWrite(StateVariable& var, const string& oldValue)
{
	// UI* ui = GetUI();

	return CPUDyntransComponent::CheckVariableWrite(var, oldValue);
}


void RISCV_CPUComponent::ShowRegisters(GXemul* gxemul, const vector<string>& arguments) const
{
	stringstream ss;

	ss.flags(std::ios::hex);
	ss << "  pc = 0x" << std::setfill('0') << std::setw(16) << (uint32_t)m_pc;

	string symbol = GetSymbolRegistry().LookupAddress(m_pc, true);
	if (symbol != "")
		ss << " <" << symbol << ">";
	ss << "\n";

	for (size_t i = 0; i < N_RISCV_XREGS; i++) {
		ss << std::setfill(' ') << std::setw(4) << RISCV_regnames[i]
			<< " = 0x" << std::setfill('0') << std::setw(16) << m_x[i];
		if ((i&3) == 3)
			ss << "\n";
		else
			ss << " ";
	}

	gxemul->GetUI()->ShowDebugMessage(ss.str());
}


int RISCV_CPUComponent::FunctionTraceArgumentCount()
{
	return 8;
}


int64_t RISCV_CPUComponent::FunctionTraceArgument(int n)
{
	return m_x[10 + n];
}


bool RISCV_CPUComponent::FunctionTraceReturnImpl(int64_t& retval)
{
	retval = m_x[10];
	return true;
}


int RISCV_CPUComponent::GetDyntransICshift() const
{
	// A "parcel" is 16 bits (shift = 1). Most instructions are 32 bits
	// wide, but it varies.
	return 1;
}


void (*RISCV_CPUComponent::GetDyntransToBeTranslated())(CPUDyntransComponent*, DyntransIC*)
{
	return instr_ToBeTranslated;
}


bool RISCV_CPUComponent::VirtualToPhysical(uint64_t vaddr, uint64_t& paddr,
	bool& writable)
{
	paddr = vaddr;
	writable = true;
	return true;
}


uint64_t RISCV_CPUComponent::PCtoInstructionAddress(uint64_t pc)
{
	return pc;
}


size_t RISCV_CPUComponent::DisassembleInstruction(uint64_t vaddr, vector<string>& result)
{
	uint16_t iwords[RISCV_MAX_PARCELS];

	AddressSelect(vaddr);
	bool readOk = ReadData(iwords[0], m_isBigEndian? BigEndian : LittleEndian);
	int nparcels = 0;
	if (readOk) {
		/*
		 *  Figure out RISC-V instruction length from the first
		 *  16-bit "parcel" (which may be a whole or a partial instruction):
		 *
		 *	xxxxxxxxxxxxxxaa	aa != 11	16-bit (1 parcel)
		 *	xxxxxxxxxxxbbb11	bbb != 111	32-bit (2 parcels)
		 *	xxxxxxxxxx011111			48-bit (3 parcels)
		 *	xxxxxxxxx0111111			64-bit (4 parcels)
		 *	xnnnxxxxx1111111	nnn != 111	(80+16*nnn)-bit
		 *	x111xxxxx1111111			>= 192 bit
		 */

		int aa = iwords[0] & 3, bbb = (iwords[0] >> 2) & 7;

		nparcels = 1;
		
		if (aa == 3) {
			nparcels = 2;
			if (bbb == 7) {
				nparcels = 3;
				if (iwords[0] & 0x20) {
					nparcels = 4;
					if (iwords[0] & 0x40) {
						int nnn = (iwords[0] >> 12) & 7;
						nparcels = 5 + nnn;
						
						if (nparcels > RISCV_MAX_PARCELS) {
							result.push_back("too many parcels in instruction");
							nparcels = 0;
						}
					} 
				} 
			}
		}

		int instructionParcelsSuccessfullyRead = 1;
		for (int i = 1; i < nparcels; ++i) {
			AddressSelect(vaddr + i * sizeof(uint16_t));
			if (ReadData(iwords[i], m_isBigEndian? BigEndian : LittleEndian))
				instructionParcelsSuccessfullyRead ++;
		}

		if (instructionParcelsSuccessfullyRead != nparcels)
			nparcels = 0;
	}

	if (nparcels == 0) {
		result.push_back("instruction could not be read");
		return 0;
	}

	stringstream ssHex;
	ssHex.flags(std::ios::hex);
	for (int i = nparcels-1; i >= 0; --i)
		ssHex << std::setfill('0') << std::setw(4) << (uint32_t) iwords[i];

	result.push_back(ssHex.str());


	stringstream ssOpcode;
	stringstream ssArgs;
	stringstream ssComments;


	/*
	 *  See this URL for a nice ordered list of all RISC-V instructions:
	 *
	 *  https://github.com/rv8-io/rv8/blob/master/doc/pdf/riscv-instructions.pdf
	 */

	int opcode = iwords[0] & 0x7f;
	int rd = (iwords[0] >> 7) & 31;
	uint64_t requiredExtension = 0;

	/*
	 *  RV32I Base Integer Instruction Set:
	 */
	// TYPE-U:
	// TYPE-UJ:
	if (opcode == 0x6f) {
		//  jal
		int32_t simm = ((iwords[1] & 0x8000) << 5);
		simm |= ((iwords[1] & 0x7fe0) >> 4);
		simm |= ((iwords[1] & 0x0010) << 7);
		simm |= ((iwords[1] & 0x000f) << 16);
		simm |= ((iwords[0] & 0xf000) << 0);
		simm <<= 11;
		simm >>= 11;
		ssOpcode << (rd ? "jal" : "j");
		if (rd)
			ssArgs << RISCV_regnames[rd] << ",";
		uint64_t addr = vaddr + simm;
		ssArgs.flags(std::ios::hex | std::ios::showbase);
		ssArgs << std::setfill('0') << addr;
		string symbol = GetSymbolRegistry().LookupAddress(addr, true);
		if (symbol != "")
			ssArgs << " <" + symbol + ">";
		requiredExtension = RISCV_EXTENSION_I;
	}
	// TYPE-I:
	// TYPE-SB:
	// TYPE-S:
	// TYPE-R:
	
	else {
		ssOpcode << "unknown main opcode 0x";
		ssOpcode.flags(std::ios::hex);
		ssOpcode << std::setfill('0') << std::setw(2) << (int)opcode;
	}


	result.push_back(ssOpcode.str());
	result.push_back(ssArgs.str());

	string comments = ssComments.str();
	if (comments.length() > 0)
		result.push_back(comments);

	if ((m_extensions & requiredExtension) != requiredExtension) {
		if (comments.length() == 0)
			result.push_back("");
	
		result.push_back("; extension not implemented by this CPU");
	}

	return sizeof(uint16_t) * nparcels;
}


string RISCV_CPUComponent::GetAttribute(const string& attributeName)
{
	if (attributeName == "description")
		return "RISC-V processor.";

	return Component::GetAttribute(attributeName);
}


/*****************************************************************************/


/*
DYNTRANS_INSTR(RISCV_CPUComponent,b)
{
	DYNTRANS_INSTR_HEAD(RISCV_CPUComponent)
	cpu->m_pc = ic->arg[0].u32;
	cpu->DyntransPCtoPointers();
}


DYNTRANS_INSTR(RISCV_CPUComponent,lda_displacement)
{
	DYNTRANS_INSTR_HEAD(RISCV_CPUComponent)
	REG32(ic->arg[2]) = ic->arg[0].u32;
	cpu->m_nextIC = ic + 2;
}


DYNTRANS_INSTR(RISCV_CPUComponent,mov_lit_reg)
{
	REG32(ic->arg[2]) = ic->arg[0].u32;
}
*/


/*****************************************************************************/


void RISCV_CPUComponent::Translate(uint16_t iwords[], int nparcels, struct DyntransIC* ic)
{
	UI* ui = GetUI();	// for debug messages

// TODO:
	unsigned int opcode = iwords[0];

	if (ic->f == NULL && ui != NULL) {
		stringstream ss;
		ss.flags(std::ios::hex);
		ss << "unimplemented opcode 0x" << opcode;
		ui->ShowDebugMessage(this, ss.str());
	}
}


DYNTRANS_INSTR(RISCV_CPUComponent,ToBeTranslated)
{
	DYNTRANS_INSTR_HEAD(RISCV_CPUComponent)

	cpu->DyntransToBeTranslatedBegin(ic);

	uint16_t iwords[RISCV_MAX_PARCELS];
	if (cpu->DyntransReadInstruction(iwords[0])) {
		// See DisassembleInstruction for details about the length. 
		int aa = iwords[0] & 3, bbb = (iwords[0] >> 2) & 7;
		int nparcels = 1;
		if (aa == 3) {
			nparcels = 2;
			if (bbb == 7) {
				nparcels = 3;
				if (iwords[0] & 0x20) {
					nparcels = 4;
					if (iwords[0] & 0x40) {
						int nnn = (iwords[0] >> 12) & 7;
						nparcels = 5 + nnn;
						
						if (nparcels > RISCV_MAX_PARCELS) {
							UI* ui = cpu->GetUI();
							ui->ShowDebugMessage(cpu, "too many parcels in instruction");
							nparcels = 0;
						}
					} 
				} 
			}
		}

		int instructionParcelsSuccessfullyRead = 1;
		for (int i = 1; i < nparcels; ++i) {
			if (cpu->DyntransReadInstruction(iwords[i], sizeof(iwords[0]) * i))
				instructionParcelsSuccessfullyRead ++;
		}

		if (instructionParcelsSuccessfullyRead == nparcels) {
			cpu->Translate(iwords, nparcels, ic);
		} else {
			UI* ui = cpu->GetUI();
			ui->ShowDebugMessage(cpu, "last part of instruction could not be read");
		}
	}

	cpu->DyntransToBeTranslatedDone(ic);
}


/*****************************************************************************/


#ifdef WITHUNITTESTS

#include "ComponentFactory.h"

static void Test_RISCV_CPUComponent_Create()
{
	refcount_ptr<Component> cpu = ComponentFactory::CreateComponent("riscv_cpu");
	UnitTest::Assert("component was not created?", !cpu.IsNULL());

	const StateVariable * p = cpu->GetVariable("a0");
	UnitTest::Assert("cpu has no a0 state variable?", p != NULL);
}

#if 0
static void Test_RISCV_CPUComponent_Disassembly_Basic()
{
	refcount_ptr<Component> RISCV_cpu = ComponentFactory::CreateComponent("riscv_cpu");
	CPUComponent* cpu = RISCV_cpu->AsCPUComponent();

	vector<string> result;
	size_t len;
	unsigned char instruction[sizeof(uint32_t) * 2];

	// This assumes that the default endianness is little endian...
	instruction[0] = 0x00;
	instruction[1] = 0x30;
	instruction[2] = 0x68;
	instruction[3] = 0x8c;

	instruction[4] = 0x01;
	instruction[5] = 0x23;
	instruction[6] = 0x34;
	instruction[7] = 0x45;

	len = cpu->DisassembleInstruction(0x12345678, sizeof(instruction), instruction, result);

	UnitTest::Assert("disassembled instruction was wrong length?", len, 8);
	UnitTest::Assert("disassembly result incomplete?", result.size(), 3);
	UnitTest::Assert("disassembly result[0]", result[0], "8c683000 45342301");
	UnitTest::Assert("disassembly result[1]", result[1], "lda");
	UnitTest::Assert("disassembly result[2]", result[2], "0x45342301,r13");
}

static GXemul SimpleMachine()
{
	GXemul gxemul;
	gxemul.GetCommandInterpreter().RunCommand("add mainbus");
	gxemul.GetCommandInterpreter().RunCommand("add riscv_cpu mainbus0");
	gxemul.GetCommandInterpreter().RunCommand("add ram mainbus0");
	gxemul.GetCommandInterpreter().RunCommand("ram0.memoryMappedBase = 0x3fe00000");
	gxemul.GetCommandInterpreter().RunCommand("ram0.memoryMappedSize = 0x1000");
	return gxemul;
}

static void Test_RISCV_CPUComponent_Execute_mov()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00048);
	bus->WriteData((uint32_t)0x5c201e06, LittleEndian);	// mov   6,r4
	bus->AddressSelect(0x3fe0004c);
	bus->WriteData((uint32_t)0x5c201e06, LittleEndian);	// mov   6,r4

	cpu->SetVariableValue("pc", "0x3fe00048");
	cpu->SetVariableValue("r4", "0x1234");

	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have increased", cpu->GetVariable("pc")->ToInteger(), 0x3fe0004c);
	UnitTest::Assert("r4 should have been modified", cpu->GetVariable("r4")->ToInteger(), 6);

	cpu->SetVariableValue("r4", "0x12345");

	gxemul.SetRunState(GXemul::SingleStepping);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have increased again", cpu->GetVariable("pc")->ToInteger(), 0x3fe00050);
	UnitTest::Assert("r4 should have been modified again", cpu->GetVariable("r4")->ToInteger(), 6);
}

static void Test_RISCV_CPUComponent_Execute_b()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00004);
	bus->WriteData((uint32_t)0x080006c0, LittleEndian);	// b 0x3fe006c4

	cpu->SetVariableValue("pc", "0x3fe00004");

	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have changed", cpu->GetVariable("pc")->ToInteger(), 0x3fe006c4);

	cpu->SetVariableValue("pc", "0x3fe00004");

	gxemul.SetRunState(GXemul::SingleStepping);
	gxemul.Execute(1);

	UnitTest::Assert("pc should have changed again", cpu->GetVariable("pc")->ToInteger(), 0x3fe006c4);
}

static void Test_RISCV_CPUComponent_Execute_lda_with_offset()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00010);
	bus->WriteData((uint32_t)0x8c180f13, LittleEndian);	// lda r3, 0xf13

	cpu->SetVariableValue("pc", "0x3fe00010");
	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);
	UnitTest::Assert("lda length", cpu->GetVariable("pc")->ToInteger(), 0x3fe00014);
	UnitTest::Assert("lda", cpu->GetVariable("r3")->ToInteger(), 0xf13);
}

static void Test_RISCV_CPUComponent_Execute_lda_with_displacement()
{
	GXemul gxemul = SimpleMachine();
	refcount_ptr<Component> cpu = gxemul.GetRootComponent()->LookupPath("root.mainbus0.cpu0");
	AddressDataBus* bus = cpu->AsAddressDataBus();

	bus->AddressSelect(0x3fe00010);
	bus->WriteData((uint32_t)0x8cf03000, LittleEndian);	// lda
	bus->AddressSelect(0x3fe00014);
	bus->WriteData((uint32_t)0x3fe0507c, LittleEndian);	//     0x3fe0507c, g14

	cpu->SetVariableValue("pc", "0x3fe00010");
	gxemul.SetRunState(GXemul::Running);
	gxemul.Execute(1);
	UnitTest::Assert("lda length", cpu->GetVariable("pc")->ToInteger(), 0x3fe00018);
	UnitTest::Assert("lda", cpu->GetVariable("g14")->ToInteger(), 0x3fe0507c);
}
#endif

UNITTESTS(RISCV_CPUComponent)
{
	UNITTEST(Test_RISCV_CPUComponent_Create);
	
#if 0
	UNITTEST(Test_RISCV_CPUComponent_Disassembly_Basic);

	UNITTEST(Test_RISCV_CPUComponent_Execute_mov);
	UNITTEST(Test_RISCV_CPUComponent_Execute_b);
	UNITTEST(Test_RISCV_CPUComponent_Execute_lda_with_offset);
	UNITTEST(Test_RISCV_CPUComponent_Execute_lda_with_displacement);
#endif
}

#endif
