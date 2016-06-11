/* $Id: mdl.c,v 1.25 2016/06/11 19:20:50 je Exp $ */

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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
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
static int	handle_musicfiles(struct sequencer_process *,
    struct musicfiles *);
static void __dead usage(void);

static int	wait_for_sequencer_event(struct sequencer_process *,
    enum sequencer_event event);

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
	struct sequencer_process seq_proc;
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

	ret = _mdl_start_sequencer_process(&seq_proc, mididev_type,
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

	ret = handle_musicfiles(&seq_proc, &musicfiles);
	if (ret != 0)
		errx(1, "error in handling musicfiles");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (shutdown(seq_proc.socket, SHUT_WR) == -1)
		warn("error shutting down sequencer connection");

	if (_mdl_wait_for_subprocess("sequencer", seq_proc.pid) != 0)
		errx(1, "error when waiting for sequencer subprocess");

	/* XXX read all from sequencer.socket? */

	if (close(seq_proc.socket) == -1)
		warn("error closing sequencer connection");

	free(musicfiles.files);

	_mdl_logging_close();

	return 0;
}

static int
open_musicfiles(char **musicfilepaths, size_t musicfilecount,
    struct musicfiles *musicfiles)
{
	char *stdinfiles[] = { "-" };
	int file_fd;
	size_t i;
	struct musicfiles tmp_musicfiles;

	if (musicfilecount == 0) {
		musicfilecount = 1;
		musicfilepaths = stdinfiles;
	}

	tmp_musicfiles.files = calloc(musicfilecount,
	    sizeof(struct musicfile));
	if (tmp_musicfiles.files == NULL) {
		warn("calloc");
		return 1;
	}

	tmp_musicfiles.count = 0;
	for (i = 0; i < musicfilecount; i++) {
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
		tmp_musicfiles.files[ tmp_musicfiles.count ].path =
		    musicfilepaths[i];

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
handle_musicfiles(struct sequencer_process *seq_proc,
    struct musicfiles *musicfiles)
{
	struct interpreter_process interp;
	int fd, ret, retvalue, sequencer_is_playing;
	char *curr_path, *prev_path;
	size_t i;

	retvalue = 0;
	sequencer_is_playing = 0;

	curr_path = NULL;
	prev_path = NULL;

	/* XXX how to check for !mdl_shutdown_main? */

	/* XXX This currently plays only the first one... */
	for (i = 0; i < musicfiles->count; i++) {
		curr_path = musicfiles->files[i].path;
		fd        = musicfiles->files[i].fd;

		ret = _mdl_start_interpreter_process(&interp, fd,
		    seq_proc->socket);
		if (ret != 0) {
			warnx("could not start interpreter process");
			retvalue = 1;
			break;
		}

		if (sequencer_is_playing) {
			/* XXX Wait for sequencer playback to finish, do this
			 * XXX right. */
			_mdl_log(MDLLOG_SONG, 0,
			    "waiting for SEQEVENT_SONG_END\n");
			ret = wait_for_sequencer_event(seq_proc,
			    SEQEVENT_SONG_END);
			if (ret != 0) {
				retvalue = 1;
				break;
			}

			_mdl_log(MDLLOG_SONG, 0, "finished playing %s\n",
			    prev_path);
		}

		_mdl_log(MDLLOG_SONG, 0, "starting to play %s\n", curr_path);

		ret = _mdl_send_event_to_sequencer(seq_proc,
		    SERVEREVENT_NEW_SONG, interp.sequencer_read_pipe, "", 0);
		if (ret != 0) {
			warnx("could not request new song from sequencer");
			retvalue = 1;
		}

		sequencer_is_playing = 1;

		ret = _mdl_wait_for_subprocess("interpreter", interp.pid);
		if (ret != 0) {
			warnx("error in interpreter subprocess");
			retvalue = 1;
		}

		if (fd != fileno(stdin) && close(fd) == -1)
			warn("error closing %s", curr_path);

		if (retvalue != 0)
			break;

		prev_path = curr_path;
	}

	if (retvalue != 0) {
		assert(curr_path != NULL);
		warnx("error in handling %s", curr_path);
	}

	if (wait_for_sequencer_event(seq_proc, SEQEVENT_SONG_END) == -1) {
		warnx("error in waiting for sequencer event");
		retvalue = 1;
	}

	if (prev_path != NULL)
		_mdl_log(MDLLOG_SONG, 0, "finished playing %s\n", prev_path);

	return retvalue;
}

static int
wait_for_sequencer_event(struct sequencer_process *seq_proc,
    enum sequencer_event event)
{
	struct imsg imsg;
	ssize_t nr, datalen;
	int found;

	found = 0;

	while (!found) {
		nr = imsg_read(&seq_proc->ibuf);
		if (nr == -1) {
			if (errno == EAGAIN)
				continue;
			warnx("error in wait_for_sequencer_event/imsg_read");
			return 1;
		}
		if (nr == 0) {
			warnx("did not receive the expected event");
			return 1;
		}

		nr = imsg_get(&seq_proc->ibuf, &imsg);
		if (nr == -1) {
			warnx("error in wait_for_sequencer_event/imsg_get");
			return 1;
		}
		if (nr == 0)
			continue;

		datalen = imsg.hdr.len - IMSG_HEADER_SIZE;

		if (imsg.hdr.type == event)
			found = 1;

		imsg_free(&imsg);
	}

	return 0;
}
