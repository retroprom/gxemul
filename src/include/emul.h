#ifndef	EMUL_H
#define	EMUL_H

/*
 *  Copyright (C) 2004-2005  Anders Gavare.  All rights reserved.
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
 *  $Id: emul.h,v 1.35 2005-02-26 12:35:48 debug Exp $
 */

#include "misc.h"

struct machine;
struct net;

struct emul {
	struct net	*net;

	char		*name;

	int		n_machines;
	struct machine	**machines;
};


/*  emul.c:  */
struct emul *emul_new(char *name);
struct machine *emul_add_machine(struct emul *e, char *name);
void emul_machine_setup(struct machine *machine, int n_load, char **load_names,
	int n_devices, char **device_names);
void emul_dumpinfo(struct emul *e);
void emul_simple_init(struct emul *emul);
struct emul *emul_create_from_configfile(char *fname);
void emul_run(struct emul **emuls, int n_emuls);


/*  emul_parse.c:  */
void emul_parse_config(struct emul *e, FILE *f);


#endif	/*  EMUL_H  */
