/*
 *  Copyright (C) 2003-2021  Anders Gavare.  All rights reserved.
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
 *  Debug message functionality.
 *
 *  To print a debug message, use debugmsg() or debugmsg_cpu(). The _cpu
 *  variant is if the message comes from a particular CPU. In that case, the
 *  name of the machine and cpu id may be printed.
 *
 *  In addition to the actual message to be printed, a subsystem, a component
 *  name, and a verbosity level also need to be supplied. The component name
 *  may be empty.
 *
 *  If it is relatively expensive to construct the message, then it would be
 *  a bad idea to do that every time the debugmsg is called in case it is
 *  rarely shown anyway (due to verbosity level being higher than the current
 *  setting). Then, the following construct may be used to quickly check before
 *  constructing the message that it will be shown:
 *
 *	if (ENOUGH_VERBOSITY(SUBSYS_XXX, VERBOSITY_YYY)) {
 *		char msg[....];
 *		// Construct msg here...
 *
 *		debugmsg(SUBSYS_XXX, "component", VERBOSITY_YYY, "%s", msg);
 *	}
 *
 *
 *  TODO:
 *	Debugger commands:
 *		"quiet"
 *		new command "verbosity"
 *		command for showing debug output in new xterms?
 *
 *	Convert existing debug() and fatal() calls to the new debugmsg() call.
 *
 *	Live registering of new subsystems, e.g. entire devices.
 *
 *	Make sure that all the hardcoded subsystems are actually used, and/or
 *		move registering to individual subsystems.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "console.h"
#include "cpu.h"
#include "emul.h"
#include "machine.h"
#include "misc.h"


int verbose = 0;
int quiet_mode = 0;

extern bool emul_executing;
extern bool single_step;

size_t debugmsg_nr_of_subsystems = 0;
static const char **debugmsg_subsystem_name = NULL;
int *debugmsg_current_verbosity = NULL;

static const int default_verbosity = VERBOSITY_INFO;

static size_t debug_indent = 0;
static bool debug_old_currently_at_start_of_line = true;

#define	DEBUG_BUFSIZE		2048
#define	DEBUG_INDENTATION	4


/*
 *  va_debug():
 *
 *  Used internally by the old debug() and fatal() functions.
 */
static void va_debug(va_list argp, const char *fmt)
{
	char buf[DEBUG_BUFSIZE + 1];
	buf[0] = buf[DEBUG_BUFSIZE] = 0;
	vsnprintf(buf, DEBUG_BUFSIZE, fmt, argp);

	char* s = buf;
	while (*s) {
		if (debug_old_currently_at_start_of_line) {
			for (size_t i = 0; i < debug_indent * DEBUG_INDENTATION; i++)
				printf(" ");
		}

		printf("%c", *s);

		debug_old_currently_at_start_of_line = false;
		if (*s == '\n' || *s == '\r')
			debug_old_currently_at_start_of_line = true;
		s++;
	}
}


/*
 *  debugmsg_va():
 *
 *  Used internally by debugmsg().
 */
static void debugmsg_va(struct cpu* cpu, int subsystem,
	const char *name, int verbosity, va_list argp, const char *fmt)
{
	if (!single_step && !ENOUGH_VERBOSITY(subsystem, verbosity))
		return;

	char buf[DEBUG_BUFSIZE];

	buf[0] = buf[DEBUG_BUFSIZE - 1] = 0;
	vsnprintf(buf, DEBUG_BUFSIZE, fmt, argp);

	char* s = buf;

	bool debug_currently_at_start_of_line = true;
	bool show_decorations = emul_executing && !single_step;

	if (console_are_slaves_allowed())
		show_decorations = false;

	while (true) {
		if (debug_currently_at_start_of_line) {
			if (show_decorations) {
				color_normal();
				printf("[ ");
			} else {
				for (size_t i = 0; i < debug_indent * DEBUG_INDENTATION; ++i)
					printf(" ");
			}

			if (cpu != NULL) {
				// With multiple machines being emulated in the
				// same emulation instance, print both the machine
				// name and cpu id. If it is just one machine,
				// only print the cpu id if there are multiple
				// CPUs in order to not clutter the output
				// needlessly.
				struct emul* emul = cpu->machine->emul;
				if (emul->n_machines > 1)
					printf("machine \"%s\" cpu%i: ",
					    cpu->machine->name ? cpu->machine->name : "(no name)",
					    cpu->cpu_id);
				else if (cpu->machine->ncpus > 1)
					printf("cpu%i: ", cpu->cpu_id);
			}

			if (verbosity == VERBOSITY_ERROR)
				color_error(false);
			else
				color_debugmsg_subsystem();

			bool print_colon = false;
			bool print_subsystem_name =
			    debugmsg_subsystem_name[subsystem][0] != '\0' &&
			    (debug_indent == 0 || (name == NULL || name[0] == '\0'));

			if (print_subsystem_name) {
				printf("%s", debugmsg_subsystem_name[subsystem]);
				print_colon = true;
			}

			if (name != NULL && name[0] != '\0') {
				if (print_subsystem_name)
					printf(" ");

				if (verbosity != VERBOSITY_ERROR)
					color_debugmsg_name();

				printf("%s", name);

				print_colon = true;
			}

			if (print_colon)
				printf(": ");

			if (show_decorations) {
				for (size_t i = 0; i < debug_indent * DEBUG_INDENTATION; ++i)
					printf(" ");
			}

			if (verbosity == VERBOSITY_ERROR)
				color_error(true);
			else
				color_normal();
		}

		debug_currently_at_start_of_line = false;

		if (!*s)
			break;

		switch (*s) {
		case '\n':
			if (show_decorations) {
				color_normal();
				printf(" ]");
			}

			debug_currently_at_start_of_line = true;
			printf("\n");
			break;

		default:
			printf("%c", *s);
		}

		s++;
	}

	color_normal();

	if (show_decorations)
		printf(" ]");

	if (!debug_currently_at_start_of_line)
		printf("\n");
}


