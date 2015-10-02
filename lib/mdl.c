/* $Id: mdl.c,v 1.3 2015/10/02 17:28:29 je Exp $ */

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SOCKETPATH_LEN 104

void get_default_socketpath(char *, size_t);

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
	int ch, sflag;
	size_t n;

	sflag = 0;

	get_default_socketpath(server_socketpath, SOCKETPATH_LEN);

	while ((ch = getopt(argc, argv, "sS:")) != -1) {
		switch (ch) {
		case 's':
			sflag = 1;
			break;
		case 'S':
			n = strlcpy(server_socketpath, optarg, SOCKETPATH_LEN);
			if (n >= SOCKETPATH_LEN)
				errx(1, "Server socketpath too long");
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	return 0;
}

void
get_default_socketpath(char *socketpath, size_t sockpath_len)
{
	int ret;
	char *home;

	if ((home = getenv("HOME")) == NULL)
		errx(1, "Could not determine user home directory");

	ret = snprintf(socketpath, sockpath_len, "%s/.mdl/socket", home);
	if (ret == -1 || ret >= sockpath_len)
		errx(1, "Default server socketpath too long, check HOME");
}
