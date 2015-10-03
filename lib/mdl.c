/* $Id: mdl.c,v 1.5 2015/10/03 10:39:16 je Exp $ */

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
#include <sys/syslimits.h>
#include <sys/types.h>
#include <sys/un.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOCKETPATH_LEN 104

int	get_default_socketpath(char *, char *);
int	handle_musicfiles_and_socket(char **, int, char *);
int	make_and_get_mdlhome(char *);
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
	char mdlhome[PATH_MAX], server_socketpath[SOCKETPATH_LEN];
	char **musicfiles;
	int musicfile_count, ch, sflag, Sflag;
	size_t ret;

	sflag = Sflag = 0;

	if (make_and_get_mdlhome(mdlhome) != 0)
		errx(1, "could not make mdl directory");

	if (get_default_socketpath(server_socketpath, mdlhome) != 0)
		errx(1, "could not get default socketpath");

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
make_and_get_mdlhome(char *mdlhome)
{
	int ret;
	char *home;

	if ((home = getenv("HOME")) == NULL) {
		warnx("could not determine user home directory");
		return 1;
	}

	ret = snprintf(mdlhome, PATH_MAX, "%s/.mdl", home);
	if (ret == -1 || ret >= PATH_MAX) {
		warnx("mdl home directory too long, check HOME");
		return 1;
	}

	if ((mkdir(mdlhome, 0755) == -1) && (errno != EEXIST)) {
		warn("error creating %s", mdlhome);
		return 1;
	}

	return 0;
}

int
get_default_socketpath(char *socketpath, char *mdlhome)
{
	int ret;

	ret = snprintf(socketpath, SOCKETPATH_LEN, "%s/socket", mdlhome);
	if (ret == -1 || ret >= SOCKETPATH_LEN) {
		warnx("default server socketpath too long, mdl home is %s",
		      mdlhome);
		return 1;
	}

	return 0;
}

int
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

	/* XXX unlink() server_socket if done with */
	/* XXX close()  server_socket if done with */

	return 0;
}

int
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
		return 1;
	}

	ret = bind(server_socket, (struct sockaddr *)&sun, SUN_LEN(&sun));
	if (ret == -1) {
		warn("could not bind socket %s", socketpath);
		(void) close(server_socket);
		return 1;
	}

	return 0;
}
