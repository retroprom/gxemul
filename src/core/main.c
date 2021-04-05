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
 *  GXemul's main entry point.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "console.h"
#include "cpu.h"
#include "debugger.h"
#include "device.h"
#include "diskimage.h"
#include "emul.h"
#include "machine.h"
#include "misc.h"
#include "settings.h"
#include "timer.h"


extern bool single_step;
extern bool debugger_enter_at_end_of_run;
extern bool enable_colorized_output;

extern int verbose;
extern int quiet_mode;

extern int optind;
extern char *optarg;

struct settings *global_settings;

int extra_argc;
char **extra_argv;
char *progname;

size_t dyntrans_cache_size = DEFAULT_DYNTRANS_CACHE_SIZE;
static bool skip_srandom_call = false;


/*
 *  internal_w():
 *
 *  For internal use by gxemul itself. Currently only used to launch
 *  slave consoles.
 */
void internal_w(char *arg)
{
	if (arg == NULL || strncmp(arg, "W@", 2) != 0) {
		fprintf(stderr, "-W is for internal use by gxemul,"
		    " not for manual use.\n");
		exit(1);
	}

	arg += 2;

	switch (arg[0]) {
	case 'S':
		console_slave(arg + 1);
		break;
	default:
		fprintf(stderr, "internal_w(): UNIMPLEMENTED arg = '%s'\n",
		    arg);
	}
}


/*
 *  print_banner():
 *
 *  Prints program startup banner to stdout.
 */
static void print_banner()
{
	color_banner();
	printf("GXemul " VERSION"    " COPYRIGHT_MSG"\n" SECONDARY_MSG);
	printf("Read the source code and/or documentation for other Copyright messages.\n\n");
	color_normal();
}


/*
 *  usage():
 *
 *  Prints program usage to stdout.
 */
