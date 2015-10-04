/* $Id: mdl.c,v 1.11 2015/10/04 19:39:42 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkil� <je@turnipsi.no-ip.org>
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
#include <sys/syslimits.h>
#include <sys/types.h>
#include <sys/un.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sequencer.h"

#define SOCKETPATH_LEN 104

static int		get_default_mdldir(char *);
static int		get_default_socketpath(char *, char *);
static int		handle_musicfiles_and_socket(char **, int, char *);
static void		handle_signal(int);
static int		setup_server_socket(char *);
static void __dead	usage(void);

/* if set in signal handler, should do shutdown */
volatile sig_atomic_t do_termshutdown = 0;

static void __dead
usage(void)
{
	(void) fprintf(stderr, "usage: mdl [-cs] [-d mdldir] [file ...]\n");
	exit(1);
}

static void
handle_signal(int signo)
{
	do_termshutdown = 1;
}

int
main(int argc, char *argv[])
{
	char mdldir[PATH_MAX], server_socketpath[SOCKETPATH_LEN];
	char **musicfiles;
	int musicfile_count, ch, cflag, dflag, sflag;
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

	/* open mdldir for exclusive lock, not needed for anything else
	   (discard the file descriptor) */
	if (open(mdldir, O_RDONLY|O_NONBLOCK|O_EXLOCK|O_DIRECTORY) == -1) {
		warn("could not open %s for exclusive lock", mdldir);
		errx(1, "do you have another instance of mdl running?");
	}

	if (get_default_socketpath(server_socketpath, mdldir) != 0)
		errx(1, "could not get default socketpath");

	musicfiles = argv;
	musicfile_count = argc;

	if (cflag && sflag) {
		warnx("-c and -s options are mutually exclusive");
		usage();
		/* NOTREACHED */
	}

	if (!sflag && musicfile_count == 0) {
		warnx("no musicfiles given and not in server mode");
		usage();
		/* NOTREACHED */
	}

	if (cflag && musicfile_count > 1)
		warnx("sending only the first musicfile (%s)", musicfiles[0]);

	ret = handle_musicfiles_and_socket(musicfiles,
					   musicfile_count,
					   sflag ? server_socketpath : NULL);
	if (ret || do_termshutdown)
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
get_default_socketpath(char *socketpath, char *mdldir)
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
handle_musicfiles_and_socket(char **musicfiles,
			     int musicfile_count,
			     char *socketpath)
{
	int server_socket;

	server_socket = -1;
	if (socketpath) {
		if ((server_socket = setup_server_socket(socketpath)) < 0)
			return 1;
	}

	if (sequencer_init() != 0) {
		warnx("error initializing sequencer");
		return 1;
	}

	while (do_termshutdown == 0)
		;

	sequencer_close();

	if (close(server_socket) < 0)
		warn("error closing server socket");

	if (unlink(socketpath) && errno != ENOENT)
		warn("could not delete %s", socketpath);

	return 0;
}

static int
setup_server_socket(char *socketpath)
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
	if (close(server_socket) < 0)
		warn("error closing server socket");

	return -1;
}
