/* $Id: sequencer.c,v 1.7 2015/10/08 19:36:29 je Exp $

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

/* XXX */
#include <stdio.h>
#include <unistd.h>

#include "sequencer.h"

#define MIDIEVENT_SIZE		3
#define SEQ_CHANNEL_MAX		15
#define SEQ_NOTE_MAX		127
#define SEQ_VELOCITY_MAX	127
#define SEQ_NOTEOFF_BASE	0x80
#define SEQ_NOTEON_BASE		0x90

struct notestate {
	unsigned int state    : 1;
	unsigned int velocity : 7;
};

static struct notestate
  sequencer_notestates[SEQ_CHANNEL_MAX+1][SEQ_NOTE_MAX+1];

static struct mio_hdl	*mio = NULL;

static void	sequencer_assert_range(int, int, int);
static void	sequencer_close(void);
static int	sequencer_init(void);
static int	sequencer_noteevent(int, int, int, int);
static int	sequencer_noteoff(int, int);
static int	sequencer_noteon(int, int, int);
static int	sequencer_send_midievent(const unsigned char *);

static int
sequencer_init(void)
{
	if ((mio = mio_open(MIO_PORTANY, MIO_OUT, 0)) == NULL) {
		warnx("could not open midi device");
		return 1;
	}

	return 0;
}

int
sequencer_loop(int input_socket)
{
	if (sequencer_init() != 0) {
		warnx("problem initializing sequencer, exiting");
		return 1;
	}

	/* XXX handle stuff, input socket should give us music to play */
	printf("in sequencer\n");
	fflush(stdout);
	sleep(30);

	sequencer_close();

	return 0;
}

static void
sequencer_assert_range(int channel, int min, int max)
{
	assert(min <= channel && channel <= max);
}

static int
sequencer_send_midievent(const unsigned char *midievent)
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
sequencer_noteevent(int note_on, int channel, int note, int velocity)
{
	unsigned char midievent[MIDIEVENT_SIZE];
	int ret;
	unsigned char eventbase;

	sequencer_assert_range(channel,  0, SEQ_CHANNEL_MAX);
	sequencer_assert_range(note,     0, SEQ_NOTE_MAX);
	sequencer_assert_range(velocity, 0, SEQ_VELOCITY_MAX);

	eventbase = note_on ? SEQ_NOTEON_BASE : SEQ_NOTEOFF_BASE;

	midievent[0] = (unsigned char) (eventbase + channel);
	midievent[1] = (unsigned char) note;
	midievent[2] = (unsigned char) velocity;

	ret = sequencer_send_midievent(midievent);
	if (ret == 0) {
		sequencer_notestates[channel][note].state    = note_on ? 1 : 0;
		sequencer_notestates[channel][note].velocity = velocity;
	}

	return ret;
}

static int
sequencer_noteon(int channel, int note, int velocity)
{
	return sequencer_noteevent(1, channel, note, velocity);
}

static int
sequencer_noteoff(int channel, int note)
{
	return sequencer_noteevent(0, channel, note, 0);
}

static void
sequencer_close(void)
{
	int c, n;

	for (c = 0; c < SEQ_CHANNEL_MAX; c++)
		for (n = 0; n < SEQ_NOTE_MAX; n++)
			if (sequencer_notestates[c][n].state)
				if (sequencer_noteoff(c, n) != 0)
					warnx("error in turning off note"
						" %d on channel %d", n, c);

	mio_close(mio);
}