/*
 *  debugmsg():
 *
 *  The main routine to call, when something should be printed.
 */
void debugmsg(int subsystem, const char *name,
	int verbosity_required, const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	debugmsg_va(NULL, subsystem, name, verbosity_required, argp, fmt);
	va_end(argp);
}


/*
 *  debugmsg():
 *
 *  The main routine to call, when something should be printed and we are
 *  in the context of a specific CPU.
 */
void debugmsg_cpu(struct cpu* cpu, int subsystem, const char *name,
	int verbosity_required, const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	debugmsg_va(cpu, subsystem, name, verbosity_required, argp, fmt);
	va_end(argp);
}


/*
 *  debug_indentation():
 *
 *  Modify the debug indentation. diff should be +1 to increase, and -1 to
 *  decrease after printing something.
 *
 *  This affects both the older debug() and fatal() functions, and the new
 *  debugmsg/debugmsg_cpu() functions.
 */
void debug_indentation(int diff)
{
	if (debug_indent == 0 && diff < 0) {
		fprintf(stderr, "WARNING: debug_indent too low!\n");
		debug_indent = 0;
	} else {
		debug_indent += diff;
	}
}


/*
 *  debug() and fatal():
 *
 *  These are here because of all the legacy code that uses them. Basically,
 *  debug() is mapped to something that is only displayed with verbosity
 *  at the default level, and fatal() is used to always print stuff. With some
 *  hacks to make it work reasonable with -q and when executing normally or
 *  when executing single-stepping.
 */
void debug(const char *fmt, ...)
{
	va_list argp;
	
	int v = verbose;
	if (emul_executing)
		v--;
	if (single_step)
		v++;

	if ((quiet_mode && !single_step) || v < 0)
		return;

	va_start(argp, fmt);
	va_debug(argp, fmt);
	va_end(argp);
}


void fatal(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	va_debug(argp, fmt);
	va_end(argp);
}


void debugmsg_set_verbosity_level(int subsystem, int verbosity)
{
	if (subsystem != SUBSYS_ALL)
		debugmsg_current_verbosity[subsystem] = verbosity;
	else
		for (size_t i = 0; i < debugmsg_nr_of_subsystems; ++i)
			debugmsg_current_verbosity[i] = verbosity;
}


void debugmsg_add_verbosity_level(int subsystem, int verbosity_delta)
{
	if (subsystem != SUBSYS_ALL)
		debugmsg_current_verbosity[subsystem] += verbosity_delta;
	else
		for (size_t i = 0; i < debugmsg_nr_of_subsystems; ++i)
			debugmsg_current_verbosity[i] += verbosity_delta;

	for (size_t i = 0; i < debugmsg_nr_of_subsystems; ++i)
		if (debugmsg_current_verbosity[i] < 0)
			debugmsg_current_verbosity[i] = 0;
}


/*
 *  debugmsg_init():
 *
 *  Initializes the debugmsg functionality.
 */
void debugmsg_init()
{
	debugmsg_nr_of_subsystems = 11;
	debugmsg_subsystem_name = malloc(sizeof(char*) * debugmsg_nr_of_subsystems);
	debugmsg_current_verbosity = malloc(sizeof(int) * debugmsg_nr_of_subsystems);;

	debugmsg_subsystem_name[SUBSYS_STARTUP]   = "";
	debugmsg_subsystem_name[SUBSYS_EMUL]      = "emul";
	debugmsg_subsystem_name[SUBSYS_DISK]      = "disk";
	debugmsg_subsystem_name[SUBSYS_NET]       = "net";
	debugmsg_subsystem_name[SUBSYS_MACHINE]   = "machine";
	debugmsg_subsystem_name[SUBSYS_DEVICE]    = "device";
	debugmsg_subsystem_name[SUBSYS_CPU]       = "cpu";
	debugmsg_subsystem_name[SUBSYS_MEMORY]    = "memory";
	debugmsg_subsystem_name[SUBSYS_EXCEPTION] = "exception";
	debugmsg_subsystem_name[SUBSYS_PROMEMUL]  = "promemul";
	debugmsg_subsystem_name[SUBSYS_X11]       = "X11";

	// Default verbosity levels.
	debugmsg_set_verbosity_level(SUBSYS_ALL, default_verbosity);
}


