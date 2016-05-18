/* $Id: mdl.c,v 1.11 2016/05/18 20:29:15 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interpreter.h"
#include "midi.h"
#include "protocol.h"
#include "sequencer.h"
#include "util.h"

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

static void	handle_signal(int);
static int	handle_musicfiles(char **, int, int);
static void __dead usage(void);

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdl_shutdown_main = 0;

extern int loglevel;

char *_mdl_process_type;

static void __dead
usage(void)
{
	(void) fprintf(stderr, "usage: mdl [-nv] [-d debuglevel] [-f device]"
	    " [-m MIDI-interface] [file ...]\n");
	exit(1);
}

static void
handle_signal(int signo)
{
	assert(signo == SIGINT || signo == SIGTERM);

	if (signo == SIGINT || signo == SIGTERM)
		mdl_shutdown_main = 1;
}

int
main(int argc, char *argv[])
{
	struct sequencer_process sequencer;
	char *devicepath;
	char **musicfiles;
	int musicfilecount, ch, nflag;
	size_t ret;
	enum mididev_type mididev_type;

#ifdef HAVE_MALLOC_OPTIONS
	malloc_options = (char *) "AFGJPS";
#endif /* HAVE_MALLOC_OPTIONS */

	_mdl_process_type = "main";

	devicepath = NULL;
	nflag = 0;
	mididev_type = DEFAULT_MIDIDEV_TYPE;

	if (pledge("proc recvfd rpath sendfd stdio unix wpath", NULL) == -1)
		err(1, "pledge");

	signal(SIGINT,  handle_signal);
	signal(SIGTERM, handle_signal);

	_mdl_logging_init();

	while ((ch = getopt(argc, argv, "d:f:m:nv")) != -1) {
		switch (ch) {
		case 'd':
			if (_mdl_logging_setopts(optarg) == -1)
				errx(1, "error in setting logging opts");
			break;
		case 'f':
			devicepath = optarg;
			break;
		case 'm':
			mididev_type = _mdl_midi_get_mididev_type(optarg);
			if (mididev_type == MIDIDEV_NONE)
				exit(1);
			break;
		case 'n':
			nflag = 1;
			break;
		case 'v':
			if (_mdl_show_version() != 0)
				exit(1);
			exit(0);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	_mdl_log(MDLLOG_PROCESS, 0, "new main process, pid %d\n", getpid());

	musicfilecount = argc;
	musicfiles = argv;

	ret = _mdl_start_sequencer_process(&sequencer, mididev_type,
	    devicepath, nflag);
	if (ret != 0)
		errx(1, "error in starting up sequencer");

	/* Now that sequencer has been forked, we can drop "wpath" pledge. */
	if (pledge("proc recvfd rpath sendfd stdio unix", NULL) == -1)
		err(1, "pledge");

	ret = handle_musicfiles(musicfiles, musicfilecount, sequencer.socket);
	if (ret != 0)
		errx(1, "error in handling musicfiles");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (close(sequencer.socket) == -1)
		warn("error closing sequencer connection");

	if (_mdl_wait_for_subprocess("sequencer", sequencer.pid) != 0)
		errx(1, "error when waiting for sequencer subprocess");

	_mdl_logging_close();

	return 0;
}

static int
handle_musicfiles(char **files, int filecount, int sequencer_fd)
{
	int file_fd, i, ret, using_stdin;
	char *stdinfiles[] = { "-" };

	if (filecount == 0) {
		filecount = 1;
		files = stdinfiles;
	}

	/* XXX We could also open all files immediately so we could then
	 * XXX drop the rpath pledge? */
	for (i = 0; i < filecount && mdl_shutdown_main == 0; i++) {
		if (strcmp(files[i], "-") == 0) {
			file_fd = fileno(stdin);
			using_stdin = 1;
		} else {
			using_stdin = 0;
			file_fd = open(files[i], O_RDONLY);
			if (file_fd == -1) {
				warn("could not open %s", files[i]);
				continue;
			}
		}

		ret = _mdl_start_interpreter(file_fd, sequencer_fd);
		if (ret != 0) {
			warnx("error in handling %s",
			    (using_stdin ? "stdin" : files[i]));
			if (close(file_fd) == -1)
				warn("error closing %s", files[i]);
			return 1;
		}

		if (file_fd != fileno(stdin) && close(file_fd) == -1)
			warn("error closing %s", files[i]);
	}

	return 0;
}
