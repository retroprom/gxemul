/*
 *  Copyright (C) 2005-2021  Anders Gavare.  All rights reserved.
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
 *  This file contains things that don't fit anywhere else, and fake/dummy
 *  implementations of libc functions that are missing on some systems.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "cpu.h"
#include "misc.h"


bool enable_colorized_output = true;


static bool use_colorized_output()
{
	static bool isatty_initialized = false;
	static bool r = false;

	if (!isatty_initialized) {
		r = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
		isatty_initialized = true;
	}

	return r && enable_colorized_output;
}


void color_prompt()
{
	if (use_colorized_output())
		printf("\e[34;1m");
}


void color_normal()
{
	if (use_colorized_output())
		printf("%s", color_normal_ptr());
}

void color_error(bool bold)
{
	if (!use_colorized_output())
		return;

	if (bold)
		printf("\e[31;1m");
	else
		printf("\e[31m");
}


void color_debugmsg_subsystem()
{
	if (use_colorized_output())
		printf("\e[33m");
}


void color_debugmsg_name()
{
	if (use_colorized_output())
		printf("\e[34;1m");
}


void color_banner()
{
	if (use_colorized_output())
		printf("\e[1m");
}


void color_pc_indicator()
{
	if (!use_colorized_output())
		return;

	printf("\e[31m");
}


const char* color_symbol_ptr()
{
	if (use_colorized_output())
		return "\e[35m";
	else
		return "";
}


const char* color_normal_ptr()
{
	if (use_colorized_output())
		return "\e[0m";
	else
		return "";
}


// https://en.wikipedia.org/wiki/Xorshift#xorshift*
// "A xorshift* generator takes a xorshift generator and applies an invertible
//  multiplication (modulo the word size) to its output as a non-linear
//  transformation, as suggested by Marsaglia.[1] The following 64-bit
//  generator with 64 bits of state has a maximal period of 2^64-1 [7] and
//  fails only the MatrixRank test of BigCrush."
uint64_t xorshift64star(uint64_t *state)
{
	uint64_t x = *state;	/* The state must be seeded with a nonzero value. */
	x ^= x >> 12; // a
	x ^= x << 25; // b
	x ^= x >> 27; // c
	*state = x;
	return x * 0x2545F4914F6CDD1D;
}


/*
 *  mystrtoull():
 *
 *  This function is used on OSes that don't have strtoull() in libc.
 */
unsigned long long mystrtoull(const char *s, char **endp, int base)
{
	unsigned long long res = 0;
	int minus_sign = 0;

	if (s == NULL)
		return 0;

	/*  TODO: Implement endp?  */
	if (endp != NULL) {
		fprintf(stderr, "mystrtoull(): endp isn't implemented\n");
		exit(1);
	}

	if (s[0] == '-') {
		minus_sign = 1;
		s++;
	}

	/*  Guess base:  */
	if (base == 0) {
		if (s[0] == '0') {
			/*  Just "0"? :-)  */
			if (!s[1])
				return 0;
			if (s[1] == 'x' || s[1] == 'X') {
				base = 16;
				s += 2;
			} else {
				base = 8;
				s ++;
			}
		} else if (s[0] >= '1' && s[0] <= '9')
			base = 10;
	}

	while (s[0]) {
		int c = s[0];
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			c = c - 'A' + 10;
		else
			break;
		switch (base) {
		case 8:	res = (res << 3) | c;
			break;
		case 16:res = (res << 4) | c;
			break;
		default:res = (res * base) + c;
		}
		s++;
	}

	if (minus_sign)
		res = (uint64_t) -(int64_t)res;
	return res;
}


/*
 *  mymkstemp():
 *
 *  mkstemp() replacement for systems that lack that function. This is NOT
 *  really safe, but should at least allow the emulator to build and run.
 */
int mymkstemp(char *templ)
{
	int h = 0;
	char *p = templ;

	while (*p) {
		if (*p == 'X')
			*p = 48 + random() % 10;
		p++;
	}

	h = open(templ, O_RDWR | O_CREAT | O_EXCL, 0600);
	return h;
}


#ifdef USE_STRLCPY_REPLACEMENTS
/*
 *  mystrlcpy():
 *
 *  Quick hack strlcpy() replacement for systems that lack that function.
 *  NOTE: No length checking is done.
 */
size_t mystrlcpy(char *dst, const char *src, size_t size)
{
	strcpy(dst, src);
	return strlen(src);
}


/*
 *  mystrlcat():
 *
 *  Quick hack strlcat() replacement for systems that lack that function.
 *  NOTE: No length checking is done.
 */
size_t mystrlcat(char *dst, const char *src, size_t size)
{
	size_t orig_dst_len = strlen(dst);
	strcat(dst, src);
	return strlen(src) + orig_dst_len;
}
#endif


/*
 *  print_separator_line():
 *
 *  Prints a line of "----------".
 */
void print_separator_line(void)
{
        int i = 79; 
        while (i-- > 0)
                debug("-");
        debug("\n");
}


/*
 *  size_to_mask():
 *
 *  For e.g. 0x1000, the mask returned is 0xfff.
 *  For e.g. 0x1400, the mask returned is 0x1fff.
 *
 *  In other words, the returned value is the smallest mask that can be applied
 *  which preserves any address within the range 0..(size-1).
 */
uint64_t size_to_mask(uint64_t size)
{
	if (size == 0)
		return 0;

	size --;

	uint64_t mask = 1;

	while (size > 0)
	{
		size >>= 1;
		mask = (mask << 1) | 1;
	}

	return mask;
}


