/* $Id: mdl.c,v 1.20 2015/10/09 19:48:13 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkilä <je@turnipsi.no-ip.org>
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
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interpreter.h"
#include "sequencer.h"

#define SOCKETPATH_LEN 104

static int		get_default_mdldir(char *);
static int		get_default_socketpath(char *, const char *);
static int		start_interpreter(int, int, int);
static void		handle_signal(int);
static int		send_fd_through_socket(int, int);
static int		setup_sequencer_for_sources(char **,
						    int,
						    const char *);
static int		setup_server_socket(const char *);
static void __dead	usage(void);

/* if set in signal handler, should do shutdown */
volatile sig_atomic_t mdl_shutdown = 0;

static void __dead
usage(void)
{
	(void) fprintf(stderr, "usage: mdl [-cs] [-d mdldir] [file ...]\n");
	exit(1);
}

static void
handle_signal(int signo)
{
	mdl_shutdown = 1;
}

int
main(int argc, char *argv[])
{
	char mdldir[PATH_MAX], server_socketpath[SOCKETPATH_LEN];
	char **musicfiles;
	int musicfilecount, ch, cflag, dflag, sflag, fileflags;
	size_t ret;

	signal(SIGINT,  handle_signal);
	signal(SIGTERM, handle_signal);

	if (get_default_mdldir(mdldir) != 0)
		errx(1, "could not get default mdl directory");

	cflag = dflag = sflag = 0;

	while ((ch = getopt(argc, argv, "cd:s")) != -1) {
		switch (ch) {
		case 'c':
			cflag = 1;
			break;
		case 'd':
			dflag = 1;
			if (strlcpy(mdldir, optarg, PATH_MAX) >= PATH_MAX)
				errx(1, "mdldir too long");
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if ((mkdir(mdldir, 0755) == -1) && errno != EEXIST)
		err(1, "error creating %s", mdldir);

	if (sflag) {
		/* when opening server socket, open mdldir for exclusive lock,
		 * to get exclusive access to socket path, not needed for
		 * anything else (discard the file descriptor) */
		fileflags = O_RDONLY|O_NONBLOCK|O_EXLOCK|O_DIRECTORY;
		if (open(mdldir, fileflags) == -1) {
			warn("could not open %s for exclusive lock", mdldir);
			errx(1, "do you have another instance of" \
				  " mdl running?");
		}
	}

	if (get_default_socketpath(server_socketpath, mdldir) != 0)
		errx(1, "could not get default socketpath");

	musicfilecount = argc;
	musicfiles = argv;

	if (cflag && sflag) {
		warnx("-c and -s options are mutually exclusive");
		usage();
		/* NOTREACHED */
	}

	if (cflag && musicfilecount > 1)
		warnx("sending only the first musicfile (%s)", musicfiles[0]);

	ret = setup_sequencer_for_sources(musicfiles,
					  musicfilecount,
					  sflag ? server_socketpath : NULL);
	if (ret != 0 || mdl_shutdown == 1)
		return 1;

	return 0;
}

static int
get_default_mdldir(char *mdldir)
{
	int ret;
	char *home;

	if ((home = getenv("HOME")) == NULL) {
		warnx("could not determine user home directory");
		return 1;
	}

	ret = snprintf(mdldir, PATH_MAX, "%s/.mdl", home);
	if (ret == -1 || ret >= PATH_MAX) {
		warnx("mdl home directory too long, check HOME");
		return 1;
	}

	return 0;
}

static int
get_default_socketpath(char *socketpath, const char *mdldir)
{
	int ret;

	ret = snprintf(socketpath, SOCKETPATH_LEN, "%s/socket", mdldir);
	if (ret == -1 || ret >= SOCKETPATH_LEN) {
		warnx("default server socketpath too long, mdldir is %s",
		      mdldir);
		return 1;
	}

	return 0;
}

static int
setup_sequencer_for_sources(char **files,
			    int filecount,
			    const char *socketpath)
{
	int ms_sp[2];	/* main-sequencer socketpair */

	int server_socket, file_fd, ret, retvalue, using_stdin, i;
	pid_t sequencer_pid;
	char *stdinfiles[] = { "-" };

	retvalue = 0;

	/* setup socketpair for main <-> sequencer communication */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ms_sp) == -1) {
		warn("could not setup socketpair for main <-> sequencer");
		return 1;
	}

	/* for the midi sequencer process */
	if ((sequencer_pid = fork()) == -1) {
		warn("could not fork sequencer process");
		if (close(ms_sp[0]) == -1)
			warn("error closing first endpoint of ms_sp");
		if (close(ms_sp[1]) == -1)
			warn("error closing second endpoint of ms_sp");
		return 1;
	}

	if (sequencer_pid == 0) {
		/* sequencer process, start sequencer loop */
		if (close(ms_sp[0]) == -1)
			warn("error closing first endpoint of ms_sp");
		_exit( sequencer_loop(ms_sp[1]) );
	}

	if (close(ms_sp[1]) == -1)
		warn("error closing second endpoint of ms_sp");

	server_socket = -1;
	if (socketpath) {
		if ((server_socket = setup_server_socket(socketpath)) < 0) {
			retvalue = 1;
			goto finish;
		}
	}

	if (filecount == 0) {
		filecount = 1;
		files = stdinfiles;
	}

	for (i = 0; i < filecount && mdl_shutdown == 0; i++) {
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

		ret = start_interpreter(file_fd, ms_sp[0], server_socket);
		if (ret != 0) {
			warnx("error in handling %s",
			      using_stdin ? "stdin" : files[i]);
			if (close(file_fd) == -1) {
				warn("error closing %s", files[i]);
			retvalue = 1;
			goto finish;
		}

		if (file_fd != fileno(stdin) && close(file_fd) == -1)
			warn("error closing %s", files[i]);
	}
}

finish:
	if (close(ms_sp[0]) == -1)
		warn("error closing first endpoint of ms_sp");

	if (server_socket >= 0 && close(server_socket) == -1)
		warn("error closing server socket");

	if (socketpath != NULL && unlink(socketpath) && errno != ENOENT)
		warn("could not delete %s", socketpath);

	return retvalue;
}