static void usage(bool longusage)
{
	print_banner();

	printf("Usage: ");
	color_prompt();
	printf("%s [machine, other, and general options] [file [...]]\n", progname);
	color_normal();
	printf("   or  ");
	color_prompt();
	printf("%s [general options] @configfile\n", progname);
	color_normal();
	
	if (!longusage) {
		printf("\nRun  ");
		color_prompt();
		printf("%s -h", progname);
		color_normal();
		printf("  for help on command line options.\n");
		return;
	}

	printf("\nMachine selection options:\n");
	printf("  -E t      try to emulate machine type t. (Use -H to get "
	    "a list of types.)\n");
	printf("  -e st     try to emulate machine subtype st.\n");

	printf("\nOther options:\n");
	printf("  -C x      try to emulate a specific CPU. (Use -H to get a "
	    "list of types.)\n");
	printf("  -d fname  add fname as a disk image. You can add \"xxx:\""
	    " as a prefix\n");
	printf("            where xxx is one or more of the following:\n");
	printf("                b      specifies that this is the boot"
	    " device\n");
	printf("                c      CD-ROM\n");
	printf("                d      DISK\n");
	printf("                f      FLOPPY\n");
	printf("                gH;S;  set geometry to H heads and S"
	    " sectors-per-track\n");
	printf("                i      IDE\n");
	printf("                oOFS;  set base offset to OFS (for ISO9660"
	    " filesystems)\n");
	printf("                r      read-only (don't allow changes to the file)\n");
	printf("                R      don't allow changes to the file, but add a temporary\n");
	printf("                       overlay to allow guest OS writes (which are lost when\n");
	printf("                       the emulator exits)\n");
	printf("                s      SCSI\n");
	printf("                t      tape\n");
	printf("                V      add an overlay (also requires explicit ID)\n");
	printf("                0-7    use a specific ID\n");
	printf("  -I hz     set the main cpu frequency to hz (not used by "
	    "all combinations\n            of machines and guest OSes)\n");
	printf("  -i        display each instruction as it is executed\n");
	printf("  -J        disable dyntrans instruction combinations\n");
	printf("  -j name   set the name of the kernel; for DECstation "
	    "emulation, this passes\n            the name to the bootloader,"
	    " for example:\n");
	printf("                -j netbsd     (NetBSD/pmax)      "
	    "-j bsd      (OpenBSD/pmax)\n");
	printf("                -j vmsprite   (Sprite/pmax)      "
	    "-j vmunix   (Ultrix/RISC)\n");
	printf("            For other emulation modes, if the boot disk is an"
	    " ISO9660\n            filesystem, -j sets the name of the"
	    " kernel to load.\n");
	printf("  -L tapdev enable tap networking using device 'tapdev'\n");
	printf("  -M m      emulate m MBs of physical RAM\n");
	printf("  -N        display nr of instructions/second average, at"
	    " regular intervals\n");
	printf("  -n nr     set nr of CPUs (for SMP experiments)\n");
	printf("  -O        force netboot (tftp instead of disk), even when"
	    " a disk image is\n"
	    "            present (for DECstation, SGI, and ARC emulation)\n");
	printf("  -o arg    set the boot argument, for DEC, ARC, or SGI"
	    " emulation\n");
	printf("            (default arg for DEC is -a, for ARC/SGI -aN)\n");
	printf("  -p pc     add a breakpoint (remember to use the '0x' "
	    "prefix for hex!)\n");
	printf("  -Q        no built-in PROM emulation  (use this for "
	    "running ROM images)\n");
	printf("  -R        use random bootstrap cpu, instead of nr 0\n");
	printf("  -r        register dumps before every instruction\n");
	printf("  -S        initialize emulated RAM to random bytes, "
	    "instead of zeroes\n");
	printf("  -s f:name write statistics to file 'name', "
	    "f is one or more of the following:\n");
	printf("                v    virtual program counter\n");
	printf("                p    physical equivalent of program counter\n");
	printf("                i    internal ic->f representation of "
	    "the program counter\n");
	printf("            and optionally:\n");
	printf("                d    disable statistics gathering at "
	    "startup\n");
	printf("                o    overwrite instead of append\n");
	printf("  -T        halt on non-existant memory accesses\n");
	printf("  -t        show function trace tree\n");
#ifdef WITH_X11
	printf("  -X        use X11\n");
	printf("  -Y n      scale down framebuffer windows by n x n times\n");
#endif /*  WITH_X11  */
	printf("  -Z n      set nr of graphics cards, for emulating a "
	    "dual-head or tripple-head\n"
	    "            environment (only for DECstation emulation)\n");
#ifdef WITH_X11
	printf("  -z disp   add disp as an X11 display to use for framebuffers\n");
#endif /*  WITH_X11  */

	printf("\nGeneral options:\n");
	printf("  -A        disable colorized output\n");
	printf("  -c cmd    add cmd as a command to run before starting "
	    "the simulation\n");
	printf("  -D        skip the srandom call at startup\n");
	printf("  -G        enable colorized output (same as if the CLICOLOR"
	    " env. var is set)\n");
	printf("  -H        display a list of possible CPU and "
	    "machine types\n");
	printf("  -h        display this help message\n");
	printf("  -k n      set dyntrans translation caches to n MB (default"
	    " size is %i MB)\n", DEFAULT_DYNTRANS_CACHE_SIZE / 1048576);
	printf("  -K        show the debugger prompt instead of exiting, when a simulation ends\n");
	printf("  -q        quiet mode (don't print startup messages)\n");
	printf("  -V        start up in the interactive debugger, paused; this also sets -K\n");
	printf("  -v        increase debug message verbosity\n");
#ifdef WITH_X11
	printf("  -x        open up new xterms for emulated serial ports (default is on when\n"
	       "            using configuration files with multiple machines specified, or\n"
	       "            when X11 is used, off otherwise)\n");
#endif /*  WITH_X11  */
	printf("\n");
	printf("If you are selecting a machine type to emulate directly "
	    "on the command line,\nthen you must specify one or more names"
	    " of files that you wish to load into\n"
	    "memory. Supported formats are:   ELF a.out ecoff srec syms raw\n"
	    "where syms is the text produced by running 'nm' (or 'nm -S') "
	    "on a binary.\n"
	    "To load a raw binary into memory, add \"address:\" in front "
	    "of the filename,\n"
	    "or \"address:skiplen:\" or \"address:skiplen:initialpc:\".\n"
	    "\nExamples:\n"
	    "    0xbfc00000:rom.bin                    for a raw ROM image\n"
	    "    0xbfc00000:0x100:rom.bin              for an image with "
	    "0x100 bytes header\n"
	    "    0xbfc00000:0x100:0xbfc00884:rom.bin   "
	    "start with pc=0xbfc00884\n\n");
}


