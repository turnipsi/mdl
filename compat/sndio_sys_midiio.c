/* $Id: sndio_sys_midiio.c,v 1.3 2016/04/24 19:33:31 je Exp $ */

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

#ifdef HAVE_SYS_MIDIIO

#include <sys/types.h>
#include <sys/midiio.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "sndio.h"

static struct mio_hdl mio_sys_midiio = { .fd = -1 };

struct mio_hdl *
mio_open(const char *name, unsigned int mode, int nbio_flag)
{
	int fd;

	/* We support only the parts of the sndio API that we need. */
	assert(strcmp(name, MIO_PORTANY) == 0);
	assert(mode == MIO_OUT);
	assert(nbio_flag == 0);

	assert(mio_sys_midiio.fd == -1);

	if ((fd = open("/dev/music", O_WRONLY)) == -1) {
		warn("error when opening /dev/music");
		return NULL;
	}

	mio_sys_midiio.fd = fd;

	return &mio_sys_midiio;
}

size_t
mio_write(struct mio_hdl *hdl, const void *addr, size_t nbytes)
{
	ssize_t nw, total_wcount;
	int n;

	assert(hdl != NULL);
	assert(hdl->fd >= 0);

	total_wcount = 0;

	while (total_wcount < nbytes) {
		nw = write(hdl->fd, addr, nbytes - total_wcount);
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to midi device");
			return 0;
		}
		total_wcount += nw;
	}

	return total_wcount;
}

void
mio_close(struct mio_hdl *hdl)
{
	assert(mio_sys_midiio.fd >= 0);

	if (close(mio_sys_midiio.fd) == -1)
		warn("error closing /dev/music");

	mio_sys_midiio.fd = -1;
}

#endif /* HAVE_SYS_MIDIIO */
