/* $Id: mdl.c,v 1.4 2015/10/03 06:40:23 je Exp $ */

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
#include <sys/types.h>
#include <sys/un.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOCKETPATH_LEN 104

int	get_default_socketpath(char *);
int	handle_musicfiles_and_socket(char **, int, char *);
int	setup_server_socket(char *);

static void __dead
usage(void)
{
	extern char *__progname;

	(void) fprintf(stderr,
		       "usage: %s [-s] [-S socketpath] [file ...]\n",
		       __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char server_socketpath[SOCKETPATH_LEN];
	char **musicfiles;
	int musicfile_count, ch, sflag, Sflag;
	size_t ret;

	sflag = Sflag = 0;

	if (get_default_socketpath(server_socketpath) != 0)
		errx("could not get default socketpath");

	while ((ch = getopt(argc, argv, "sS:")) != -1) {
		switch (ch) {
		case 's':
			sflag = 1;
			break;
		case 'S':
			Sflag = 1;
			ret = strlcpy(server_socketpath,
				      optarg,
				      SOCKETPATH_LEN);
			if (ret >= SOCKETPATH_LEN)
				errx(1, "server socketpath too long");
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	musicfiles = argv;
	musicfile_count = argc;

	if (musicfile_count == 0 && !sflag) {
		warnx("no musicfiles given and not in server mode");
		usage();
		/* NOTREACHED */
	}

	if (Sflag && !sflag)
		warnx("socketpath set but not in server mode");

	ret = handle_musicfiles_and_socket(musicfiles,
					   musicfile_count,
					   sflag ? server_socketpath : NULL);
	if (ret)
		return 1;

	return 0;
}

int
get_default_socketpath(char *socketpath)
{
	int ret;
	char *home;

	if ((home = getenv("HOME")) == NULL) {
		warnx("could not determine user home directory");
		return 1;
	}

	ret = snprintf(socketpath, SOCKETPATH_LEN, "%s/.mdl/socket", home);
	if (ret == -1 || ret >= SOCKETPATH_LEN) {
		warnx("default server socketpath too long, check HOME");
		return 1;
	}

	return 0;
}

int
handle_musicfiles_and_socket(char **musicfiles,
			     int musicfile_count,
			     char *socketpath)
{
	int server_socket, i;

	server_socket = -1;
	if (socketpath) {
		if ((server_socket = setup_server_socket(socketpath)) < 0)
			return 1;
	}

	return 0;
}

int
setup_server_socket(char *socketpath)
{
	struct sockaddr_un sun;
	int ret, retvalue, server_socket;

	retvalue = 0;

	bzero(&sun, sizeof(struct sockaddr_un));
	sun.sun_family = AF_UNIX;
	ret = strlcpy(sun.sun_path, socketpath, SOCKETPATH_LEN);
	assert(ret < SOCKETPATH_LEN);

	if ((server_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		warn("could not open socket");
		return 1;
	}

	if (bind(server_socket, (struct sockaddr *)&sun, /* XXX */ 0) == -1) {
		warn("could not bind socket");
		retvalue = 1;
		goto close;
	}

unlink:
	/* XXX unlink() server_socket */
close:
	(void) close(server_socket);

	return retvalue;
}
