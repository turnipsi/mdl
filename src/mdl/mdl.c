/* $Id: mdl.c,v 1.49 2016/07/17 20:04:29 je Exp $ */

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
#include <sys/wait.h>

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
	size_t			current;
	int			all_done;
};

struct server_connection {
	int		pending_writes;
	int		socket;
	struct imsgbuf	ibuf;
};

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdl_shutdown_client = 0;

extern int loglevel;

char *_mdl_process_type;

static void	handle_signal(int);
static int	establish_sequencer_connection(struct server_connection *,
    struct sequencer_connection *);
static int	establish_server_connection(struct server_connection *, int);
static int	enqueue_song(struct server_connection *,
    struct musicfiles *, struct interpreter_handler *, int);
static int	open_musicfiles(struct musicfiles *, char **, size_t);
static int	handle_interpreter_process(struct interpreter_handler *,
    struct sequencer_connection *, struct musicfiles *);
static int	handle_musicfiles(struct server_connection *,
    struct sequencer_connection *, struct musicfiles *);
static int	handle_sequencer_events(struct server_connection *,
    struct sequencer_connection *, struct musicfiles *,
    struct interpreter_handler *);
static int	handle_server_events(struct server_connection *,
    struct sequencer_connection *);
static int	replace_server_with_client_conn(struct sequencer_connection *);
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
		mdl_shutdown_client = 1;
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

	/*
	 * Now that sequencer has been forked, we can drop all sndio related
	 * pledges, plus "recvfd" only used by sequencer.
	 */
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

	if ((nr = imsg_read(&server_conn->ibuf)) == -1 || nr == 0) {
		warnx("error in reading from server / imsg_read");
		return 1;
	}


	if ((nr = imsg_get(&server_conn->ibuf, &imsg)) == -1 || nr == 0) {
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

	seq_conn->pending_writes = 0;
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

	server_conn->pending_writes = 0;
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
			file_fd = dup(fileno(stdin));
		} else {
			file_fd = open(musicfilepaths[i], O_RDONLY);
		}
		if (file_fd == -1) {
			warn("could not open %s", musicfilepaths[i]);
			goto error;
		}

		tmp_musicfiles.files[ tmp_musicfiles.count ].fd = file_fd;
		tmp_musicfiles.files[ tmp_musicfiles.count ].path =
		    musicfilepaths[i];

		tmp_musicfiles.count += 1;
	}

	musicfiles->all_done = 0;
	musicfiles->count    = tmp_musicfiles.count;
	musicfiles->current  = 0;
	musicfiles->files    = tmp_musicfiles.files;

	return 0;

