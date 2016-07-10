/* $Id: mdld.c,v 1.33 2016/07/10 21:02:42 je Exp $ */

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
#include <libgen.h>
#include <limits.h>
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

struct client_connection {
	TAILQ_ENTRY(client_connection)	tq;
	struct imsgbuf			ibuf;
	int				pending_writes;
	int				socket;
};

TAILQ_HEAD(clientlist, client_connection);

struct interpreter_handler {
	struct client_connection       *client_conn;
	struct interpreter_process	process;
	int				is_active;
	int				next_musicfile_fd;
};

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

static int	setup_socketdir(const char *);
static int	accept_new_client_connection(struct client_connection **,
    struct sequencer_connection *, int);
static void	drop_client(struct client_connection *, struct clientlist *,
    struct interpreter_handler *);
static int	handle_client_events(struct client_connection *,
    struct interpreter_handler *, struct clientlist *);
static int	handle_interpreter_process(struct interpreter_handler *,
    struct sequencer_connection *);
static int	handle_sequencer_events(struct sequencer_connection *);
static int	handle_musicfd_event(struct client_connection *,
    struct interpreter_handler *, int);
static void	handle_signal(int);
static int	handle_connections(struct sequencer_connection *, int);
static int	setup_server_socket(const char *);
static void __dead usage(void);

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdld_shutdown_server = 0;

extern int loglevel;

char *_mdl_process_type;

static void __dead
usage(void)
{
	(void) fprintf(stderr, "usage: mdld [-v] [-d debuglevel] [-f device]"
	    " [-m MIDI-interface]\n");
	exit(1);
}

static void
handle_signal(int signo)
{
	assert(signo == SIGINT || signo == SIGTERM);

	if (signo == SIGINT || signo == SIGTERM)
		mdld_shutdown_server = 1;
}

