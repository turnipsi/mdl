/* $Id: midi.c,v 1.45 2016/09/29 20:02:50 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_SNDIO
#include <sndio.h>
#endif /* HAVE_SNDIO */

#include "midi.h"
#include "util.h"

#define MIDI_EVENT_MAXSIZE		3
#define MIDI_CONTROLCHANGE_BASE		0xb0
#define MIDI_INSTRUMENT_CHANGE_BASE	0xc0
#define MIDI_INSTRUMENT_MAX		127
#define MIDI_NOTEOFF_BASE		0x80
#define MIDI_NOTEON_BASE		0x90
#define MIDI_VELOCITY_MAX		127
#define MIDI_VOLUME_MAX			127

#define MIDICC_CHANNEL_VOLUME		7

struct mididevice {
	enum mididev_type mididev_type;
	union {
		int		raw_fd;
		struct mio_hdl *sndio_mio;
	} u;
	size_t	(*write_to_device)(u_int8_t *, size_t);
	void	(*close_device)(void);
};

struct mididevice mididev = { MIDIDEV_NONE, { 0 }, NULL, NULL };

static int midi_check_range(u_int8_t, u_int8_t, u_int8_t);

static int	raw_open_device(const char *);
static size_t	raw_write_to_device(u_int8_t *, size_t);
static void	raw_close_device(void);

#ifdef HAVE_SNDIO
static int	sndio_open_device(const char *);
static size_t	sndio_write_to_device(u_int8_t *, size_t);
static void	sndio_close_device(void);
#endif

static void	maybe_log_the_clock(int);

int
_mdl_midi_open_device(enum mididev_type mididev_type, const char *device)
{
	switch (mididev_type) {
	case MIDIDEV_NONE:
		assert(0);
		break;
	case MIDIDEV_RAW:
		return raw_open_device(device);
	case MIDIDEV_SNDIO:
#ifdef HAVE_SNDIO
		return sndio_open_device(device);
#else
		warnx("sndio support not compiled in");
		return 1;
#endif
	default:
		assert(0);
	}

	return 1;
}

void
_mdl_midi_close_device(void)
{
	mididev.close_device();
	mididev.mididev_type = MIDIDEV_NONE;
}

static int
raw_open_device(const char *device)
{
	int fd;
	const char *devpath;

	assert(mididev.mididev_type == MIDIDEV_NONE);

	devpath = (device != NULL) ? device : "/dev/rmidi0";

	if ((fd = open(devpath, O_WRONLY)) == -1) {
		warn("could not open raw midi device %s", devpath);
		return 1;
	}

	mididev.mididev_type = MIDIDEV_RAW;
	mididev.u.raw_fd = fd;
	mididev.write_to_device = raw_write_to_device;
	mididev.close_device = raw_close_device;

	return 0;
}

static size_t
raw_write_to_device(u_int8_t *midievent, size_t midievent_size)
{
	size_t total_wcount;
	ssize_t nw;

	assert(mididev.mididev_type == MIDIDEV_RAW);

	total_wcount = 0;

	while (total_wcount < midievent_size) {
		/* XXX what if nw == 0 (continuously)?  can that happen? */
		nw = write(mididev.u.raw_fd, midievent,
		    midievent_size-total_wcount);
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to raw midi device");
			return 0;
		}
		total_wcount += nw;
	}

	return total_wcount;
}

static void
raw_close_device(void)
{
	assert(mididev.mididev_type == MIDIDEV_RAW);

	if (close(mididev.u.raw_fd) == -1)
		warn("error closing raw midi device");
}

#ifdef HAVE_SNDIO

static int
sndio_open_device(const char *device)
{
	struct mio_hdl *mio;
	const char *sndio_device;

	assert(mididev.mididev_type == MIDIDEV_NONE);

	sndio_device = (device != NULL) ? device : MIO_PORTANY;

	if ((mio = mio_open(sndio_device, MIO_OUT, 0)) == NULL) {
		warnx("could not open sndio midi device %s", sndio_device);
		return 1;
	}

	mididev.mididev_type = MIDIDEV_SNDIO;
	mididev.u.sndio_mio = mio;
	mididev.write_to_device = sndio_write_to_device;
	mididev.close_device = sndio_close_device;

	return 0;
}

static size_t
sndio_write_to_device(u_int8_t *midievent, size_t midievent_size)
{
	assert(mididev.mididev_type == MIDIDEV_SNDIO);

	return mio_write(mididev.u.sndio_mio, midievent, midievent_size);
}

static void
sndio_close_device(void)
{
	assert(mididev.mididev_type == MIDIDEV_SNDIO);

	mio_close(mididev.u.sndio_mio);
}

