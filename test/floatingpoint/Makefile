CFLAGS=-DHOSTNATIVE -I../../src/include/testmachine

all:
	@echo Read the Makefile to see which targets are available.
	@echo To build for the host, type \"make native\"

fptest: fptest.o fpconst.o
	$(CC) fptest.o fpconst.o -o fptest

native: clean fptest
	./fptest > fptest_hostnative.output
	cat fptest_hostnative.output

clean:
	rm -f *.o fptest fptest.output *core


##############################################################################


arm: clean
	arm-unknown-elf-gcc -I../../src/include/testmachine -g fptest.c -O2 -c -mhard-float
	arm-unknown-elf-gcc -I../../src/include/testmachine -g fpconst.c -O2 -c -mhard-float
	arm-unknown-elf-ld -e f fptest.o fpconst.o -o fptest
	file fptest
	../../gxemul -qE testarm fptest > fptest_arm.output
	diff fptest_hostnative.output fptest_arm.output

mips32: clean
	mips64-unknown-elf-gcc -DMIPS -I../../src/include/testmachine -g fptest.c -O0 -mips1 -mabi=32 -c -G 0
	mips64-unknown-elf-gcc -DMIPS -I../../src/include/testmachine -g fpconst.c -O0 -mips1 -mabi=32 -c -G 0
	mips64-unknown-elf-ld -Ttext 0x80030000 -e f fptest.o fpconst.o -o fptest
	file fptest
	../../gxemul -C 4Kc -qE testmips fptest > fptest_mips32.output
	diff fptest_hostnative.output fptest_mips32.output

mips64: clean
	mips64-unknown-elf-gcc -DMIPS -I../../src/include/testmachine -g fptest.c -O0 -mips4 -mabi=64 -c -G 0
	mips64-unknown-elf-gcc -DMIPS -I../../src/include/testmachine -g fpconst.c -O0 -mips4 -mabi=64 -c -G 0
	mips64-unknown-elf-ld -Ttext 0xa800000000030000 -e f fptest.o fpconst.o -o fptest --oformat=elf64-bigmips
	file fptest
	../../gxemul -qE testmips fptest > fptest_mips64.output
	diff fptest_hostnative.output fptest_mips64.output

riscv: clean
	riscv64-unknown-elf-gcc -I../../src/include/testmachine -O2 -g fptest.c -c
	riscv64-unknown-elf-gcc -I../../src/include/testmachine -O2 -g fpconst.c -c
	riscv64-unknown-elf-ld -e _f fptest.o fpconst.o -o fptest
	file fptest
	../../gxemul -qE testriscv fptest > fptest_riscv.output
	diff fptest_hostnative.output fptest_riscv.output

sh4: clean
	sh4-unknown-elf-gcc -I../../src/include/testmachine -O2 -g fptest.c -c
	sh4-unknown-elf-gcc -I../../src/include/testmachine -O2 -g fpconst.c -c
	sh4-unknown-elf-ld -e _f fptest.o fpconst.o -o fptest
	file fptest
	../../gxemul -qE testsh fptest > fptest_sh4.output
	diff fptest_hostnative.output fptest_sh4.output


