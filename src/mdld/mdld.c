/* $Id: mdld.c,v 1.10 2016/05/18 20:29:16 je Exp $ */

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
static void	handle_signal(int);
static int	handle_connections(int, struct sequencer_process);
static int	setup_server_socket(const char *);
static void __dead usage(void);

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdld_shutdown_main = 0;

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
		mdld_shutdown_main = 1;
}

int
main(int argc, char *argv[])
{
	struct sequencer_process sequencer;
	char *devicepath, *socketpath;
	int ch, exitstatus, server_socket;
	size_t ret;
	enum mididev_type mididev_type;

#ifdef HAVE_MALLOC_OPTIONS
	malloc_options = (char *) "AFGJPS";
#endif /* HAVE_MALLOC_OPTIONS */

	_mdl_process_type = "main";

	devicepath = NULL;
	exitstatus = 0;
	mididev_type = DEFAULT_MIDIDEV_TYPE;
	server_socket = -1;
	socketpath = NULL;

	sequencer.pid = 0;
	sequencer.socket = -1;

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

	_mdl_log(MDLLOG_PROCESS, 0, "new main process, pid %d\n", getpid());

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

	ret = _mdl_start_sequencer_process(&sequencer, mididev_type,
	    devicepath, 0);
	if (ret != 0) {
		warnx("error in starting up sequencer");
		exitstatus = 1;
		goto finish;
	}

	/* Now that sequencer has been forked, we can drop "wpath" pledge. */
	if (pledge("cpath proc recvfd rpath sendfd stdio unix", NULL) == -1)
		err(1, "pledge");

	ret = handle_connections(server_socket, sequencer);
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

	if (sequencer.socket >= 0 && close(sequencer.socket) == -1)
		warn("error closing sequencer connection");

	if (sequencer.pid > 0) {
		ret = _mdl_wait_for_subprocess("sequencer", sequencer.pid);
		if (ret != 0) {
			errx(1, "error when waiting for sequencer subprocess");
			exitstatus = 1;
		}
	}

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
handle_connections(int server_socket, struct sequencer_process sequencer)
{
	int client_fd, ret, retvalue;
	struct sockaddr_storage socket_addr;
	socklen_t socket_len;

	retvalue = 0;
	client_fd = -1;

	socket_len = sizeof(socket_addr);
	client_fd = accept(server_socket, (struct sockaddr *)&socket_addr,
	    &socket_len);
	if (client_fd == -1) {
		warn("accept");
		retvalue = 1;
		goto finish;
	}

	ret = _mdl_start_interpreter(client_fd, sequencer.socket);
	if (ret != 0)
		retvalue = 1;

finish:
	if (client_fd >= 0 && close(client_fd) == -1)
		warn("error closing client connection");

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