#endif /* HAVE_SNDIO */

int
_mdl_midi_check_timed_midievent(struct timed_midievent tme,
    float minimum_time_as_measures)
{
	struct midievent *me;
	int ret;

	me = &tme.midiev;

	ret = midi_check_range(me->evtype, 0, MIDIEV_TYPECOUNT-1);
	if (!ret) {
		warnx("midievent eventtype is invalid: %d", me->evtype);
		return 0;
	}

	if (tme.time_as_measures < minimum_time_as_measures) {
		warnx("time is decreasing in eventstream (%f < %f)",
		    tme.time_as_measures, minimum_time_as_measures);
		return 0;
	}

	if (!isfinite(tme.time_as_measures)) {
		warnx("time_as_measures is not finite");
		return 0;
	}

	switch (me->evtype) {
	case MIDIEV_INSTRUMENT_CHANGE:
		ret = midi_check_range(me->u.instr_change.code, 0,
		    MIDI_INSTRUMENT_MAX);
		if (!ret) {
			warnx("instrument code is invalid: %d",
			    me->u.instr_change.code);
			return 0;
		}

		ret = midi_check_range(me->u.instr_change.channel, 0,
		    MIDI_CHANNEL_COUNT-1);
		if (!ret) {
			warnx("midievent channel is invalid: %d",
			    me->u.instr_change.channel);
			return 0;
		}

		return 1;
	case MIDIEV_MARKER:
		return 1;
	case MIDIEV_NOTEOFF:
	case MIDIEV_NOTEON:
		ret = midi_check_range(me->u.midinote.channel, 0,
		    MIDI_CHANNEL_COUNT-1);
		if (!ret) {
			warnx("midievent channel is invalid: %d",
			    me->u.midinote.channel);
			return 0;
		}

		ret = midi_check_range(me->u.midinote.note, 0,
		    MIDI_NOTE_COUNT-1);
		if (!ret) {
			warnx("midievent note is invalid: %d",
			    me->u.midinote.note);
			return 0;
		}

		ret = midi_check_range(me->u.midinote.velocity, 0,
		    MIDI_VELOCITY_MAX);
		if (!ret) {
			warnx("midievent velocity is invalid: %d",
			    me->u.midinote.velocity);
			return 0;
		}

		if (!(me->u.midinote.joining == 0
		    || me->u.midinote.joining == 1)) {
			warnx("joining marker not either 0 or 1");
			return 0;
		}

		return 1;
	case MIDIEV_SONG_END:
		return 1;
	case MIDIEV_TEMPOCHANGE:
		if (me->u.bpm < 0) {
			warnx("tempochange bpm is negative");
			return 0;
		}
		if (!isfinite(me->u.bpm)) {
			warnx("tempochange bpm is not finite");
			return 0;
		}
		return 1;
	case MIDIEV_VOLUMECHANGE:
		ret = midi_check_range(me->u.volumechange.channel, 0,
		    MIDI_CHANNEL_COUNT-1);
		if (!ret) {
			warnx("midievent channel is invalid: %d",
			    me->u.volumechange.channel);
			return 0;
		}
		ret = midi_check_range(me->u.volumechange.volume, 0,
		    MIDI_VOLUME_MAX);
		if (!ret) {
			warnx("midievent volume is invalid: %d",
			    me->u.volumechange.volume);
			return 0;
		}

		return 1;
	default:
		assert(0);
	}

	return 0;
}

