/* $Id: midi.c,v 1.17 2016/02/24 20:29:08 je Exp $ */

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
#include <math.h>
#include <sndio.h>
#include <time.h>

#include "midi.h"
#include "util.h"

#define MIDI_EVENT_MAXSIZE		3
#define MIDI_INSTRUMENT_CHANGE_BASE	0xc0
#define MIDI_INSTRUMENT_MAX		127
#define MIDI_NOTEOFF_BASE		0x80
#define MIDI_NOTEON_BASE		0x90
#define MIDI_VELOCITY_MAX		127

static struct mio_hdl *mio = NULL;

static int midi_check_range(u_int8_t, u_int8_t, u_int8_t);

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
	int ret;

	ret = midi_check_range(me.eventtype, 0, EVENTTYPE_COUNT);
	if (!ret) {
		warnx("midievent eventtype is invalid: %d", me.eventtype);
		return 0;
	}

	if (me.time_as_measures < minimum_time_as_measures) {
		warnx("time is decreasing in eventstream (%f < %f)",
		    me.time_as_measures, minimum_time_as_measures);
		return 0;
	}

	switch (me.eventtype) {
	case INSTRUMENT_CHANGE:
		ret = midi_check_range(me.u.instrument_change.code, 0,
		    MIDI_INSTRUMENT_MAX);
		if (!ret) {
			warnx("instrument code is invalid: %d",
			    me.u.instrument_change.code);
			return 0;
		}

		ret = midi_check_range(me.u.instrument_change.channel, 0,
		    MIDI_CHANNEL_COUNT-1);
		if (!ret) {
			warnx("midievent channel is invalid: %d",
			    me.u.instrument_change.channel);
			return 0;
		}

		return 1;
	case NOTEOFF:
	case NOTEON:
		ret = midi_check_range(me.u.note.channel, 0,
		    MIDI_CHANNEL_COUNT-1);
		if (!ret) {
			warnx("midievent channel is invalid: %d",
			    me.u.note.channel);
			return 0;
		}

		ret = midi_check_range(me.u.note.note, 0, MIDI_NOTE_COUNT-1);
		if (!ret) {
			warnx("midievent note is invalid: %d", me.u.note.note);
			return 0;
		}

		ret = midi_check_range(me.u.note.velocity, 0,
		    MIDI_VELOCITY_MAX);
		if (!ret) {
			warnx("midievent velocity is invalid: %d",
			    me.u.note.velocity);
			return 0;
		}

		if (!isfinite(me.time_as_measures)) {
			warnx("time_as_measures is not a valid (finite)");
			return 0;
		}

		return 1;
	case SONG_END:
		return 1;
	default:
		assert(0);
	}

	return 0;
}

int
midi_send_midievent(struct midievent *me)
{
	struct timespec time;
	u_int8_t midievent[MIDI_EVENT_MAXSIZE];
	int ret, midievent_size;
	u_int8_t eventbase, velocity;

	assert(mio != NULL);

	midievent_size = 0;

	if (clock_gettime(CLOCK_REALTIME, &time) == -1) {
		warn("could not get real time");
		time.tv_sec = 0;
		time.tv_nsec = 0;
	}

	switch (me->eventtype) {
	case INSTRUMENT_CHANGE:
		midievent_size = 2;
		midievent[0] = (u_int8_t) (MIDI_INSTRUMENT_CHANGE_BASE +
		    me->u.instrument_change.channel);
		midievent[1] = me->u.instrument_change.code;
		break;
	case NOTEON:
	case NOTEOFF:
		midievent_size = 3;
		eventbase = (me->eventtype == NOTEON)
			        ? MIDI_NOTEON_BASE
				: MIDI_NOTEOFF_BASE;
		velocity = (me->eventtype == NOTEON) ? me->u.note.velocity : 0;

		mdl_log(MDLLOG_MIDI, 0,
		    "sending \"%s\": notevalue=%d channel=%d"
		    " velocity=%d clock=%d.%.0f\n",
		    (me->eventtype == NOTEON ? "note on" : "note off"),
		    me->u.note.note, me->u.note.channel, velocity,
		    time.tv_sec, (time.tv_nsec / 1000000.0));

		midievent[0] = (u_int8_t) (eventbase + me->u.note.channel);
		midievent[1] = me->u.note.note;
		midievent[2] = velocity;
		break;
	default:
		assert(0);
	}

	ret = mio_write(mio, midievent, midievent_size);
	if (ret != midievent_size) {
		warnx("midi error, tried to write exactly %d bytes, wrote %d",
		    midievent_size, ret);
		return 1;
	}

	return 0;
}

static int
midi_check_range(u_int8_t value, u_int8_t min, u_int8_t max)
{
	return (min <= value && value <= max);
}

void
midievent_log(const char *prefix, struct midievent *midievent, int level)
{
	switch (midievent->eventtype) {
	case INSTRUMENT_CHANGE:
		mdl_log(MDLLOG_MIDI, level,
		    "%s instrument change time=%.3f channel=%d"
		    " instrument=%d\n", prefix, midievent->time_as_measures,
		    midievent->u.instrument_change.channel,
		    midievent->u.instrument_change.code);
		break;
	case NOTEOFF:
	case NOTEON:
		mdl_log(MDLLOG_MIDI, level,
		    "%s %s time=%.3f channel=%d note=%d velocity=%d\n",
		    prefix,
		    (midievent->eventtype == NOTEOFF ? "noteoff" : "noteon"),
		    midievent->time_as_measures, midievent->u.note.channel,
		    midievent->u.note.note, midievent->u.note.velocity);
		break;
	case SONG_END:
		mdl_log(MDLLOG_MIDI, level, "%s song end time=%.3f\n", prefix,
		    midievent->time_as_measures);
		break;
	default:
		assert(0);
	}
}
