/* $iD: Sequencer.c,v 1.1 2015/10/04 19:39:43 je Exp $ */

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

#include <assert.h>
#include <err.h>

#include "sequencer.h"

#define MIDIEVENT_SIZE		3
#define SEQUENCER_NOTEOFF_BASE	0x80
#define SEQUENCER_NOTEON_BASE	0x90

static struct mio_hdl	*mio = NULL;

static void	sequencer_assert_range(int, int, int);
static int	sequencer_noteevent(int, int, int, int);
static int	sequencer_send_midievent(unsigned char *);

int
sequencer_init(void)
{
	if ((mio = mio_open(MIO_PORTANY, MIO_OUT, 0)) == NULL) {
		warnx("could not open midi device");
		return 1;
	}

	return 0;
}

static void
sequencer_assert_range(int channel, int min, int max)
{
	assert(min <= channel && channel <= max);
}

static int
sequencer_send_midievent(unsigned char *midievent)
{
	int ret;

	assert(mio != NULL);

	ret = mio_write(mio, midievent, MIDIEVENT_SIZE);
	if (ret != MIDIEVENT_SIZE) {
		warnx("midi error, tried to write exactly %d bytes, wrote %d",
		      MIDIEVENT_SIZE,
		      ret);
		return 1;
	}

	return 0;
}

static int
sequencer_noteevent(int eventbase, int channel, int note, int velocity)
{
	unsigned char midievent[MIDIEVENT_SIZE];

	sequencer_assert_range(channel,  0,  15);
	sequencer_assert_range(note,     0, 127);
	sequencer_assert_range(velocity, 0, 127);

	midievent[0] = (unsigned char) (eventbase + channel);
	midievent[1] = (unsigned char) note;
	midievent[2] = (unsigned char) velocity;

	return sequencer_send_midievent(midievent);
}

int
sequencer_noteon(int channel, int note, int velocity)
{
	return sequencer_noteevent(SEQUENCER_NOTEON_BASE,
				   channel,
				   note,
				   velocity);
}

int
sequencer_noteoff(int channel, int note, int velocity)
{
	return sequencer_noteevent(SEQUENCER_NOTEOFF_BASE,
				   channel,
				   note,
				   velocity);
}

void
sequencer_close(void)
{
	mio_close(mio);
}