static int
start_interpreter(int file_fd, int sequencer_socket, int server_socket)
{
	int mi_sp[2];	/* main-interpreter socketpair */
	int is_sp[2];	/* interpreter-sequencer socketpair */

	int ret;
	int status;
	pid_t interpreter_pid;

	/* setup socketpair for main <-> interpreter communication */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, mi_sp) == -1) {
		warn("could not setup socketpair for main <-> interpreter");
		return 1;
	}

	/* setup socketpair for interpreter <-> sequencer communication */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, is_sp) == -1) {
		warn("could not setup socketpair for" \
		       " interpreter <-> sequencer");
		if (close(mi_sp[0]) == -1)
			warn("error closing first endpoint of mi_sp");
		if (close(mi_sp[1]) == -1)
			warn("error closing second endpoint of mi_sp");
		return 1;
	}


	if ((interpreter_pid = fork()) == -1) {
		warn("could not fork interpreter pid");
		if (close(mi_sp[0]) == -1)
			warn("error closing first endpoint of mi_sp");
		if (close(mi_sp[1]) == -1)
			warn("error closing second endpoint of mi_sp");
		if (close(is_sp[0]) == -1)
			warn("error closing first endpoint of is_sp");
		if (close(is_sp[1]) == -1)
			warn("error closing second endpoint of is_sp");
		return 1;
	}

	if (interpreter_pid == 0) {
		/* interpreter process */
		if (close(mi_sp[0]) == -1)
			warn("error closing first endpoint of mi_sp");
		if (close(is_sp[1]) == -1)
			warn("error closing second endpoint of is_sp");

		ret = handle_musicfile_and_socket(file_fd,
					   	  mi_sp[1],
					   	  is_sp[0],
						  server_socket);
		_exit(ret);
	}

	if (close(mi_sp[1]) == -1)
		warn("error closing second endpoint of mi_sp");
	if (close(is_sp[0]) == -1)
		warn("error closing first endpoint of is_sp");

	/* we can communicate to interpreter through mi_sp[0],
	 * but to establish interpreter <-> sequencer communucation
	 * we must send is_sp[1] to sequencer */

	if (send_fd_through_socket(is_sp[1], sequencer_socket) != 0) {
		/* XXX what to do in case of error?
		 * XXX what should we clean up? */
	}

	/* XXX only one interpreter thread should be running at once,
	 * XXX so we should wait specifically for that one to finish
	 * XXX (we might do something interesting while waiting, though */
	if (waitpid(interpreter_pid, &status, 0) == -1)
		warn("error when wait for interpreter to finish");

	return 0;
}

static int
send_fd_through_socket(int fd, int socket)
{
	struct msghdr	msg;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr	hdr;
		unsigned char	buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	memset(&msg, 0, sizeof(msg));
	msg.msg_control    = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type  = SCM_RIGHTS;

	*(int *)CMSG_DATA(cmsg) = fd;

	if (sendmsg(socket, &msg, 0) == -1) {
		warn("sending fd through socket");
		return 1;
	}

	return 0;
}

static int
setup_server_socket(const char *socketpath)
{
	struct sockaddr_un sun;
	int ret, server_socket;

	bzero(&sun, sizeof(struct sockaddr_un));
	sun.sun_family = AF_UNIX;
	ret = strlcpy(sun.sun_path, socketpath, SOCKETPATH_LEN);
	assert(ret < SOCKETPATH_LEN);

	if ((server_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		warn("could not open socket %s", socketpath);
		return -1;
	}

	/* exclusive flock() for mdldir should mean that
	 * no other mdl process is using the socketpath */
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