int
main(int argc, char *argv[])
{
	struct sequencer_connection seq_conn;
	pid_t sequencer_pid;
	const char *devicepath, *socketpath;
	int ch, exitstatus, server_socket;
	size_t ret;
	enum mididev_type mididev_type;

#ifdef HAVE_MALLOC_OPTIONS
	malloc_options = (char *) "AFGJPS";
#endif /* HAVE_MALLOC_OPTIONS */

	_mdl_process_type = "server";

	devicepath = NULL;
	exitstatus = 0;
	mididev_type = DEFAULT_MIDIDEV_TYPE;
	server_socket = -1;
	socketpath = NULL;

	sequencer_pid = 0;

	if (pledge("cpath proc recvfd rpath sendfd stdio unix wpath",
	    NULL) == -1)
		err(1, "pledge");

	signal(SIGINT,  handle_signal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, handle_signal);

	_mdl_logging_init();

	while ((ch = getopt(argc, argv, "d:f:m:v")) != -1) {
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

	if (argc != 0)
		errx(1, "extra arguments after options");

	_mdl_log(MDLLOG_PROCESS, 0, "new server process, pid %d\n", getpid());

	if ((socketpath = _mdl_get_socketpath()) == NULL)
		errx(1, "error in determining server socket path");

	/*
	 * After calling setup_server_socket() we might have created the
	 * socket in filesystem, so we must go through "finish" to unlink it
	 * and exit.  Except if pledge() call fails we stop immediately.
	 */
	if ((server_socket = setup_server_socket(socketpath)) == -1) {
		warnx("could not setup server socket");
		exitstatus = 1;
		goto finish;
	}

	ret = _mdl_start_sequencer_process(&sequencer_pid, &seq_conn,
	    mididev_type, devicepath, 0);
	if (ret != 0) {
		warnx("error in starting up sequencer");
		exitstatus = 1;
		goto finish;
	}

	/* Now that sequencer has been forked, we can drop "wpath" pledge. */
	if (pledge("cpath proc recvfd rpath sendfd stdio unix", NULL) == -1)
		err(1, "pledge");

	ret = handle_connections(&seq_conn, server_socket);
	if (ret != 0) {
		warnx("error in handling connections");
		exitstatus = 1;
	}

finish:
	if (pledge("cpath stdio", NULL) == -1)
		err(1, "pledge");

	if (socketpath != NULL && unlink(socketpath) && errno != ENOENT)
		warn("could not delete %s", socketpath);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (server_socket >= 0 && close(server_socket) == -1)
		warn("error closing server socket");

	if (sequencer_pid != 0) {
		ret = _mdl_disconnect_sequencer_process(sequencer_pid,
		    &seq_conn);
		if (ret != 0)
			warnx("error in disconnecting to sequencer process");
	}

	_mdl_logging_close();

	return exitstatus;
}

static int
setup_socketdir(const char *socketpath)
{
	char socketpath_copy[MDL_SOCKETPATH_LEN];
	char *socketpath_dir;
	struct stat sb;
	size_t s;
	uid_t uid;
	mode_t mask, omask;

	s = strlcpy(socketpath_copy, socketpath, MDL_SOCKETPATH_LEN);
	if (s >= MDL_SOCKETPATH_LEN) {
		warnx("error in making a copy of socketpath");
		return 1;
	}

	if ((socketpath_dir = dirname(socketpath_copy)) == NULL) {
		warn("could not determine socket directory");
		return 1;
	}

	uid = geteuid();
	mask = (uid == 0) ? 0022 : 0077;
	omask = umask(mask);
	if (mkdir(socketpath_dir, 0777) == -1) {
		if (errno != EEXIST) {
			warn("error in making %s", socketpath_dir);
			umask(omask);
			return 1;
		}
	}
	umask(omask);

	if (stat(socketpath_dir, &sb) < 0) {
		warn("stat for %s failed", socketpath_dir);
		return 1;
	}

	if (!S_ISDIR(sb.st_mode)) {
		warn("%s is not a directory", socketpath_dir);
		return 1;
	}

	if (sb.st_uid != uid || (sb.st_mode & mask) != 0) {
		warn("%s has wrong permissions", socketpath_dir);
		return 1;
	}

	return 0;
}

static int
handle_connections(struct sequencer_connection *seq_conn, int server_socket)
{
	struct client_connection *client_conn, *cc_tmp;
	struct clientlist clients;
	struct interpreter_handler interp;
	fd_set readfds, writefds;
	sigset_t loop_sigmask, select_sigmask;
	int ret, retvalue, status;

	retvalue = 0;

	interp.client_conn = NULL;
	interp.is_active = 0;
	interp.next_musicfile_fd = -1;

	TAILQ_INIT(&clients);

	if (fcntl(seq_conn->socket, F_SETFL, O_NONBLOCK) == -1) {
		warn("could not set sequencer connection socket non-blocking");
		return 1;
	}

	if (sigemptyset(&loop_sigmask) == -1 ||
	    sigaddset(&loop_sigmask, SIGCHLD) == -1 ||
	    sigaddset(&loop_sigmask, SIGINT) == -1 ||
	    sigaddset(&loop_sigmask, SIGTERM) == -1 ||
	    sigprocmask(SIG_BLOCK, &loop_sigmask, NULL) == -1 ||
	    sigemptyset(&select_sigmask) == -1) {
		warn("error in setting up server signal handling");
		return 1;
	}

	for (;;) {
		if (mdld_shutdown_server)
			break;

		if (handle_interpreter_process(&interp, seq_conn) != 0) {
			warnx("error handling interpreter process");
			retvalue = 1;
			break;

		}

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_SET(server_socket, &readfds);
		FD_SET(seq_conn->socket, &readfds);

		if (seq_conn->pending_writes)
			FD_SET(seq_conn->socket, &writefds);

		TAILQ_FOREACH(client_conn, &clients, tq) {
			FD_SET(client_conn->socket, &readfds);
			if (client_conn->pending_writes)
				FD_SET(client_conn->socket, &writefds);
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
			ret = handle_sequencer_events(seq_conn);
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

		/* Handle client connections. */

		TAILQ_FOREACH_SAFE(client_conn, &clients, tq, cc_tmp) {
			if (FD_ISSET(client_conn->socket, &readfds)) {
				ret = handle_client_events(client_conn,
				    &interp, &clients);
				if (ret != 0)
					warnx("error handling client events");
				/*
				 * handle_client_events() may remove
				 * client_conn from clients, in which case it
				 * is also freed, so jump to next client.
				 */
				continue;
			}
			if (FD_ISSET(client_conn->socket, &writefds)) {
				if (imsg_flush(&client_conn->ibuf) == -1) {
					if (errno == EAGAIN)
						continue;
					warnx("error in sending messages to"
					    " client, dropping client");
					drop_client(client_conn, &clients,
					    &interp);
				} else {
					client_conn->pending_writes = 0;
				}
			}
		}

		/* Handle new incoming clients. */

		if (FD_ISSET(server_socket, &readfds)) {
			ret = accept_new_client_connection(&client_conn,
			    seq_conn, server_socket);
			if (ret != 0) {
				warnx("error in accepting new client");
				continue;
			}
			TAILQ_INSERT_HEAD(&clients, client_conn, tq);
		}
	}

	/* Drop all clients. */
	TAILQ_FOREACH_SAFE(client_conn, &clients, tq, cc_tmp)
		drop_client(client_conn, &clients, &interp);

	/* Exit the interpreter process. */

	if (interp.next_musicfile_fd >= 0 &&
	    close(interp.next_musicfile_fd) == -1)
		warn("closing next musicfile descriptor");

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

	return retvalue;
}

static int
handle_interpreter_process(struct interpreter_handler *interp,
    struct sequencer_connection *seq_conn)
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

	if (interp->client_conn == NULL) {
		/*
		 * The client connection that requested a new interpreter
		 * process has been dropped, so nothing to do except close
		 * the remaining file descriptor.
		 */
		if (close(interp->next_musicfile_fd) == -1)
			warn("closing musicfile descriptor");
		interp->next_musicfile_fd = -1;
		return 0;
	}

	/* Start a new interpreter process. */

	ret = _mdl_start_interpreter_process(&interp->process,
	    interp->next_musicfile_fd, seq_conn->socket);
	if (close(interp->next_musicfile_fd) == -1)
		warn("closing musicfile descriptor");
	interp->next_musicfile_fd = -1;
	if (ret != 0) {
		warnx("could not start interpreter process");
		return 1;
	}

	interp->is_active = 1;

	_mdl_log(MDLLOG_IPC, 0,
	    "sending interpreter pipe to client (for sequencer)\n");

	ret = imsg_compose(&interp->client_conn->ibuf,
	    SERVEREVENT_NEW_INTERPRETER, 0, 0,
	    interp->process.sequencer_read_pipe, "", 0);
	if (ret == -1) {
		warnx("sending interpreter pipe to client (for sequencer)");
		return 1;
	}

	interp->client_conn->pending_writes = 1;

	/*
	 * Client should now pass the sequencer read pipe to sequencer
	 * through its own client <-> sequencer communication socket.
	 */

	return 0;
}

static int
handle_sequencer_events(struct sequencer_connection *seq_conn)
{
	ssize_t nr;

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

	warnx("received sequencer events on server, this should not happen");

	return 1;
}

static void
drop_client(struct client_connection *client_conn, struct clientlist *clients,
    struct interpreter_handler *interp)
{
	if (interp->client_conn == client_conn)
		interp->client_conn = NULL;

	imsg_clear(&client_conn->ibuf);
	if (close(client_conn->socket) == -1)
		warn("error closing client connection");
	TAILQ_REMOVE(clients, client_conn, tq);
	free(client_conn);
}

static int
handle_client_events(struct client_connection *client_conn,
    struct interpreter_handler *interp, struct clientlist *clients)
{
	enum mdl_event event;
	struct imsg imsg;
	ssize_t nr;
	int ret, retvalue;

	if ((nr = imsg_read(&client_conn->ibuf)) == -1) {
		if (errno == EAGAIN)
			return 0;
		warnx("error in imsg_read");
		drop_client(client_conn, clients, interp);
		return 1;
	}

	if (nr == 0) {
		drop_client(client_conn, clients, interp);
		return 0;
	}

	if ((nr = imsg_get(&client_conn->ibuf, &imsg)) == -1) {
		warnx("error in imsg_get, dropping client");
		drop_client(client_conn, clients, interp);
		return 1;
	}

	if (nr == 0)
		return 0;

	retvalue = 0;

	event = imsg.hdr.type;
	switch (event) {
	case CLIENTEVENT_NEW_MUSICFD:
		ret = handle_musicfd_event(client_conn, interp, imsg.fd);
		if (ret != 0) {
			warnx("error handling CLIENTEVENT_NEW_MUSICFD event");
			retvalue = 1;
		}
		break;
	case CLIENTEVENT_NEW_SONG:
		warnx("server received a new song event from client");
		retvalue = 1;
		break;
	case CLIENTEVENT_REPLACE_SONG:
		warnx("server received a replace song event from client");
		retvalue = 1;
		break;
	case SEQEVENT_SONG_END:
		warnx("server received a sequencer event from client");
		retvalue = 1;
		break;
	case SERVEREVENT_NEW_CLIENT:
	case SERVEREVENT_NEW_INTERPRETER:
		warnx("server received a server event from client");
		retvalue = 1;
		break;
	default:
		warnx("unknown event received from client");
		retvalue = 1;
	}

	imsg_free(&imsg);

	return retvalue;
}

static int
handle_musicfd_event(struct client_connection *client_conn,
    struct interpreter_handler *interp, int musicfile_fd)
{
	if (musicfile_fd == -1) {
		warnx("no music descriptor received when expected");
		return 1;
	}

	_mdl_log(MDLLOG_IPC, 0, "received a new musicfile descriptor\n");

	if (interp->is_active) {
		if (kill(interp->process.pid, SIGTERM) == -1) {
			warn("error killing the current interpreter");
			return 1;
		}
		_mdl_log(MDLLOG_IPC, 0,
		    "sent SIGTERM to interpreter process\n");
	}

	if (interp->next_musicfile_fd >= 0 &&
	    close(interp->next_musicfile_fd) == -1)
		warn("closing previous next musicfile descriptor");

	interp->client_conn = client_conn;
	interp->next_musicfile_fd = musicfile_fd;

	return 0;
}

static int
accept_new_client_connection(struct client_connection **new_client_conn,
    struct sequencer_connection *seq_conn, int server_socket)
{
	struct client_connection *client_conn;
	struct sockaddr_storage socket_addr;
	socklen_t socket_len;
	int cs_sp[2];
	int client_imsg_init, ret;

	cs_sp[0] = -1;
	cs_sp[1] = -1;
	client_imsg_init = 0;

	if ((client_conn = malloc(sizeof(struct client_connection))) == NULL) {
		warn("malloc in accept_new_client_connection");
		return 1;
	}

	socket_len = sizeof(socket_addr);
	client_conn->socket = accept(server_socket,
	    (struct sockaddr *)&socket_addr, &socket_len);
	if (client_conn->socket == -1) {
		warn("accept");
		goto error;
	}

	if (fcntl(client_conn->socket, F_SETFL, O_NONBLOCK) == -1) {
		warn("could not set new client socket non-blocking");
		goto error;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, cs_sp) == -1) {
		warn("could not setup socketpair for client <-> sequencer");
		goto error;
	}

	imsg_init(&client_conn->ibuf, client_conn->socket);
	client_imsg_init = 1;

	ret = imsg_compose(&client_conn->ibuf, SERVEREVENT_NEW_CLIENT, 0, 0,
	    cs_sp[0], "", 0);
	if (ret == -1) {
		warnx("imsg_compose for client connection");
		goto error;
	}
	cs_sp[0] = -1;
	client_conn->pending_writes = 1;

	ret = imsg_compose(&seq_conn->ibuf, SERVEREVENT_NEW_CLIENT, 0, 0,
	    cs_sp[1], "", 0);
	if (ret == -1) {
		warnx("error sending client-sequencer socket to sequencer");
		goto error;
	}
	cs_sp[1] = -1;
	seq_conn->pending_writes = 1;

	*new_client_conn = client_conn;

	return 0;

error:
	if (cs_sp[0] >= 0 && close(cs_sp[0]) == -1)
		warn("closing first end of client <-> sequencer connection");
	if (cs_sp[1] >= 0 && close(cs_sp[1]) == -1)
		warn("closing second end of client <-> sequencer connection");

	if (client_imsg_init)
		imsg_clear(&client_conn->ibuf);
	if (client_conn->socket >= 0 && close(client_conn->socket) == -1)
		warn("closing client connection");
	free(client_conn);

	return 1;
}

static int
setup_server_socket(const char *socketpath)
{
	struct sockaddr_un sun;
	int ret, server_socket;

	if (setup_socketdir(socketpath) != 0) {
		warnx("could not setup directory for server socket");
		return -1;
	}

	memset(&sun, 0, sizeof(struct sockaddr_un));
	sun.sun_family = AF_UNIX;
	ret = strlcpy(sun.sun_path, socketpath, MDL_SOCKETPATH_LEN);
	assert(ret < MDL_SOCKETPATH_LEN);

	if ((server_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		warn("could not open socket %s", socketpath);
		return -1;
	}

	/* If socketpath is already in use, unlink it. */
	if (unlink(socketpath) == -1 && errno != ENOENT) {
		warn("could not remove %s", socketpath);
		goto fail;
	}

	ret = bind(server_socket, (struct sockaddr *)&sun, SUN_LEN(&sun));
	if (ret == -1) {
		warn("could not bind socket %s", socketpath);
		goto fail;
	}

	if (listen(server_socket, 1) == -1) {
		warn("could not listen on socket %s", socketpath);
		goto fail;
	}

	return server_socket;

fail:
	if (close(server_socket) == -1)
		warn("error closing server socket");

	return -1;
}