error:
	for (i = 0; i < tmp_musicfiles.count; i++) {
		file_fd = tmp_musicfiles.files[i].fd;
		if (close(file_fd) == -1) {
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
	struct interpreter_handler interp;
	fd_set readfds, writefds;
	sigset_t loop_sigmask, select_sigmask;
	size_t i;
	int fd, ret, retvalue, status;

	assert(musicfiles->count >= 1);
	assert(musicfiles->files != NULL);

	retvalue = 0;

	interp.client_conn = NULL;
	interp.is_active = 0;
	interp.next_musicfile_fd = -1;

	if (fcntl(seq_conn->socket, F_SETFL, O_NONBLOCK) == -1) {
		warn("could not set sequencer connection socket non-blocking");
		return 1;
	}

	if (server_conn != NULL) {
		if (fcntl(server_conn->socket, F_SETFL, O_NONBLOCK) == -1) {
			warn("could not set server connection socket"
			    " non-blocking");
			return 1;
		}
	}

	signal(SIGINT,  handle_signal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, handle_signal);

	if (sigemptyset(&loop_sigmask) == -1 ||
	    sigaddset(&loop_sigmask, SIGCHLD) == -1 ||
	    sigaddset(&loop_sigmask, SIGINT) == -1 ||
	    sigaddset(&loop_sigmask, SIGTERM) == -1 ||
	    sigprocmask(SIG_BLOCK, &loop_sigmask, NULL) == -1 ||
	    sigemptyset(&select_sigmask) == -1) {
		warn("error in setting up client signal handling");
		return 1;
	}

	ret = enqueue_song(server_conn, musicfiles, &interp, 0);
	if (ret != 0) {
		warnx("could not enqueue first song");
		return 1;
	}

	while (!musicfiles->all_done) {
		if (mdl_shutdown_client)
			break;

		if (server_conn == NULL) {
			ret = handle_interpreter_process(&interp, seq_conn,
			    musicfiles);
			if (ret != 0) {
				warnx("error handling interpreter process");
				retvalue = 1;
				break;
			}
		}

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_SET(seq_conn->socket, &readfds);

		if (seq_conn->pending_writes)
			FD_SET(seq_conn->socket, &writefds);

		if (server_conn != NULL) {
			FD_SET(server_conn->socket, &readfds);
			if (server_conn->pending_writes)
				FD_SET(server_conn->socket, &writefds);
		}

		ret = pselect(FD_SETSIZE, &readfds, &writefds, NULL, NULL,
		    &select_sigmask);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			warn("error in pselect");
			retvalue = 1;
			break;
		}

		/* Handle sequencer connection. */

		if (FD_ISSET(seq_conn->socket, &readfds)) {
			ret = handle_sequencer_events(server_conn, seq_conn,
			    musicfiles, &interp);
			if (ret != 0) {
				warnx("error handling sequencer events");
				retvalue = 1;
				break;
			}
		}

		if (FD_ISSET(seq_conn->socket, &writefds)) {
			if (imsg_flush(&seq_conn->ibuf) == -1) {
				if (errno == EAGAIN)
					continue;
				warnx("error in sending messages to"
				    " sequencer");
				retvalue = 1;
				break;
			} else {
				seq_conn->pending_writes = 0;
			}
		}

		if (server_conn != NULL) {
			/* Handle server connection. */

			if (FD_ISSET(server_conn->socket, &readfds)) {
				ret = handle_server_events(server_conn,
				    seq_conn);
				if (ret != 0) {
					warnx("error handling server events");
					retvalue = 1;
					break;
				}
			}

			if (FD_ISSET(server_conn->socket, &writefds)) {
				if (imsg_flush(&server_conn->ibuf) == -1) {
					if (errno == EAGAIN)
						continue;
					warnx("error in sending messages to"
					    " server");
					retvalue = 1;
					break;
				} else {
					server_conn->pending_writes = 0;
				}
			}
		}
	}

	for (i = 0; i < musicfiles->count; i++) {
		fd = musicfiles->files[i].fd;
		if (fd == -1)
			continue;
		if (close(fd) == -1)
			warn("error closing %s", musicfiles->files[i].path);
	}

	if (interp.is_active) {
		if (kill(interp.process.pid, SIGTERM) == -1) {
			warn("error killing the current interpreter");
		} else {
			_mdl_log(MDLLOG_IPC, 0,
			    "sent SIGTERM to interpreter process\n");
			if (waitpid(interp.process.pid, &status, 0) == -1)
				warn("waiting for interpreter");
		}
	}

	if (server_conn != NULL)
		imsg_clear(&server_conn->ibuf);

	return retvalue;
}

static int
handle_interpreter_process(struct interpreter_handler *interp,
    struct sequencer_connection *seq_conn, struct musicfiles *musicfiles)
{
	int ret, status;
	pid_t pid;

	if (interp->is_active) {
		pid = waitpid(interp->process.pid, &status, WNOHANG);
		if (pid == -1) {
			warn("waiting for interpreter process");
			return 1;
		}
		if (pid == 0)
			return 0;

		_mdl_log(MDLLOG_IPC, 0, "interpreter (pid %d) has finished\n",
		    interp->process.pid);

		interp->is_active = 0;
	}

	if (interp->is_active || interp->next_musicfile_fd == -1)
		return 0;

	/* Start a new interpreter process. */

	ret = _mdl_interpreter_start_process(&interp->process,
	    interp->next_musicfile_fd, seq_conn->socket);

	assert(musicfiles->current < musicfiles->count);
	assert(interp->next_musicfile_fd
	    == musicfiles->files[ musicfiles->current ].fd);
	if (close(interp->next_musicfile_fd) == -1) {
		warn("closing musicfile %s",
		    musicfiles->files[ musicfiles->current ].path);
	}
	interp->next_musicfile_fd = -1;
	musicfiles->files[ musicfiles->current ].fd = -1;

	if (ret != 0) {
		warnx("could not start interpreter process");
		return 1;
	}

	interp->is_active = 1;

	_mdl_log(MDLLOG_IPC, 0, "sending interpreter pipe to sequencer\n");

	ret = imsg_compose(&seq_conn->ibuf, CLIENTEVENT_NEW_SONG, 0, 0,
	    interp->process.sequencer_read_pipe, "", 0);
	if (ret == -1) {
		warnx("sending interpreter pipe to sequencer");
		return 1;
	}

	seq_conn->pending_writes = 1;

	return 0;
}

static int
handle_sequencer_events(struct server_connection *server_conn,
    struct sequencer_connection *seq_conn, struct musicfiles *musicfiles,
    struct interpreter_handler *interp)
{
	struct imsg imsg;
	enum mdl_event event;
	ssize_t nr;
	int ret, retvalue;

	retvalue = 0;

	if ((nr = imsg_read(&seq_conn->ibuf)) == -1) {
		if (errno == EAGAIN)
			return 0;
		warnx("error in imsg_read for sequencer connection");
		return 1;
	}

	if (nr == 0) {
		warnx("sequencer connection was closed");
		return 1;
	}

