/*
 * signal.c
 *
 * Copyright (C) 2010-2014 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "fault.h"


//==========================================================
// Constants.
//

// String constants in version.c, generated by make.
extern const char aerospike_build_type[];
extern const char aerospike_build_id[];
extern const char aerospike_build_os[];


//==========================================================
// Globals.
//

// The mutex that the main function deadlocks on after starting the service.
extern pthread_mutex_t g_NONSTOP;
extern bool g_startup_complete;


//==========================================================
// Local helpers.
//

static inline void
register_signal_handler(int sig_num, sighandler_t handler)
{
	sighandler_t old_handler = signal(sig_num, handler);

	if (old_handler == SIG_ERR) {
		cf_crash(AS_AS, "could not register signal handler for %d", sig_num);
	}
	else if (old_handler) {
		// Occasionally we've seen the value 1 returned, but otherwise the
		// registration of the handler seems to be fine, so proceed...
		cf_warning(AS_AS, "found unexpected old signal handler %p for %d",
				old_handler, sig_num);
	}
}

static inline void
reraise_signal(int sig_num, sighandler_t handler)
{
	if (signal(sig_num, SIG_DFL) != handler) {
		cf_warning(AS_AS, "could not register default signal handler for %d",
				sig_num);
		_exit(-1);
	}

	raise(sig_num);
}


//==========================================================
// Signal handlers.
//

// We get here on cf_crash(), cf_assert(), as well as on some crashes.
void
as_sig_handle_abort(int sig_num)
{
	cf_warning(AS_AS, "SIGABRT received, aborting %s build %s os %s",
			aerospike_build_type, aerospike_build_id, aerospike_build_os);

	PRINT_STACK();
	reraise_signal(sig_num, as_sig_handle_abort);
}

// Floating point exception.
void
as_sig_handle_fpe(int sig_num)
{
	cf_warning(AS_AS, "SIGFPE received, aborting %s build %s os %s",
			aerospike_build_type, aerospike_build_id, aerospike_build_os);

	PRINT_STACK();
	reraise_signal(sig_num, as_sig_handle_fpe);
}

// This signal is our cue to roll the log.
void
as_sig_handle_hup(int sig_num)
{
	cf_info(AS_AS, "SIGHUP received, rolling log");

	cf_fault_sink_logroll();
}

// We get here on some crashes.
void
as_sig_handle_ill(int sig_num)
{
	cf_warning(AS_AS, "SIGILL received, aborting %s build %s os %s",
			aerospike_build_type, aerospike_build_id, aerospike_build_os);

	PRINT_STACK();
	reraise_signal(sig_num, as_sig_handle_ill);
}

// We get here on cf_crash_nostack(), cf_assert_nostack().
void
as_sig_handle_int(int sig_num)
{
	cf_warning(AS_AS, "SIGINT received, shutting down");

	if (! g_startup_complete) {
		cf_warning(AS_AS, "startup was not complete, exiting immediately");
		_exit(0);
	}

	pthread_mutex_unlock(&g_NONSTOP);
}

// We get here if we intentionally trigger the signal.
void
as_sig_handle_quit(int sig_num)
{
	cf_warning(AS_AS, "SIGQUIT received, aborting %s build %s os %s",
			aerospike_build_type, aerospike_build_id, aerospike_build_os);

	PRINT_STACK();
	reraise_signal(sig_num, as_sig_handle_quit);
}

// We get here on some crashes.
void
as_sig_handle_segv(int sig_num)
{
	cf_warning(AS_AS, "SIGSEGV received, aborting %s build %s os %s",
			aerospike_build_type, aerospike_build_id, aerospike_build_os);

	PRINT_STACK();
	reraise_signal(sig_num, as_sig_handle_segv);
}

// We get here on normal shutdown.
void
as_sig_handle_term(int sig_num)
{
	cf_warning(AS_AS, "SIGTERM received, shutting down");

	if (! g_startup_complete) {
		cf_warning(AS_AS, "startup was not complete, exiting immediately");
		_exit(0);
	}

	pthread_mutex_unlock(&g_NONSTOP);
}


//==========================================================
// Public API.
//

void
as_signal_setup()
{
	register_signal_handler(SIGABRT, as_sig_handle_abort);
	register_signal_handler(SIGFPE, as_sig_handle_fpe);
	register_signal_handler(SIGHUP, as_sig_handle_hup);
	register_signal_handler(SIGILL, as_sig_handle_ill);
	register_signal_handler(SIGINT, as_sig_handle_int);
	register_signal_handler(SIGQUIT, as_sig_handle_quit);
	register_signal_handler(SIGSEGV, as_sig_handle_segv);
	register_signal_handler(SIGTERM, as_sig_handle_term);

	// Block SIGPIPE signal when there is some error while writing to pipe. The
	// write() call will return with a normal error which we can handle.
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = SIG_IGN;
	sigemptyset(&sigact.sa_mask);
	sigaddset(&sigact.sa_mask, SIGPIPE);

	if (sigaction(SIGPIPE, &sigact, NULL) != 0) {
		cf_warning(AS_AS, "could not block the SIGPIPE signal");
	}
}