/*
 *  get_cmd_args():
 *
 *  Reads command line arguments.
 */
int get_cmd_args(int argc, char *argv[], struct emul *emul,
	char ***diskimagesp, int *n_diskimagesp, char **tap_devname)
{
	int using_switch_e = 0, using_switch_E = 0;
	bool using_switch_Z = false;
	bool using_switch_d = false;
	bool machine_specific_options_used = false;

	int ch, res;

	char *type = NULL, *subtype = NULL;
	struct machine *m = emul_add_machine(emul, NULL);

	const char *opts =
	    "AC:c:Dd:E:e:GHhI:iJj:k:KL:M:Nn:Oo:p:QqRrSs:TtVvW:"
#ifdef WITH_X11
	    "XxY:"
#endif
	    "Z:z:";

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'A':
			enable_colorized_output = false;
			break;
		case 'C':
			CHECK_ALLOCATION(m->cpu_name = strdup(optarg));
			machine_specific_options_used = true;
			break;
		case 'c':
			emul->n_debugger_cmds ++;
			CHECK_ALLOCATION(emul->debugger_cmds = (char **)
			    realloc(emul->debugger_cmds,
			    emul->n_debugger_cmds * sizeof(char *)));
			CHECK_ALLOCATION(emul->debugger_cmds[emul->
			    n_debugger_cmds-1] = strdup(optarg));
			break;
		case 'D':
			skip_srandom_call = true;
			break;
		case 'd':
			/*  diskimage_add() is called further down  */
			(*n_diskimagesp) ++;
			CHECK_ALLOCATION( (*diskimagesp) = (char **)
			    realloc(*diskimagesp,
			    sizeof(char *) * (*n_diskimagesp)) );
			CHECK_ALLOCATION( (*diskimagesp)[(*n_diskimagesp) - 1] =
			    strdup(optarg) );
			using_switch_d = true;
			machine_specific_options_used = true;
			break;
		case 'E':
			if (using_switch_E ++ > 0) {
				fprintf(stderr, "-E already used.\n");
				exit(1);
			}
			type = optarg;
			machine_specific_options_used = true;
			break;
		case 'e':
			if (using_switch_e ++ > 0) {
				fprintf(stderr, "-e already used.\n");
				exit(1);
			}
			subtype = optarg;
			machine_specific_options_used = true;
			break;
		case 'G':
			enable_colorized_output = true;
			break;
		case 'H':
			machine_list_available_types_and_cpus();
			exit(1);
		case 'h':
			usage(true);
			exit(1);
		case 'I':
			m->emulated_hz = atoi(optarg);
			machine_specific_options_used = true;
			break;
		case 'i':
			m->instruction_trace = 1;
			machine_specific_options_used = true;
			break;
		case 'J':
			m->allow_instruction_combinations = 0;
			machine_specific_options_used = true;
			break;
		case 'j':
			CHECK_ALLOCATION(m->boot_kernel_filename = strdup(optarg));
			machine_specific_options_used = true;
			break;
		case 'k':
			dyntrans_cache_size = atoi(optarg) * 1048576;
			if (dyntrans_cache_size < 1) {
				fprintf(stderr, "The dyntrans cache size must"
				    " be at least 1 MB.\n");
				exit(1);
			}
			break;
		case 'K':
			debugger_enter_at_end_of_run = true;
			break;
		case 'L':
			*tap_devname = strdup(optarg);
			break;
		case 'M':
			m->physical_ram_in_mb = atoi(optarg);
			machine_specific_options_used = true;
			break;
		case 'N':
			m->show_nr_of_instructions = 1;
			machine_specific_options_used = true;
			break;
		case 'n':
			m->ncpus = atoi(optarg);
			machine_specific_options_used = true;
			break;
		case 'O':
			m->force_netboot = 1;
			machine_specific_options_used = true;
			break;
		case 'o':
			CHECK_ALLOCATION(m->boot_string_argument = strdup(optarg));
			machine_specific_options_used = true;
			break;
		case 'p':
			machine_add_breakpoint_string(m, optarg);
			machine_specific_options_used = true;
			break;
		case 'Q':
			m->prom_emulation = 0;
			machine_specific_options_used = true;
			break;
		case 'q':
			quiet_mode = 1;
			break;
		case 'R':
			m->use_random_bootstrap_cpu = 1;
			machine_specific_options_used = true;
			break;
		case 'r':
			m->register_dump = 1;
			machine_specific_options_used = true;
			break;
		case 'S':
			m->random_mem_contents = 1;
			machine_specific_options_used = true;
			break;
		case 's':
			machine_statistics_init(m, optarg);
			machine_specific_options_used = true;
			break;
		case 'T':
			m->halt_on_nonexistant_memaccess = 1;
			machine_specific_options_used = true;
			break;
		case 't':
			m->show_trace_tree = 1;
			machine_specific_options_used = true;
			break;
		case 'V':
			single_step = true;
			debugger_enter_at_end_of_run = true;
			break;
		case 'v':
			verbose ++;
			break;
		case 'W':
			internal_w(optarg);
			exit(0);
		case 'X':
			m->x11_md.in_use = 1;
			machine_specific_options_used = true;
			/*  FALL-THROUGH  */
		case 'x':
			console_allow_slaves(1);
			break;
		case 'Y':
			m->x11_md.scaledown = atoi(optarg);
			if (m->x11_md.scaledown < -1) {
				m->x11_md.scaleup = - m->x11_md.scaledown;
				m->x11_md.scaledown = 1;
			}
			if (m->x11_md.scaledown < 1) {
				fprintf(stderr, "Invalid scaledown value.\n");
				exit(1);
			}
			machine_specific_options_used = true;
			break;
		case 'Z':
			m->n_gfx_cards = atoi(optarg);
			using_switch_Z = true;
			machine_specific_options_used = true;
			break;
		case 'z':
			m->x11_md.n_display_names ++;
			CHECK_ALLOCATION(m->x11_md.display_names = (char **) realloc(
			    m->x11_md.display_names,
			    m->x11_md.n_display_names * sizeof(char *)));
			CHECK_ALLOCATION(m->x11_md.display_names[
			    m->x11_md.n_display_names-1] = strdup(optarg));
			machine_specific_options_used = true;
			break;
		default:
			fprintf(stderr, "Run  %s -h  for help on command "
			    "line options.\n", progname);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	extra_argc = argc;
	extra_argv = argv;

	// If -V is used, -q is ignored.
	if (single_step && quiet_mode) {
		fprintf(stderr, "ignoring -q, because -V is used\n");
		quiet_mode = 0;
	}

	if (type != NULL || subtype != NULL) {
		if (type == NULL)
			type = strdup("");
		if (subtype == NULL)
			subtype = strdup("");

		res = machine_name_to_type(type, subtype,
		    &m->machine_type, &m->machine_subtype);
		if (!res)
			exit(1);
	}

	if (m->machine_type == MACHINE_NONE && machine_specific_options_used) {
		fprintf(stderr, "Machine specific options used directly on "
		    "the command line, but no machine\nemulation specified?\n");
		exit(1);
	}


	/*  -i and -r are pretty verbose:  */

	if (m->instruction_trace && !verbose) {
		fprintf(stderr, "Implicitly %sturning on -v, because"
		    " of -i\n", quiet_mode? "turning off -q and " : "");
		verbose = 1;
		quiet_mode = 0;
	}

	if (m->register_dump && !verbose) {
		fprintf(stderr, "Implicitly %sturning on -v, because"
		    " of -r\n", quiet_mode? "turning off -q and " : "");
		verbose = 1;
		quiet_mode = 0;
	}


	/*
	 *  Usually, an executable filename must be supplied.
	 *
	 *  However, it is possible to boot directly from a harddisk image
	 *  file. If no kernel is supplied, but a diskimage is being used,
	 *  then try to boot from disk.
	 */
	if (extra_argc == 0) {
		if (using_switch_d) {
			/*  Booting directly from a disk image...  */
		} else {
			usage(false);
			fprintf(stderr, "\nNo filename given. Aborting.\n");
			exit(1);
		}
	} else if (m->boot_kernel_filename[0] == '\0') {
		/*
		 *  Default boot_kernel_filename is "", which can be overriden
		 *  by the -j command line option.  If it is still "" here,
		 *  and we're not booting directly from a disk image, then
		 *  try to set it to the last part of the last file name
		 *  given on the command line. (Last part = the stuff after
		 *  the last slash.)
		 */
		char *s = extra_argv[extra_argc - 1];
		char *s2;

		s2 = strrchr(s, '/');
		if (s2 == NULL)
			s2 = s;
		else
			s2 ++;

		CHECK_ALLOCATION(m->boot_kernel_filename = strdup(s2));
	}

	if (m->n_gfx_cards < 0 || m->n_gfx_cards > 3) {
		fprintf(stderr, "Bad number of gfx cards (-Z).\n");
		exit(1);
	}

	if (!using_switch_Z && !m->x11_md.in_use)
		m->n_gfx_cards = 0;

	return 0;
}


