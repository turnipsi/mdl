/* $Id: mdl.c,v 1.14 2016/05/21 19:28:44 je Exp $ */

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

#include <sys/resource.h>
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

#define MAX_MUSICFILES 65536

struct musicfile {
	char   *path;
	int	fd;
};

struct musicfiles {
	struct musicfile       *files;
	size_t			count;
};

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdl_shutdown_main = 0;

extern int loglevel;

char *_mdl_process_type;

static void	handle_signal(int);
static int	open_musicfiles(char **, size_t, struct musicfiles *);
static int	handle_musicfiles(struct musicfiles *, int);
static void __dead usage(void);

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
	char **musicfilepaths;
	struct musicfiles musicfiles;
	int ch, musicfilecount, nflag, ret;
	enum mididev_type mididev_type;

#ifdef HAVE_MALLOC_OPTIONS
	malloc_options = (char *) "AFGJPS";
#endif /* HAVE_MALLOC_OPTIONS */

	_mdl_process_type = "main";

	devicepath = NULL;
	nflag = 0;
	mididev_type = DEFAULT_MIDIDEV_TYPE;

	/* Use all pledge promises needed by sndio (except for "audio" which
	 * I think is sio_* specific), plus "proc", "recvfd" and "sendfd". */
	ret = pledge("cpath dns inet proc recvfd rpath sendfd stdio unix"
	    " wpath", NULL);
	if (ret == -1)
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
	musicfilepaths = argv;

	ret = _mdl_start_sequencer_process(&sequencer, mididev_type,
	    devicepath, nflag);
	if (ret != 0)
		errx(1, "error in starting up sequencer");

	/* Now that sequencer has been forked, we can drop all sndio related
	 * pledges, plus "recvfd" only used by sequencer. */
	if (pledge("proc rpath sendfd stdio", NULL) == -1)
		err(1, "pledge");

	ret = open_musicfiles(musicfilepaths, musicfilecount, &musicfiles);
	if (ret != 0)
		errx(1, "error in opening musicfiles");

	/* Music files have been opened, we can drop "rpath" pledge. */
	if (pledge("proc sendfd stdio", NULL) == -1)
		err(1, "pledge");

	ret = handle_musicfiles(&musicfiles, sequencer.socket);
	if (ret != 0)
		errx(1, "error in handling musicfiles");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (close(sequencer.socket) == -1)
		warn("error closing sequencer connection");

	if (_mdl_wait_for_subprocess("sequencer", sequencer.pid) != 0)
		errx(1, "error when waiting for sequencer subprocess");

	free(musicfiles.files);

	_mdl_logging_close();

	return 0;
}

static int
open_musicfiles(char **musicfilepaths, size_t musicfilecount,
    struct musicfiles *musicfiles)
{
	struct rlimit limit;
	char *stdinfiles[] = { "-" };
	int file_fd, ret;
	size_t i;
	rlim_t fd_limit;
	struct musicfiles tmp_musicfiles;

	ret = getrlimit(RLIMIT_NOFILE, &limit);
	assert(ret != -1);

	fd_limit = (int) MIN(limit.rlim_cur, MAX_MUSICFILES);

	if (musicfilecount > fd_limit) {
		warnx("cannot handle as many as %ld music files",
		    musicfilecount);
		return 1;
	}

	tmp_musicfiles.files = calloc(fd_limit, sizeof(struct musicfile));
	if (tmp_musicfiles.files == NULL) {
		warn("calloc");
		return 1;
	}

	if (musicfilecount == 0) {
		musicfilecount = 1;
		musicfilepaths = stdinfiles;
	}

	tmp_musicfiles.count = 0;
	for (i = 0; i < musicfilecount; i++) {
		assert(tmp_musicfiles.count < fd_limit);

		if (strcmp(musicfilepaths[i], "-") == 0) {
			file_fd = fileno(stdin);
		} else {
			file_fd = open(musicfilepaths[i], O_RDONLY);
			if (file_fd == -1) {
				warn("could not open %s", musicfilepaths[i]);
				goto error;
			}
		}

		tmp_musicfiles.files[ tmp_musicfiles.count ].fd = file_fd;
		tmp_musicfiles.files[ tmp_musicfiles.count ].path
		    = musicfilepaths[i];

		tmp_musicfiles.count += 1;
	}

	musicfiles->count = tmp_musicfiles.count;
	musicfiles->files = tmp_musicfiles.files;

	return 0;

error:
	for (i = 0; i < tmp_musicfiles.count; i++) {
		file_fd = tmp_musicfiles.files[i].fd;
		if (file_fd != fileno(stdin) && close(file_fd) == -1) {
			warn("closing musicfile %s",
			    tmp_musicfiles.files[i].path);
		}
	}

	return 1;
}

static int
handle_musicfiles(struct musicfiles *musicfiles, int sequencer_fd)
{
	int exitstatus, fd, ret;
	char *path;
	size_t i;

	exitstatus = 0;

	/* XXX how to check for !mdl_shutdown_main? */

	for (i = 0; i < musicfiles->count; i++) {
		fd   = musicfiles->files[i].fd;
		path = musicfiles->files[i].path;

		ret = _mdl_eval_in_interpreter(fd, sequencer_fd);
		if (ret != 0) {
			warnx("error in handling %s", path);
			exitstatus = 1;
		}

		if (fd != fileno(stdin) && close(fd) == -1)
			warn("error closing %s", path);

		if (exitstatus != 0)
			break;
	}

	return exitstatus;
}
