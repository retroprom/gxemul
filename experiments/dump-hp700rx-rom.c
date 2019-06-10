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
 *  HP 700/RX (i960) memory range dumping tool by Anders Gavare (gavare@gmail.com)
 *
 *  Can be used to dump the 512 KB ROM at 0xfff80000 or RAM or any other interesting range.
 *
 *  Build it using i960-unknown-coff toolchain like this, to produce a b.out "module"
 *  that can be accepted by the HP 700/RX.
 *
 *	i960-unknown-coff-gcc -c dump.c -Wall
 *	i960-unknown-coff-ld dump.o -o dump --relocatable -e _entry_point_data
 *	i960-unknown-coff-objcopy dump -O b.out.little dump.bout --strip-unneeded
 *
 *
 *  The output is displayed both on the screen and on the serial port.
 *  It is sent three times. It can then relatively easily be parsed.
 *
 *  Yes, it is horrible, but it works.
 */


// #define TEST_NATIVE

#ifdef TEST_NATIVE
void logPrintf(char* format, ...) { }
#endif


// Not accurate, but at least does something.
void delay(int sec)
{
	char buf[1];
	int n = 3000;
	int i;
	
	buf[0] = 42;

	while (sec > 0) {
		for (i = 0; i < n; ++i) {
			do {
				buf[0]--;
			} while (buf[0] != 42);
		}
		sec--;
	}
}


// Dumps 16 bytes in hex and ascii, then advances ofs bytes.
void dump(int fd, char** addrp, int ofs)
{
	unsigned char* addr = *addrp;
	char buf[50];
	int i;
	
	logPrintf("%08x ", addr);
	if (fd >= 0) {
		sprintf(buf, "%08x ", addr);
		write(fd, buf, strlen(buf));
	}

	for (i = 0; i < 16; ++i) {
		logPrintf("%02x ", addr[i]);
		if (fd >= 0) {
			sprintf(buf, "%02x ", addr[i]);
			write(fd, buf, strlen(buf));
		}
	}

	for (i = 0; i < 16; ++i) {
		unsigned char c = addr[i];
		if (c < 32 || c >= 127)
			c = '_';
		logPrintf("%c", c);
		if (fd >= 0) {
			sprintf(buf, "%c", c);
			write(fd, buf, strlen(buf));
		}
	}

	logPrintf("\n");
	if (fd >= 0) {
		sprintf(buf, "\n");
		write(fd, buf, strlen(buf));
	}

	addr += ofs;
	
	*addrp = addr;
}


int f()
{
	int fd = open("/dev/serial", 2); //  (2 = UPDATE)
	int i;
	char* addr = &f;

	printf("printf: &f = %08x\n", &f);
	logPrintf("logPrintf: &f = %08x\n", &f);

	printf("printf: fd = %08x\n", fd);
	logPrintf("logPrintf: fd = %08x\n", fd);

	if (fd >= 0) {
		char *buffer = "Testing testing on /dev/serial\r\n\r\n";
		write(fd, buffer, strlen(buffer));
	}

	delay(5);

	addr = (char*) ( ((long)addr) & ~0xfffff );

	// Rough map:
	// ----------
	//
	// 0x00000000 .. 0x0fffffff = filled with 0xff and occasional 0xdb or 0xdf or 0xfb. (First 1 KB is supposed to be CPU built-in RAM.)
	// 0x1 = hang with weird graphics pattern
	// 0x20000000 .. 0x2fffffff = filled with 0xff mostly.
	// 0x30000000 .. 0x3fffffff = 2MB RAM. Actually just 2 MB which repeats itself over the entire range.
	// 0x40000000 .. 0x407fffff = 8MB RAM.
	// 0x40800000 .. 0x40ffffff = Same as first 8 MB at 0x40000000.
	// 0x41000000 .. 0x41ffffff = Video RAM (supposed to be 2 MB but NOT linearly mapped?!)
	// 0x42000000 .. 0x43ffffff = same as 0x40000000 .. 0x41ffffff etc.?
	// 0x5 = hang with weird graphics pattern
	// 0x60000000 = just hangs
	// 0x70000000 .. 0x8fffffff = OK dumpable
	// 0x90000000 = just hangs
	// 0xa0000000 = hangs with weird graphics pattern
	// 0xb0000000 = hangs with weird graphics pattern
	// 0xc0000000 = some devices (?)
	// 0xd0000000 .. 0xfff7ffff = filled with 0xff on read
	// 0xfff8xxxx = ROM
	// 0xffffff00 = Initial Boot Record (IBR), containing first instruction pointer etc.

	addr = (char*)(int)0xfff80000;

	for (;;) {
		for (i = 0; i < 16; ++i) {
			dump(fd, &addr, 0x0);
			dump(fd, &addr, 0x0);
			dump(fd, &addr, 0x10);

// These can be used when scanning through the entire 4 GB memory space for interesting regions.
//			dump(fd, &addr, 0x00100000 - 0x10);
//			dump(fd, &addr, 0x00100000 - 0x10);
		}

		if (addr == 0)
			break;
	}

	return -2;
}


#ifdef TEST_NATIVE
int main()
{
	f();
}
#endif


/*
 *  This is the weird way entry points are specified for the
 *  loadable b.out module files for the HP 700/RX. f is the actual
 *  code entry point.
 */

unsigned int entry_point_data[3] = { 0xa9ad646a, 2, (unsigned int) &f };

