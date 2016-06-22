/* $Id: mdl.c,v 1.35 2016/06/22 20:39:54 je Exp $ */

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
#include "ipc.h"
#include "midi.h"
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

struct server_connection {
	int		socket;
	struct imsgbuf	ibuf;
};

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdl_shutdown_main = 0;

extern int loglevel;

char *_mdl_process_type;

static void	handle_signal(int);
static int	establish_sequencer_connection(struct server_connection *,
    struct sequencer_connection *);
static int	establish_server_connection(struct server_connection *, int);
static int	get_interp_pipe(struct server_connection *);
static int	open_musicfiles(struct musicfiles *, char **, size_t);
static int	handle_musicfiles(struct server_connection *,
    struct sequencer_connection *, struct musicfiles *);
static int	replace_server_with_client_conn(struct sequencer_connection *);
static void __dead usage(void);

static int	wait_for_sequencer_event(struct sequencer_connection *,
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
	struct sequencer_connection seq_conn;
	struct server_connection server_conn;
	pid_t sequencer_pid;
	char *devicepath;
	char **musicfilepaths;
	struct musicfiles musicfiles;
	int cflag, nflag, sflag;
	int ch, connect_to_server, force_server_connection;
	int musicfilecount, ret, sequencer_connection_established;
	int server_connection_established;
	enum mididev_type mididev_type;

#ifdef HAVE_MALLOC_OPTIONS
	malloc_options = (char *) "AFGJPS";
#endif /* HAVE_MALLOC_OPTIONS */

	_mdl_process_type = "main";

	cflag = 0;
	nflag = 0;
	sflag = 0;

	connect_to_server = 1;
	force_server_connection = 0;

	devicepath = NULL;
	mididev_type = DEFAULT_MIDIDEV_TYPE;
	sequencer_pid = 0;

	/* Use all pledge promises needed by sndio (except for "audio" which
	 * I think is sio_* specific), plus "proc", "recvfd" and "sendfd". */
	ret = pledge("cpath dns inet proc recvfd rpath sendfd stdio unix"
	    " wpath", NULL);
	if (ret == -1)
		err(1, "pledge");

	signal(SIGINT,  handle_signal);
	signal(SIGTERM, handle_signal);

	_mdl_logging_init();

	while ((ch = getopt(argc, argv, "cd:f:m:nsv")) != -1) {
		switch (ch) {
		case 'c':
			cflag = 1;
			break;
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
			sflag = 1;	/* -n implies -s */
			break;
		case 's':
			sflag = 1;
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

	if (cflag && sflag)
		errx(1, "-c and -s options are mutually exclusive");
	if (cflag)
		force_server_connection = 1;
	if (sflag)
		connect_to_server = 0;

	_mdl_log(MDLLOG_PROCESS, 0, "new main process, pid %d\n", getpid());

	musicfilecount = argc;
	musicfilepaths = argv;

	sequencer_connection_established = 0;
	server_connection_established = 0;
	if (connect_to_server) {
		ret = establish_server_connection(&server_conn,
		    force_server_connection);
		if (ret == 0) {
			ret = establish_sequencer_connection(&server_conn,
			    &seq_conn);
			if (ret != 0) {
				imsg_clear(&server_conn.ibuf);
				if (close(server_conn.socket) == -1)
					warn("closing server connection");
			} else {
				sequencer_connection_established = 1;
				server_connection_established = 1;
			}
		}
	}

	if (!sequencer_connection_established) {
		if (force_server_connection)
			errx(1, "forced a server connection, but it failed");
		ret = _mdl_start_sequencer_process(&sequencer_pid, &seq_conn,
		    mididev_type, devicepath, nflag);
		if (ret != 0)
			errx(1, "error in starting up sequencer");
		if (replace_server_with_client_conn(&seq_conn) != 0)
			errx(1, "error in setting up sequencer client"
			    " connection");
	}

	/* Now that sequencer has been forked, we can drop all sndio related
	 * pledges, plus "recvfd" only used by sequencer. */
	if (pledge("proc rpath sendfd stdio", NULL) == -1)
		err(1, "pledge");

	ret = open_musicfiles(&musicfiles, musicfilepaths, musicfilecount);
	if (ret != 0)
		errx(1, "error in opening musicfiles");

	/* Music files have been opened, we can drop "rpath" pledge. */
	if (pledge("proc sendfd stdio", NULL) == -1)
		err(1, "pledge");

	ret = handle_musicfiles(
	    (server_connection_established ? &server_conn : NULL),
	    &seq_conn, &musicfiles);
	if (ret != 0)
		errx(1, "error in handling musicfiles");

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (sequencer_pid != 0) {
		ret = _mdl_disconnect_sequencer_process(sequencer_pid,
		    &seq_conn);
		if (ret != 0)
			errx(1, "error when disconnecting sequencer"
			    " subprocess");
	}

	free(musicfiles.files);

	_mdl_logging_close();

	return 0;
}

static int
establish_sequencer_connection(struct server_connection *server_conn,
    struct sequencer_connection *seq_conn)
{
	struct imsg imsg;
	ssize_t nr;
	int seq_socket;

	seq_socket = -1;

	if ((nr = imsg_read(&server_conn->ibuf)) == -1 || nr == 0) {
		warnx("error in reading from server / imsg_read");
		return 1;
	}


	if ((nr == imsg_get(&server_conn->ibuf, &imsg)) == -1 || nr == 0) {
		warnx("error in reading from server / imsg_get");
		return 1;
	}

	if (imsg.hdr.type != SERVEREVENT_NEW_CLIENT) {
		warnx("received an unexpected event from server");
		imsg_free(&imsg);
		return 1;
	}

	if (imsg.fd == -1) {
		warnx("did not receive a sequencer socket");
		imsg_free(&imsg);
		return 1;
	}

	_mdl_log(MDLLOG_IPC, 0, "received a sequencer socket\n");

	seq_conn->socket = imsg.fd;
	imsg_free(&imsg);

	imsg_init(&seq_conn->ibuf, seq_conn->socket);

	return 0;
}

static int
establish_server_connection(struct server_connection *server_conn,
    int force_server_connection)
{
	struct sockaddr_un sun;
	const char *socketpath;
	int client_socket, ret;

	if ((socketpath = _mdl_get_socketpath()) == NULL) {
		if (force_server_connection)
			warnx("could not determine mdl socketpath");
		return 1;
	}

	if ((client_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		if (force_server_connection)
			warn("could not create a client socket");
		return 1;
	}

	memset(&sun, 0, sizeof(struct sockaddr_un));
	sun.sun_family = AF_UNIX;
	ret = strlcpy(sun.sun_path, socketpath, MDL_SOCKETPATH_LEN);
	assert(ret < MDL_SOCKETPATH_LEN);
	ret = connect(client_socket, (struct sockaddr *)&sun, SUN_LEN(&sun));
	if (ret == -1) {
		if (force_server_connection)
			warn("could not connect to server");
		if (close(client_socket) == -1 && force_server_connection)
			warn("closing client socket");
		return 1;
	}

	_mdl_log(MDLLOG_IPC, 0, "connected to server\n");

	server_conn->socket = client_socket;
	imsg_init(&server_conn->ibuf, server_conn->socket);

	return 0;
}

static int
open_musicfiles(struct musicfiles *musicfiles, char **musicfilepaths,
    size_t musicfilecount)
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
handle_musicfiles(struct server_connection *server_conn,
    struct sequencer_connection *seq_conn, struct musicfiles *musicfiles)
{
	struct interpreter_process interp;
	char *curr_path, *prev_path;
	size_t i;
	int fd, interp_pipe, ret, retvalue;
	int sequencer_is_playing;

	retvalue = 0;
	sequencer_is_playing = 0;

	curr_path = NULL;
	prev_path = NULL;

	/* XXX how to check for !mdl_shutdown_main? */

	for (i = 0; i < musicfiles->count; i++) {
		curr_path = musicfiles->files[i].path;
		fd        = musicfiles->files[i].fd;

		if (server_conn != NULL) {
			ret = imsg_compose(&server_conn->ibuf,
			    CLIENTEVENT_NEW_MUSICFD, 0, 0, fd, "", 0);
			if (ret == -1 ||
			    imsg_flush(&server_conn->ibuf) == -1) {
				warnx("error sending a music file descriptor"
				    " to server");
				retvalue = 1;
				break;
			} else {
				/* Successful send closes this. */
				musicfiles->files[i].fd = -1;
			}

			interp_pipe = get_interp_pipe(server_conn);
			if (interp_pipe == -1) {
				warnx("error in getting an interpreter pipe"
				    " from server");
				retvalue = 1;
				break;
			}
		} else {
			ret = _mdl_start_interpreter_process(&interp, fd,
			    seq_conn->socket);
			if (ret != 0) {
				warnx("could not start interpreter process");
				retvalue = 1;
				break;
			}
			interp_pipe = interp.sequencer_read_pipe;
		}

		if (sequencer_is_playing) {
			_mdl_log(MDLLOG_IPC, 0,
			    "waiting for SEQEVENT_SONG_END\n");
			ret = wait_for_sequencer_event(seq_conn,
			    SEQEVENT_SONG_END);
			if (ret != 0) {
				retvalue = 1;
				break;
			}

			_mdl_log(MDLLOG_SONG, 0, "finished playing %s\n",
			    prev_path);
		}

		_mdl_log(MDLLOG_SONG, 0, "starting to play %s\n", curr_path);

		ret = imsg_compose(&seq_conn->ibuf, CLIENTEVENT_NEW_SONG, 0, 0,
		    interp_pipe, "", 0);
		if (ret == -1 || imsg_flush(&seq_conn->ibuf) == -1) {
			warnx("could not request new song from sequencer");
			retvalue = 1;
		}

		sequencer_is_playing = 1;

		if (server_conn == NULL) {
			ret = _mdl_wait_for_subprocess("interpreter",
			    interp.pid);
			if (ret != 0) {
				warnx("error in interpreter subprocess");
				retvalue = 1;
			}
		}

		if (retvalue != 0)
			break;

		prev_path = curr_path;
	}

	if (retvalue != 0) {
		assert(curr_path != NULL);
		warnx("error in handling %s", curr_path);
	}

	if (wait_for_sequencer_event(seq_conn, SEQEVENT_SONG_END) == -1) {
		warnx("error in waiting for sequencer event");
		retvalue = 1;
	}

	if (prev_path != NULL)
		_mdl_log(MDLLOG_SONG, 0, "finished playing %s\n", prev_path);

	for (i = 0; i < musicfiles->count; i++) {
		fd = musicfiles->files[i].fd;
		if (fd == -1)
			continue;
		if (fd != fileno(stdin) && close(fd) == -1)
			warn("error closing %s", musicfiles->files[i].path);
	}

	return retvalue;
}

static int
wait_for_sequencer_event(struct sequencer_connection *seq_conn,
    enum sequencer_event event)
{
	struct imsg imsg;
	ssize_t nr, datalen;
	int found;

	found = 0;

	while (!found) {
		nr = imsg_read(&seq_conn->ibuf);
		if (nr == -1) {
			warnx("error in wait_for_sequencer_event/imsg_read");
			return 1;
		}
		if (nr == 0) {
			warnx("did not receive the expected event");
			return 1;
		}

		nr = imsg_get(&seq_conn->ibuf, &imsg);
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

static int
get_interp_pipe(struct server_connection *server_conn)
{
	struct imsg imsg;
	ssize_t nr;

	if ((nr = imsg_read(&server_conn->ibuf)) == -1 || nr == 0) {
		warnx("error reading interpreter pipe / imsg_read");
		return 1;
	}


	if ((nr = imsg_get(&server_conn->ibuf, &imsg)) == -1 || nr == 0) {
		warnx("error reading interpreter pipe / imsg_get");
		return 1;
	}

	if (imsg.hdr.type != SERVEREVENT_NEW_INTERPRETER) {
		warnx("received an unexpected event from server");
		imsg_free(&imsg);
		return 1;
	}

	if (imsg.fd == -1) {
		warnx("did not receive an interpreter socket");
		imsg_free(&imsg);
		return 1;
	}

	imsg_free(&imsg);

	return imsg.fd;
}

static int
replace_server_with_client_conn(struct sequencer_connection *seq_conn)
{
	int cs_sp[2];
	int ret;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, cs_sp) == -1) {
		warn("could not setup socketpair for client <-> sequencer");
		return 1;
	}

	ret = imsg_compose(&seq_conn->ibuf, SERVEREVENT_NEW_CLIENT, 0, 0,
	    cs_sp[0], "", 0);
	if (ret == -1 || imsg_flush(&seq_conn->ibuf) == -1) {
		warnx("sending new client event to sequencer");
		if (close(cs_sp[0]) == -1)
			warn("closing first end of cs_sp");
		if (close(cs_sp[1]) == -1)
			warn("closing second end of cs_sp");
		return 1;
	}

	imsg_clear(&seq_conn->ibuf);
	if (close(seq_conn->socket) == -1)
		warn("error closing server <-> sequencer socket");

	seq_conn->socket = cs_sp[1];
	imsg_init(&seq_conn->ibuf, seq_conn->socket);

	return 0;
}
