/* $Id: midi.c,v 1.4 2015/11/28 14:58:20 je Exp $ */

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

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <sndio.h>
#include <unistd.h>

#include "midi.h"
#include "util.h"

#define MIDI_EVENT_SIZE		3
#define MIDI_VELOCITY_MAX	127
#define MIDI_NOTEOFF_BASE	0x80
#define MIDI_NOTEON_BASE	0x90

static struct mio_hdl	*mio = NULL;

static int	midi_check_range(u_int8_t, u_int8_t, u_int8_t);

int
midi_open_device(void)
{
	if ((mio = mio_open(MIO_PORTANY, MIO_OUT, 0)) == NULL) {
		warnx("could not open midi device");
		return 1;
	}

	return 0;
}

void
midi_close_device(void)
{
	if (mio) {
		mio_close(mio);
		mio = NULL;
	}
}

int
midi_check_midievent(struct midievent me, float minimum_time_as_measures)
{
	if (!midi_check_range(me.eventtype, 0, EVENTTYPE_COUNT)) {
		warnx("midievent eventtype is invalid: %d", me.eventtype);
		return 0;
	}

	if (me.eventtype == SONG_END)
		return 1;

	if (!midi_check_range(me.channel, 0, MIDI_CHANNEL_MAX)) {
		warnx("midievent channel is invalid: %d", me.channel);
		return 0;
	}

	if (!midi_check_range(me.note, 0, MIDI_NOTE_MAX)) {
		warnx("midievent note is invalid: %d", me.note);
		return 0;
	}

	if (!midi_check_range(me.velocity, 0, MIDI_VELOCITY_MAX)) {
		warnx("midievent velocity is invalid: %d", me.velocity);
		return 0;
	}

	if (!isfinite(me.time_as_measures)) {
		warnx("time_as_measures is not a valid (finite) value");
		return 0;
	}

	if (me.time_as_measures < minimum_time_as_measures) {
		warnx("time is decreasing in eventstream");
		return 0;
	}

	return 1;
}

int
midi_send_midievent(struct midievent *me)
{
	u_int8_t midievent[MIDI_EVENT_SIZE];
	int ret;
	u_int8_t eventbase, velocity;

	assert(mio != NULL);

	switch (me->eventtype) {
	case NOTEON:
		eventbase = MIDI_NOTEON_BASE;
		velocity = me->velocity;
		break;
	case NOTEOFF:
		eventbase = MIDI_NOTEOFF_BASE;
		velocity = 0;
		break;
	default:
		assert(0);
	}

	midievent[0] = (u_int8_t) (eventbase + me->channel);
	midievent[1] = me->note;
	midievent[2] = velocity;

	ret = mio_write(mio, midievent, MIDI_EVENT_SIZE);
	if (ret != MIDI_EVENT_SIZE) {
		warnx("midi error, tried to write exactly %d bytes, wrote %d",
		      MIDI_EVENT_SIZE,
		      ret);
		return 1;
	}

	return 0;
}

static int
midi_check_range(u_int8_t value, u_int8_t min, u_int8_t max)
{
	return (min <= value && value <= max);
}

struct midieventstream *
midi_eventstream_new(void)
{
	struct midieventstream *midi_es;

        midi_es = malloc(sizeof(struct midieventstream));
        if (midi_es == NULL) {
                warn("malloc failure when creating midievent stream");
                return NULL;
        }

        midi_es->events = mdl_stream_init(&midi_es->params,
                                          sizeof(struct midievent));
        if (midi_es->events == NULL) {
		free(midi_es);
		return NULL;
	}

	return midi_es;
}

void
midi_eventstream_free(struct midieventstream *midi_es)
{
	mdl_stream_free(&midi_es->params, (void **) &midi_es->events);
	free(midi_es);
}

ssize_t
midi_write_midistream(int sequencer_socket, struct midieventstream *es)
{
	size_t wsize;
	ssize_t nw, total_wcount;

	total_wcount = 0;

	/* XXX overflow? */
	wsize = es->params.count * sizeof(struct midievent);

	while (total_wcount < wsize) {
		nw = write(sequencer_socket,
			   (char *) es->events + total_wcount,
			   wsize - total_wcount);
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to sequencer");
			return -1;
		}
		mdl_log(2, "wrote %ld bytes to sequencer\n", nw);
		total_wcount += nw;
	}

	return total_wcount;
}
