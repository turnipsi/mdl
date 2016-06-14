/* $Id: mdld.c,v 1.18 2016/06/14 12:15:33 je Exp $ */

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
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
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

#define SOCKETPATH_LEN	104

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

static int	setup_socketdir(const char *);
static int	get_musicfile_fd(struct imsgbuf *);
static void	handle_signal(int);
static int	handle_connections(struct sequencer_process *, int);
static int	handle_client_connection(struct sequencer_process *, int);
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
	struct sequencer_process seq_proc;
	char *devicepath, *socketpath;
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

	seq_proc.pid = 0;
	seq_proc.socket = -1;

	if (pledge("cpath proc recvfd rpath sendfd stdio unix wpath",
	    NULL) == -1)
		err(1, "pledge");

	signal(SIGINT,  handle_signal);
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

	/* After calling setup_server_socket() we might have created the
	 * socket in filesystem, so we must go through "finish" to unlink it
	 * and exit.  Except if pledge() call fails we stop immediately. */
	if ((server_socket = setup_server_socket(socketpath)) == -1) {
		warnx("could not setup server socket");
		exitstatus = 1;
		goto finish;
	}

	ret = _mdl_start_sequencer_process(&seq_proc, mididev_type,
	    devicepath, 0);
	if (ret != 0) {
		warnx("error in starting up sequencer");
		exitstatus = 1;
		goto finish;
	}

	/* Now that sequencer has been forked, we can drop "wpath" pledge. */
	if (pledge("cpath proc recvfd rpath sendfd stdio unix", NULL) == -1)
		err(1, "pledge");

	ret = handle_connections(&seq_proc, server_socket);
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

	if (_mdl_disconnect_sequencer_process(&seq_proc) != 0)
		warnx("error in disconnecting to sequencer process");

	_mdl_logging_close();

	return exitstatus;
}

static int
setup_socketdir(const char *socketpath)
{
	char socketpath_copy[SOCKETPATH_LEN];
	char *socketpath_dir;
	struct stat sb;
	size_t s;
	uid_t uid;
	mode_t mask, omask;

	s = strlcpy(socketpath_copy, socketpath, SOCKETPATH_LEN);
	if (s >= SOCKETPATH_LEN) {
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
handle_connections(struct sequencer_process *seq_proc, int server_socket)
{
	int ret, retvalue;
	fd_set readfds;
	sigset_t loop_sigmask, select_sigmask;

	retvalue = 0;

	(void) sigemptyset(&loop_sigmask);
	(void) sigaddset(&loop_sigmask, SIGINT);
	(void) sigaddset(&loop_sigmask, SIGTERM);
	(void) sigprocmask(SIG_BLOCK, &loop_sigmask, NULL);

	(void) sigemptyset(&select_sigmask);

	while (!mdld_shutdown_server) {
		/* XXX for a loop as simple as this pselect() is not needed */
		FD_ZERO(&readfds);
		FD_SET(server_socket, &readfds);

		ret = pselect(FD_SETSIZE, &readfds, NULL, NULL, NULL,
		    &select_sigmask);
		if (ret == -1 && errno != EINTR) {
			warn("error in pselect");
			retvalue = 1;
			break;
		}

		if (FD_ISSET(server_socket, &readfds)) {
			ret = handle_client_connection(seq_proc,
			    server_socket);
			if (ret != 0)
				warnx("error in handling client connection");
			continue;
		}
	}

	return retvalue;
}

static int
handle_client_connection(struct sequencer_process *seq_proc, int server_socket)
{
	struct interpreter_process interp;
	struct imsgbuf client_ibuf;
	struct sockaddr_storage socket_addr;
	socklen_t socket_len;
	int cs_sp[2];
	int client_socket, musicfile_fd, ret, retvalue;

	cs_sp[0] = -1;
	cs_sp[1] = -1;
	musicfile_fd = -1;

	socket_len = sizeof(socket_addr);
	client_socket = accept(server_socket, (struct sockaddr *)&socket_addr,
	    &socket_len);
	if (client_socket == -1) {
		warn("accept");
		return 1;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, cs_sp) == -1) {
		warn("could not setup socketpair for client <-> sequencer");
		retvalue = 1;
		goto finish;
	}

	imsg_init(&client_ibuf, client_socket);

	ret = imsg_compose(&seq_proc->ibuf, SERVEREVENT_NEW_CLIENT, 0, 0,
	    cs_sp[0], "", 0);
	if (ret == -1 || imsg_flush(&seq_proc->ibuf) == -1) {
		warnx("sending new client event to sequencer");
		retvalue = 1;
		goto finish;
	}
	cs_sp[0] = -1;

	ret = imsg_compose(&client_ibuf, SERVEREVENT_NEW_CLIENT, 0, 0,
	    cs_sp[1], "", 0);
	if (ret == -1 || imsg_flush(&client_ibuf) == -1) {
		warnx("sending new sequencer socket to client");
		retvalue = 1;
		goto finish;
	}
	cs_sp[1] = -1;

	if ((musicfile_fd = get_musicfile_fd(&client_ibuf)) == -1) {
		warnx("did not receive new music file descriptor from client");
		retvalue = 1;
		goto finish;
	}

	ret = _mdl_start_interpreter_process(&interp, musicfile_fd,
	    seq_proc->socket);
	if (ret != 0) {
		warnx("could not start interpreter process");
		retvalue = 1;
		goto finish;
	}

	ret = imsg_compose(&client_ibuf, SERVEREVENT_NEW_INTERPRETER, 0, 0,
	    interp.sequencer_read_pipe, "", 0);
	if (ret == -1 || imsg_flush(&client_ibuf) == -1) {
		warnx("sending interpreter pipe to client");
		retvalue = 1;
		goto finish;
	}

	/*
	 * Client should now pass interpreter_fd to sequencer through the
	 * socketpair created above.  We wait for client and interpreter to
	 * do their work, before we listen for next clients.
	 */

	ret = _mdl_wait_for_subprocess("interpreter", interp.pid);
	if (ret != 0) {
		warnx("error in interpreter subprocess");
		retvalue = 1;
		goto finish;
	}

finish:
	if (cs_sp[0] >= 0 && close(cs_sp[0]) == -1)
		warn("closing first end of cs_sp");
	if (cs_sp[1] >= 0 && close(cs_sp[1]) == -1)
		warn("closing second end of cs_sp");
	if (musicfile_fd >= 0 && close(musicfile_fd) == -1)
		warn("closing music file descriptor");

	if (client_socket >= 0) {
		imsg_clear(&client_ibuf);
		if (close(client_socket) == -1)
			warn("closing client connection");
	}

	return retvalue;
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
	ret = strlcpy(sun.sun_path, socketpath, SOCKETPATH_LEN);
	assert(ret < SOCKETPATH_LEN);

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

static int
get_musicfile_fd(struct imsgbuf *ibuf)
{
	struct imsg imsg;
	ssize_t nr;

	if ((nr = imsg_read(ibuf)) == -1 || nr == 0) {
		warnx("error in imsg_read");
		return 1;
	}
	
	if ((nr = imsg_get(ibuf, &imsg)) == -1 || nr == 0) {
		warnx("error in imsg_get");
		return 1;
	}

	if (imsg.hdr.type != CLIENTEVENT_NEW_MUSICFD) {
		warnx("received event was not a new music descriptor event");
		return 1;
	}

	return imsg.fd;
}