int
_mdl_midi_play_midievent(struct midievent *me, int level, int dry_run)
{
	u_int8_t midievent[MIDI_EVENT_MAXSIZE];
	size_t midievent_size, wsize;
	u_int8_t eventbase, velocity;

	midievent_size = 0;

	switch (me->evtype) {
	case MIDIEV_INSTRUMENT_CHANGE:
		_mdl_log(MDLLOG_MIDI, level,
		    "playing instrumentchange: channel=%d code=%d\n",
		    me->u.instr_change.channel, me->u.instr_change.code);
		maybe_log_the_clock(level+1);

		midievent_size = 2;
		midievent[0] = (u_int8_t) (MIDI_INSTRUMENT_CHANGE_BASE +
		    me->u.instr_change.channel);
		midievent[1] = me->u.instr_change.code;
		break;
	case MIDIEV_NOTEON:
	case MIDIEV_NOTEOFF:
		midievent_size = 3;
		eventbase = (me->evtype == MIDIEV_NOTEON)
			        ? MIDI_NOTEON_BASE
				: MIDI_NOTEOFF_BASE;
		velocity = (me->evtype == MIDIEV_NOTEON)
		    ? me->u.midinote.velocity : 0;

		_mdl_log(MDLLOG_MIDI, level,
		    "playing %s: notevalue=%d channel=%d velocity=%d\n",
		    (me->evtype == MIDIEV_NOTEON ? "noteon" : "noteoff"),
		    me->u.midinote.note, me->u.midinote.channel, velocity);
		maybe_log_the_clock(level+1);

		midievent[0] = (u_int8_t) (eventbase + me->u.midinote.channel);
		midievent[1] = me->u.midinote.note;
		midievent[2] = velocity;
		break;
	case MIDIEV_VOLUMECHANGE:
		_mdl_log(MDLLOG_MIDI, level,
		    "playing volumechange: channel=%d volume=%d\n",
		    me->u.volumechange.channel, me->u.volumechange.volume);
		maybe_log_the_clock(level+1);

		midievent_size = 3;
		midievent[0] = (u_int8_t) (MIDI_CONTROLCHANGE_BASE +
		    me->u.volumechange.channel);
		midievent[1] = MIDICC_CHANNEL_VOLUME;
		midievent[2] = me->u.volumechange.volume;
		break;
	default:
		assert(0);
	}

	/* Do not actually send any midi event when dry_run is set. */
	if (dry_run)
		return 0;

	wsize = mididev.write_to_device(midievent, midievent_size);
	if (wsize != midievent_size) {
		warnx("midi error, tried to write exactly %ld bytes,"
		    " wrote %ld", midievent_size, wsize);
		return 1;
	}

	return 0;
}

static void
maybe_log_the_clock(int level)
{
	struct timespec time;

	if (_mdl_log_checkopt(MDLLOG_CLOCK)) {
		if (clock_gettime(CLOCK_REALTIME, &time) == -1) {
			warn("could not get real time");
		} else {
			_mdl_log(MDLLOG_CLOCK, level, "clock=%d.%.0f\n",
			    time.tv_sec, (time.tv_nsec / 1000000.0));
		}
	}
}

static int
midi_check_range(u_int8_t value, u_int8_t min, u_int8_t max)
{
	return (min <= value && value <= max);
}

void
_mdl_timed_midievent_log(enum logtype logtype, const char *prefix,
    struct timed_midievent *tme, int level)
{
	struct midievent *me;

	me = &tme->midiev;

	switch (me->evtype) {
	case MIDIEV_INSTRUMENT_CHANGE:
		_mdl_log(logtype, level,
		    "%s instrument change time=%.3f channel=%d"
		    " instrument=%d\n", prefix, tme->time_as_measures,
		    me->u.instr_change.channel,
		    me->u.instr_change.code);
		break;
	case MIDIEV_MARKER:
		_mdl_log(logtype, level, "%s marker time=%.3f\n",
		    prefix, tme->time_as_measures);
		break;
	case MIDIEV_NOTEOFF:
	case MIDIEV_NOTEON:
		_mdl_log(logtype, level,
		    "%s %s time=%.3f channel=%d note=%d velocity=%d\n",
		    prefix,
		    (me->evtype == MIDIEV_NOTEOFF ? "noteoff" : "noteon"),
		    tme->time_as_measures, me->u.midinote.channel,
		    me->u.midinote.note, me->u.midinote.velocity);
		break;
	case MIDIEV_SONG_END:
		_mdl_log(logtype, level, "%s song end time=%.3f\n", prefix,
		    tme->time_as_measures);
		break;
	case MIDIEV_TEMPOCHANGE:
		_mdl_log(logtype, level,
		    "%s tempochange time=%.3f bpm=%.3f\n", prefix,
		    tme->time_as_measures, me->u.bpm);
		break;
	case MIDIEV_VOLUMECHANGE:
		_mdl_log(logtype, level,
		    "%s volumechange time=%.3f channel=%d volume=%d\n", prefix,
		    tme->time_as_measures, me->u.volumechange.channel,
		    me->u.volumechange.volume);
		break;
	default:
		assert(0);
	}
}

enum mididev_type
_mdl_midi_get_mididev_type(const char *miditype)
{
	enum mididev_type mididev_type;

	if (strcmp(miditype, "raw") == 0) {
		mididev_type = MIDIDEV_RAW;
#ifdef HAVE_SNDIO
	} else if (strcmp(miditype, "sndio") == 0) {
		mididev_type = MIDIDEV_SNDIO;
#endif /* HAVE_SNDIO */
	} else {
		warnx("unsupported midi interface \"%s\"", miditype);
		warnx("run with -v to see possible options");
		mididev_type = MIDIDEV_NONE;
	}

	return mididev_type;
}