/*
 *  main():
 *
 *  Two kinds of emulations are started from here:
 *
 *	o)  Simple emulations, using command line arguments.
 *	o)  Emulations set up by parsing special config files. (0 or more.)
 */
int main(int argc, char *argv[])
{
	/*  Setting constants:  */
	int constant_yes = 1;
	int constant_true = 1;
	int constant_no = 0;
	int constant_false = 0;

	struct emul *emul;
	bool using_config_file = false;
	char *tap_devname = NULL;
	char **diskimages = NULL;
	int n_diskimages = 0;
	int i;


	progname = argv[0];


	enable_colorized_output = getenv("CLICOLOR") != NULL;

	debugmsg_init();


	/*
	 *  Create the settings object, and add global settings to it:
	 *
	 *  Read-only "constants":     yes, no, true, false.
	 *  Global emulator settings:  verbose, single_step, ...
	 */
	global_settings = settings_new();

	settings_add(global_settings, "yes", 0, SETTINGS_TYPE_INT,
	    SETTINGS_FORMAT_YESNO, (void *)&constant_yes);
	settings_add(global_settings, "no", 0, SETTINGS_TYPE_INT,
	    SETTINGS_FORMAT_YESNO, (void *)&constant_no);
	settings_add(global_settings, "true", 0, SETTINGS_TYPE_INT,
	    SETTINGS_FORMAT_BOOL, (void *)&constant_true);
	settings_add(global_settings, "false", 0, SETTINGS_TYPE_INT,
	    SETTINGS_FORMAT_BOOL, (void *)&constant_false);

	/*  Read/write settings:  */
	settings_add(global_settings, "verbose", 1,
	    SETTINGS_TYPE_INT, SETTINGS_FORMAT_YESNO, (void *)&verbose);
	settings_add(global_settings, "quiet_mode", 1,
	    SETTINGS_TYPE_INT, SETTINGS_FORMAT_YESNO, (void *)&quiet_mode);

	/*  Initialize various subsystems:  */
	console_init();
	cpu_init();
	device_init();
	machine_init();
	timer_init();

	/*  Create a simple emulation setup:  */
	emul = emul_new(NULL);
	settings_add(global_settings, "emul", 1,
	    SETTINGS_TYPE_SUBSETTINGS, 0, emul->settings);

	get_cmd_args(argc, argv, emul, &diskimages, &n_diskimages, &tap_devname);

	if (!skip_srandom_call) {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		srandom(tv.tv_sec ^ getpid() ^ tv.tv_usec);
	}

	// -q means that only errors should be shown.
	if (quiet_mode)
		debugmsg_set_verbosity_level(SUBSYS_ALL, VERBOSITY_ERROR);

	for (int k = 0; k < verbose; ++k)
		debugmsg_add_verbosity_level(SUBSYS_ALL, 1);

	if (!quiet_mode)
		print_banner();

	/*  Simple initialization, from command line arguments:  */
	if (emul->machines[0]->machine_type != MACHINE_NONE) {
		for (i=0; i<n_diskimages; i++) {
			if (diskimage_add(emul->machines[0], diskimages[i]) < 0) {
				fprintf(stderr, "Aborting.\n");
				return 1;
			}
		}

		/*  Make sure that there are no configuration files as well:  */
		for (i=1; i<argc; i++)
			if (argv[i][0] == '@') {
				fprintf(stderr, "You can either start one "
				    "emulation with one machine directly from "
				    "the command\nline, or start an "
				    "emulation using a configuration file."
				    " Not both.\n");
				return 1;
			}

		/*  Initialize one emul:  */
		if (!emul_simple_init(emul, tap_devname)) {
			fprintf(stderr, "Could not initialize the emulation.\n");
			return 1;
		}
	}

	/*  Initialize an emulation from a config file:  */
	for (i=1; i<argc; i++) {
		if (argv[i][0] == '@') {
			char *s = argv[i] + 1;

			if (using_config_file) {
				fprintf(stderr, "More than one configuration "
				    "file cannot be used.\n");
				return 1;
			}

			if (strlen(s) == 0 && i+1 < argc && *argv[i+1] != '@')
				s = argv[++i];

			/*  Destroy the temporary emul, since it will
			    be overwritten:  */
			if (emul != NULL) {
				emul_destroy(emul);
				emul = NULL;
			}

			emul = emul_create_from_configfile(s);

			settings_add(global_settings, "emul", 1,
			    SETTINGS_TYPE_SUBSETTINGS, 0, emul->settings);

			using_config_file = true;
		}
	}

	if (emul->n_machines == 0) {
		printf("No machine defined. Maybe you forgot to use ");
		color_prompt();
		printf("-E xx");
		color_normal();
		printf(" and/or ");
		color_prompt();
		printf("-e yy");
		color_normal();
		printf(", to specify\nthe machine type."
		    " For example:\n\n    ");
		color_prompt();
		printf("%s -e 3max -d disk.img", progname);
		color_normal();
		printf("\n\n"
		    "to boot an emulated DECstation 5000/200 with a disk "
		    "image.\n");
		return 1;
	}

	if (emul->machines[0]->machine_type == MACHINE_NONE) {
		printf("No machine type specified?\nRun  ");
		color_prompt();
		printf("gxemul -H");
		color_normal();
		printf("  for a list of available machine types.\n"
		    "Then use the ");
		color_prompt();
		printf("-e");
		color_normal();
		printf(" or ");
		color_prompt();
		printf("-E");
		color_normal();
		printf(" option(s) to specify the machine type.\n");
		return 1;
	}

	device_set_exit_on_error(0);
	console_warn_if_slaves_are_needed(1);

	// Print "INFO" during startup, but then just "WARNINGS" by default.
	debugmsg_add_verbosity_level(SUBSYS_ALL, -1);


	/*  Run the emulation:  */
	emul_run(emul);


	/*
	 *  Deinitialize everything:
	 */

	console_deinit();

	emul_destroy(emul);

	settings_remove_all(global_settings);
	settings_destroy(global_settings);

	return 0;
}