	for (;;) {
		nr = imsg_get(&seq_conn->ibuf, &imsg);
		if (nr == -1) {
			warnx("error in handle_sequencer_events/imsg_get");
			return 1;
		}
		if (nr == 0)
			return 0;

		event = imsg.hdr.type;
		switch (event) {
		case CLIENTEVENT_NEW_MUSICFD:
		case CLIENTEVENT_NEW_SONG:
		case CLIENTEVENT_REPLACE_SONG:
			warnx("received a client event on client from"
			    " sequencer, this should not happen");
			retvalue = 1;
			break;
		case SEQEVENT_SONG_END:
			_mdl_log(MDLLOG_SONG, 0, "finished playing %s\n",
			    musicfiles->files[ musicfiles->current ].path);
			ret = enqueue_song(server_conn, musicfiles, interp, 1);
			if (ret != 0) {
				warnx("problem enqueueing next song");
				retvalue = 1;
			}
			break;
		case SERVEREVENT_NEW_CLIENT:
		case SERVEREVENT_NEW_INTERPRETER:
			warnx("received a server event on client from"
			    " sequencer, this should not happen");
			retvalue = 1;
			break;
		default:
			warnx("received an unknown event from sequencer");
			retvalue = 1;
			break;
		}

		imsg_free(&imsg);

		if (retvalue != 0)
			break;
	}

	return retvalue;
}

static int
enqueue_song(struct server_connection *server_conn,
    struct musicfiles *musicfiles, struct interpreter_handler *interp,
    int enqueue_next)
{
	struct musicfile *mf;
	int ret;

	if (enqueue_next)
		musicfiles->current += 1;

	if (musicfiles->current >= musicfiles->count) {
		musicfiles->all_done = 1;
		return 0;
	}

	mf = &musicfiles->files[ musicfiles->current ];

	_mdl_log(MDLLOG_SONG, 0, "starting to play %s\n", mf->path);

	if (server_conn != NULL) {
		ret = imsg_compose(&server_conn->ibuf,
		    CLIENTEVENT_NEW_MUSICFD, 0, 0, mf->fd, "", 0);
		if (ret == -1) {
			warnx("error sending a music file descriptor"
			    " to server");
			return 1;
		}
		server_conn->pending_writes = 1;
		mf->fd = -1;
	} else {
		if (interp->next_musicfile_fd >= 0 &&
		    close(interp->next_musicfile_fd) == -1)
			warn("closing interpreter musicfile fd");
		interp->next_musicfile_fd = mf->fd;
	}

	return 0;
}

static int
handle_server_events(struct server_connection *server_conn,
    struct sequencer_connection *seq_conn)
{
	struct imsg imsg;
	enum mdl_event event;
	ssize_t nr;
	int ret, retvalue;

	retvalue = 0;

	if ((nr = imsg_read(&server_conn->ibuf)) == -1 || nr == 0) {
		warnx("error reading from server / imsg_read");
		return 1;
	}

	for (;;) {
		nr = imsg_get(&server_conn->ibuf, &imsg);
		if (nr == -1) {
			warnx("error reading from server / imsg_get");
			return 1;
		}
		if (nr == 0)
			return 0;

		event = imsg.hdr.type;
		switch (event) {
		case CLIENTEVENT_NEW_MUSICFD:
		case CLIENTEVENT_NEW_SONG:
		case CLIENTEVENT_REPLACE_SONG:
			warnx("received a client event on client from"
			    " server, this should not happen");
			retvalue = 1;
			break;
		case SEQEVENT_SONG_END:
			warnx("received a sequencer event on client from"
			    " server, this should not happen");
			retvalue = 1;
			break;
		case SERVEREVENT_NEW_CLIENT:
			warnx("client received information on new client");
			retvalue = 1;
			break;
		case SERVEREVENT_NEW_INTERPRETER:
			if (imsg.fd == -1) {
				warnx("client expected a new interpreter pipe"
				    " from server, but did not receive one");
				retvalue = 1;
				break;
			}
			_mdl_log(MDLLOG_IPC, 0, "received an interpreter pipe"
			    " (for sequencer) from server\n");
			_mdl_log(MDLLOG_IPC, 0, "sending interpreter pipe to"
			    " server\n");
			ret = imsg_compose(&seq_conn->ibuf,
			    CLIENTEVENT_NEW_SONG, 0, 0, imsg.fd, "", 0);
			if (ret == -1) {
				warnx("could not send interpreter pipe to"
				    " sequencer");
				if (close(imsg.fd) == -1)
					warn("closing interpreter pipe");
				retvalue = 1;
			}
			seq_conn->pending_writes = 1;
			break;
		default:
			warnx("received an unknown event from server");
			retvalue = 1;
			break;
		}

		imsg_free(&imsg);
	}


	return retvalue;
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

	/* Sequencer socket is a blocking socket at this point */
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

	seq_conn->pending_writes = 0;
	seq_conn->socket = cs_sp[1];

	imsg_init(&seq_conn->ibuf, seq_conn->socket);

	return 0;
}
