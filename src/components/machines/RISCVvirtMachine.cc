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
 *
 *
 *  RISC-V "virt" machine
 *
 *  The idea is to mimic the "virt" machine in QEMU, to be able to run code
 *  such as FreeBSD/riscv build for that machine.
 *
 *	    [VIRT_DEBUG] =       {        0x0,         0x100 },
 *	    [VIRT_MROM] =        {     0x1000,       0x11000 },
 *	    [VIRT_TEST] =        {   0x100000,        0x1000 },
 *	    [VIRT_CLINT] =       {  0x2000000,       0x10000 },
 *	    [VIRT_PLIC] =        {  0xc000000,     0x4000000 },
 *	    [VIRT_UART0] =       { 0x10000000,         0x100 },
 *	    [VIRT_VIRTIO] =      { 0x10001000,        0x1000 },
 *	    [VIRT_DRAM] =        { 0x80000000,           0x0 },
 *	    [VIRT_PCIE_MMIO] =   { 0x40000000,    0x40000000 },
 *	    [VIRT_PCIE_PIO] =    { 0x03000000,    0x00010000 },
 *	    [VIRT_PCIE_ECAM] =   { 0x30000000, 0x10000000 }
 */

#include "components/RISCVvirtMachine.h"
#include "ComponentFactory.h"
#include "GXemul.h"


refcount_ptr<Component> RISCVvirtMachine::Create(const ComponentCreateArgs& args)
{
	// Defaults:
	ComponentCreationSettings settings;
	settings["cpu"] = "RV64G";
	settings["ram"] = "0x80000000";	// 2 GB
	settings["ncpus"] = "1";

	if (!ComponentFactory::GetCreationArgOverrides(settings, args))
		return NULL;


	refcount_ptr<Component> machine =
	    ComponentFactory::CreateComponent("machine", args.gxemul);
	if (machine.IsNULL())
		return NULL;

	machine->SetVariableValue("template", "\"riscv-virt\"");


	refcount_ptr<Component> mainbus =
	    ComponentFactory::CreateComponent("mainbus", args.gxemul);
	if (mainbus.IsNULL())
		return NULL;

	machine->AddChild(mainbus);


	refcount_ptr<Component> ram = ComponentFactory::CreateComponent("ram", args.gxemul);
	if (ram.IsNULL())
		return NULL;

	ram->SetVariableValue("memoryMappedSize", settings["ram"]);
	ram->SetVariableValue("memoryMappedBase", "0x80000000");
	mainbus->AddChild(ram);


	int ncpus;
	stringstream tmpss3;
	tmpss3 << settings["ncpus"];
	tmpss3 >> ncpus;
	if (ncpus < 1) {
		if (args.gxemul != NULL)
			args.gxemul->GetUI()->ShowDebugMessage("nr of cpus must be more than 0.");
		return NULL;
	}

	for (int i=0; i<ncpus; ++i) {
		refcount_ptr<Component> cpu =
		    ComponentFactory::CreateComponent("riscv_cpu(model=" + settings["cpu"] + ")", args.gxemul);
		if (cpu.IsNULL())
			return NULL;

		if (i > 0)
			cpu->SetVariableValue("paused", "true");

		mainbus->AddChild(cpu);
	}

	return machine;
}


string RISCVvirtMachine::GetAttribute(const string& attributeName)
{
	if (attributeName == "template")
		return "yes";

	if (attributeName == "machine")
		return "yes";

	if (attributeName == "description")
		return "RISC-V virt machine.";

	return "";
}

