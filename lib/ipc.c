/* $Id: ipc.c,v 1.1 2016/06/15 20:19:36 je Exp $ */

/*
 * Copyright (c) 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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

#include <sys/types.h>

#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "ipc.h"

#define SOCKETPATH_LEN	104

char *
_mdl_get_socketpath(void)
{
	static char socketpath[SOCKETPATH_LEN];
	uid_t uid;
	int ret;

	uid = geteuid();
	if (uid == 0) {
		ret = snprintf(socketpath, SOCKETPATH_LEN, "/tmp/mdl/socket");
	} else {
		ret = snprintf(socketpath, SOCKETPATH_LEN,
		    "/tmp/mdl-%u/socket", uid);
	}
	if (ret == -1 || ret >= SOCKETPATH_LEN) {
		warnx("snprintf error for server socketpath");
		return NULL;
	}

	return socketpath;
}
