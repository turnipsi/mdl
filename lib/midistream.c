/* $Id: midistream.c,v 1.56 2016/08/23 20:22:58 je Exp $ */

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

#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "functions.h"
#include "joinexpr.h"
#include "midi.h"
#include "midistream.h"
#include "relative.h"
#include "util.h"

#define DEFAULT_VELOCITY	80

struct miditracks {
	struct instrument      *instrument;
	struct track	       *track;
	int			notecount[MIDI_NOTE_COUNT];
	int			total_notecount;
};

static struct mdl_stream *midi_mdlstream_new(void);
static struct mdl_stream *midistream_mdlstream_new(void);
static struct mdl_stream *midistream_to_midievents(struct mdl_stream *,
    float, int);

static struct mdl_stream *offsetexpr_mdlstream_new(void);
static struct mdl_stream *offsetexprstream_to_midievents(struct mdl_stream *,
    float, int);

static int	add_note_to_midistream(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int	add_tempochange_to_midistream(struct mdl_stream *,
    const struct tempochange *, float);
static int	add_volumechange_to_midistream(struct mdl_stream *,
    const struct volumechange *, float);

static int	add_noteoff_to_midievents(struct mdl_stream *,
    struct trackmidievent *, struct miditracks *, float, int);
static int	add_noteon_to_midievents(struct mdl_stream *,
    struct trackmidievent *, struct miditracks *, float, int);

static int	add_musicexpr_to_midistream(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int	compare_midievents(const struct midievent *,
    const struct midievent *);
static int	compare_midistreamevents(const void *, const void *);

struct mdl_stream *
_mdl_musicexpr_to_midievents(struct musicexpr *me, int level)
{
	struct mdl_stream *offset_es, *midi_es;
	struct musicexpr *flatme, *p;
	struct song *song;

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "converting music expression to midi stream\n");

	midi_es = NULL;
	flatme = NULL;

	if ((offset_es = offsetexpr_mdlstream_new()) == NULL) {
		warnx("could not setup new offsetexprstream");
		return NULL;
	}

	if (_mdl_functions_apply(me, level+1) != 0) {
		warnx("problem applying functions");
		_mdl_stream_free(offset_es);
		return NULL;
	}

	if ((song = _mdl_song_new()) == NULL) {
		warnx("could not create a new song");
		_mdl_stream_free(offset_es);
		return NULL;
	}

	if (_mdl_song_setup_tracks(song, me, level+1) != 0) {
		warnx("could not setup tracks for a new song");
		_mdl_stream_free(offset_es);
		_mdl_song_free(song);
		return NULL;
	}

	/*
	 * First convert relative->absolute,
	 * _mdl_joinexpr_musicexpr() can not handle relative expressions.
	 */
	_mdl_musicexpr_relative_to_absolute(song, me, level+1);

	_mdl_log(MDLLOG_MIDISTREAM, level, "joining music expressions\n");
	if (_mdl_joinexpr_musicexpr(me, level+1) != 0) {
		warnx("error occurred in joining music expressions");
		goto finish;
	}

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "converting expression to a (flat) simultence\n");
	flatme = _mdl_musicexpr_to_flat_simultence(me, level+1);
	if (flatme == NULL) {
		warnx("Could not flatten music expression to create offset"
		    " expression stream");
		goto finish;
	}

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "making offset expression stream\n");
	TAILQ_FOREACH(p, &flatme->u.flatsimultence.me->u.melist, tq) {
		assert(p->me_type == ME_TYPE_OFFSETEXPR);
		offset_es->u.mexprs[ offset_es->count ] = p->u.offsetexpr;
		if (_mdl_stream_increment(offset_es) != 0)
			goto finish;
	}

	midi_es = offsetexprstream_to_midievents(offset_es,
	    flatme->u.flatsimultence.length, level);

finish:
	_mdl_stream_free(offset_es);

	if (flatme != NULL)
		_mdl_musicexpr_free(flatme, level);

	return midi_es;
}

ssize_t
_mdl_midi_write_midistream(int sequencer_read_pipe, struct mdl_stream *s,
    int level)
{
	ssize_t nw, total_wcount, wsize;

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "writing midi stream to sequencer\n");

	level += 1;

	if ((SSIZE_MAX / sizeof(struct timed_midievent)) < s->count) {
		warnx("midistream size overflow, not writing anything");
		return -1;
	}

	total_wcount = 0;

	wsize = s->count * sizeof(struct timed_midievent);

	while (total_wcount < wsize) {
		nw = write(sequencer_read_pipe,
		    ((char *) s->u.timed_midievents + total_wcount),
		    (wsize - total_wcount));
		if (nw == -1) {
			if (errno == EINTR)
				continue;
			warn("error writing to sequencer");
			return -1;
		}
		_mdl_log(MDLLOG_MIDISTREAM, level,
		    "wrote %ld bytes to sequencer\n", nw);
		total_wcount += nw;
	}

	return total_wcount;
}

static struct mdl_stream *
midi_mdlstream_new(void)
{
	return _mdl_stream_new(MIDIEVENTS);
}

static struct mdl_stream *
midistream_mdlstream_new(void)
{
	return _mdl_stream_new(MIDISTREAMEVENTS);
}

static struct mdl_stream *
offsetexpr_mdlstream_new(void)
{
	return _mdl_stream_new(OFFSETEXPRS);
}

static struct mdl_stream *
offsetexprstream_to_midievents(struct mdl_stream *offset_es, float song_length,
    int level)
{
	struct mdl_stream *midi_es, *midistream_es;
	struct musicexpr *me;
	struct offsetexpr offsetexpr;
	float timeoffset;
	size_t i;
	int ret;

	assert(offset_es->s_type == OFFSETEXPRS);

	_mdl_log(MDLLOG_MIDISTREAM, level+1,
	    "offset expression stream to midi events\n");

	midi_es = NULL;
	midistream_es = NULL;

	if ((midistream_es = midistream_mdlstream_new()) == NULL)
		goto error;

	for (i = 0; i < offset_es->count; i++) {
		offsetexpr = offset_es->u.mexprs[i];
		me = offsetexpr.me;
		timeoffset = offsetexpr.offset;

		ret = add_musicexpr_to_midistream(midistream_es, me,
		    timeoffset, level+2);
		if (ret != 0)
			goto error;
	}

	qsort(midistream_es->u.timed_midievents, midistream_es->count,
	    sizeof(struct midistreamevent), compare_midistreamevents);

	midi_es = midistream_to_midievents(midistream_es, song_length,
	    level);
	if (midi_es == NULL)
		goto error;

	_mdl_stream_free(midistream_es);

	return midi_es;

error:
	warnx("could not convert offset-expression-stream to midi stream");
	if (midistream_es)
		_mdl_stream_free(midistream_es);
	if (midi_es)
		_mdl_stream_free(midi_es);

	return NULL;
}

static int
add_noteoff_to_midievents(struct mdl_stream *midi_es,
    struct trackmidievent *tme, struct miditracks *tracks,
    float time_as_measures, int level)
{
	struct timed_midievent *tmidiev;
	unsigned int ch;

	assert(midi_es->s_type == MIDIEVENTS);
	assert(tme->midiev.evtype == MIDIEV_NOTEOFF);

	for (ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
		if (tme->track == tracks[ch].track) {
			tracks[ch].notecount[ tme->midiev.u.midinote.note ] -=
			    1;
			tracks[ch].total_notecount -= 1;
			if (tracks[ch].total_notecount == 0)
				tracks[ch].track = NULL;
			break;
		}
	}
	assert(ch < MIDI_CHANNEL_COUNT);
	assert(tracks[ch].total_notecount >= 0);

	if (tracks[ch].notecount[tme->midiev.u.midinote.note] > 0) {
		/* This note must still be play, nothing to do. */
		return 0;
	}

	tme->midiev.u.midinote.channel = ch;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev = tme->midiev;

	_mdl_timed_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    tmidiev, level);

	return _mdl_stream_increment(midi_es);
}

static int
add_noteon_to_midievents(struct mdl_stream *midi_es,
    struct trackmidievent *tme, struct miditracks *tracks,
    float time_as_measures, int level)
{
	struct timed_midievent *tmidiev;
	unsigned int ch;
	int ret;

	assert(midi_es->s_type == MIDIEVENTS);
	assert(tme->midiev.evtype == MIDIEV_NOTEON);

	if (tme->track->autoallocate_channel) {
		for (ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
			/* Midi channel 10 (index 9) is reserved for drums. */
			if (ch == MIDI_DRUMCHANNEL)
				continue;
			if (tracks[ch].track == NULL)
				tracks[ch].track = tme->track;
			if (tracks[ch].track == tme->track)
				break;
		}
		if (ch == MIDI_CHANNEL_COUNT) {
			warnx("out of available midi tracks");
			return 1;
		}
		tme->midiev.u.midinote.channel = ch;
	} else {
		ch = tme->midiev.u.midinote.channel;
		tracks[ch].track = tme->track;
	}

	tracks[ch].notecount[ tme->midiev.u.midinote.note ] += 1;
	tracks[ch].total_notecount += 1;

	if (tracks[ch].notecount[ tme->midiev.u.midinote.note ] > 1) {
		/* This note is already playing, go to next event. */
		/* XXX Actually retriggering note would be better. */
		return 0;
	}

	if (tracks[ch].instrument != tme->track->instrument) {
		tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
		memset(tmidiev, 0, sizeof(struct timed_midievent));
		tmidiev->time_as_measures = time_as_measures;
		tmidiev->midiev.evtype = MIDIEV_INSTRUMENT_CHANGE;
		tmidiev->midiev.u.instr_change.channel = ch;
		tmidiev->midiev.u.instr_change.code =
		    tme->track->instrument->code;

		_mdl_timed_midievent_log(MDLLOG_MIDISTREAM,
		    "sending to sequencer", tmidiev, level);

		ret = _mdl_stream_increment(midi_es);
		if (ret != 0)
			return ret;
	}

	tracks[ch].instrument = tme->track->instrument;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev = tme->midiev;

	_mdl_timed_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    tmidiev, level);

	ret = _mdl_stream_increment(midi_es);
	if (ret != 0)
		return ret;

	return 0;
}

static struct mdl_stream *
midistream_to_midievents(struct mdl_stream *midistream_es, float song_length,
    int level)
{
	struct mdl_stream *midi_es;
	struct midistreamevent *mse;
	struct timed_midievent *tmidiev;
	struct miditracks tracks[MIDI_CHANNEL_COUNT];
	size_t i, j;
	int ch, ret;

	assert(midistream_es->s_type == MIDISTREAMEVENTS);

	mse = NULL;

	if ((midi_es = midi_mdlstream_new()) == NULL) {
		warn("could not create new midievent stream");
		return NULL;
	}

	for (i = 0; i < MIDI_CHANNEL_COUNT; i++) {
		tracks[i].instrument = NULL;
		tracks[i].track = NULL;
		for (j = 0; j < MIDI_NOTE_COUNT; j++)
			tracks[i].notecount[j] = 0;
		tracks[i].total_notecount = 0;
	}

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "adding midievents to send queue:\n");

	level += 1;

	for (i = 0; i < midistream_es->count; i++) {
		mse = &midistream_es->u.midistreamevents[i];

		switch (mse->evtype) {
		case MIDISTREV_NOTEOFF:
			ret = add_noteoff_to_midievents(midi_es, &mse->u.tme,
			    tracks, mse->time_as_measures, level);
			if (ret != 0)
				goto error;
			break;
		case MIDISTREV_NOTEON:
			ret = add_noteon_to_midievents(midi_es, &mse->u.tme,
			    tracks, mse->time_as_measures, level);
			if (ret != 0)
				goto error;
			break;
		case MIDISTREV_TEMPOCHANGE:
			tmidiev = &midi_es->u.timed_midievents[
			    midi_es->count ];
			memset(tmidiev, 0, sizeof(struct timed_midievent));
			tmidiev->time_as_measures = mse->time_as_measures;
			tmidiev->midiev.evtype = MIDIEV_TEMPOCHANGE;
			tmidiev->midiev.u.bpm = mse->u.bpm;
			_mdl_timed_midievent_log(MDLLOG_MIDISTREAM,
			    "sending to sequencer", tmidiev, level);
			if ((ret = _mdl_stream_increment(midi_es)) != 0)
				goto error;
			break;
		case MIDISTREV_VOLUMECHANGE:
			assert(mse->u.tme.midiev.evtype
			    == MIDIEV_VOLUMECHANGE);

			/* XXX should try to find the correct midi channel
			 * XXX based on track information */
			ch = 0;

			tmidiev = &midi_es->u.timed_midievents[
			    midi_es->count ];
			memset(tmidiev, 0, sizeof(struct timed_midievent));
			tmidiev->time_as_measures = mse->time_as_measures;
			tmidiev->midiev = mse->u.tme.midiev;
			tmidiev->midiev.u.volumechange.channel = ch;
			_mdl_timed_midievent_log(MDLLOG_MIDISTREAM,
			    "sending to sequencer", tmidiev, level);
			if ((ret = _mdl_stream_increment(midi_es)) != 0)
				goto error;
			break;
		default:
			assert(0);
		}
	}

	for (i = 0; i < MIDI_CHANNEL_COUNT; i++)
		assert(tracks[i].total_notecount == 0);

	assert(song_length >= 0.0);
	assert(mse == NULL || song_length >= mse->time_as_measures);

	/* Add SONG_END midievent. */
	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = song_length;
	tmidiev->midiev.evtype = MIDIEV_SONG_END;

	_mdl_timed_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    tmidiev, level);

	if ((ret = _mdl_stream_increment(midi_es)) != 0)
		goto error;

	return midi_es;

error:
	_mdl_stream_free(midi_es);
	return NULL;
}

static int
add_musicexpr_to_midistream(struct mdl_stream *midistream_es,
    const struct musicexpr *me, float timeoffset, int level)
{
	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "adding expression with offset %.3f to trackmidievents\n",
	    timeoffset);
	_mdl_musicexpr_log(me, MDLLOG_MIDISTREAM, level+1, NULL);

	level += 1;

	assert(me->me_type == ME_TYPE_ABSDRUM ||
	    me->me_type == ME_TYPE_ABSNOTE ||
	    me->me_type == ME_TYPE_TEMPOCHANGE ||
	    me->me_type == ME_TYPE_VOLUMECHANGE);

	if (me->me_type == ME_TYPE_TEMPOCHANGE)
		return add_tempochange_to_midistream(midistream_es,
		    &me->u.tempochange, timeoffset);

	if (me->me_type == ME_TYPE_VOLUMECHANGE)
		return add_volumechange_to_midistream(midistream_es,
		    &me->u.volumechange, timeoffset);

	return add_note_to_midistream(midistream_es, me, timeoffset, level);
}

static int
add_tempochange_to_midistream(struct mdl_stream *midistream_es,
    const struct tempochange *tempochg, float timeoffset)
{
	struct midistreamevent *mse;

	assert(midistream_es->s_type == MIDISTREAMEVENTS);

	mse = &midistream_es->u.midistreamevents[ midistream_es->count ];
	memset(mse, 0, sizeof(struct midistreamevent));
	mse->evtype = MIDISTREV_TEMPOCHANGE;
	mse->time_as_measures = timeoffset;
	mse->u.bpm = tempochg->bpm;

	return _mdl_stream_increment(midistream_es);
}

static int
add_volumechange_to_midistream(struct mdl_stream *midistream_es,
    const struct volumechange *volumechg, float timeoffset)
{
	struct midistreamevent *mse;
	struct trackmidievent *tme;

	assert(midistream_es->s_type == MIDISTREAMEVENTS);

	mse = &midistream_es->u.midistreamevents[ midistream_es->count ];
	memset(mse, 0, sizeof(struct midistreamevent));
	mse->evtype = MIDISTREV_VOLUMECHANGE;
	mse->time_as_measures = timeoffset;
	tme = &mse->u.tme;
	tme->midiev.evtype = MIDIEV_VOLUMECHANGE;
	tme->midiev.u.volumechange.channel = volumechg->track->midichannel;
	tme->midiev.u.volumechange.volume = MIN(127, 127 * volumechg->volume);
	tme->track = volumechg->track;

	return _mdl_stream_increment(midistream_es);
}

static int
add_note_to_midistream(struct mdl_stream *midistream_es,
    const struct musicexpr *me, float timeoffset, int level)
{
	struct midistreamevent *mse;
	struct trackmidievent *tme;
	int new_note, ret;
	float length;

	assert(midistream_es->s_type == MIDISTREAMEVENTS);

	switch (me->me_type) {
	case ME_TYPE_ABSDRUM:
		new_note = me->u.absdrum.note;
		length = me->u.absdrum.length;
		break;
	case ME_TYPE_ABSNOTE:
		new_note = me->u.absnote.note;
		length = me->u.absnote.length;
		break;
	default:
		assert(0);
	}

	/* We accept and ignore notes that are out-of-range. */
	if (new_note < 0 || MIDI_NOTE_COUNT <= new_note) {
		_mdl_log(MDLLOG_MIDISTREAM, level,
		    "skipping note with value %d\n", new_note);
		return 0;
	}
	assert(length > 0.0);

	/*
	 * Ignore notes that are less than MINIMUM_MUSICEXPR_LENGTH.
	 * Notes that have zero length trigger issues after midi events
	 * are sorted.
	 */
	if (length < MINIMUM_MUSICEXPR_LENGTH) {
		_mdl_log(MDLLOG_MIDISTREAM, level,
		    "skipping note with length %.9f\n", length);
		return 0;
	}

	mse = &midistream_es->u.midistreamevents[ midistream_es->count ];
	memset(mse, 0, sizeof(struct midistreamevent));
	mse->evtype = MIDISTREV_NOTEON;
	mse->time_as_measures = timeoffset;
	tme = &mse->u.tme;
	tme->midiev.evtype = MIDIEV_NOTEON;
	tme->midiev.u.midinote.note = new_note;
	tme->midiev.u.midinote.velocity = DEFAULT_VELOCITY;

	if (me->me_type == ME_TYPE_ABSDRUM) {
		tme->track = me->u.absdrum.track;
		/* XXX ? tme->instrument = me->u.absdrum.instrument; */
		tme->midiev.u.midinote.channel = tme->track->midichannel;
	} else if (me->me_type == ME_TYPE_ABSNOTE) {
		tme->track = me->u.absnote.track;
		/* XXX ? tme->instrument = me->u.absnote.instrument; */
		tme->midiev.u.midinote.channel = tme->track->midichannel;
	} else {
		assert(0);
	}

	ret = _mdl_stream_increment(midistream_es);
	if (ret != 0)
		return ret;

	mse = &midistream_es->u.midistreamevents[ midistream_es->count ];
	memset(mse, 0, sizeof(struct midistreamevent));
	mse->evtype = MIDISTREV_NOTEOFF;
	mse->time_as_measures = timeoffset + me->u.absnote.length;
	tme = &mse->u.tme;
	tme->midiev.evtype = MIDIEV_NOTEOFF;
	tme->midiev.u.midinote.note = new_note;
	tme->midiev.u.midinote.velocity = 0;

	if (me->me_type == ME_TYPE_ABSDRUM) {
		tme->track = me->u.absdrum.track;
		/* XXX ? tme->instrument = me->u.absdrum.instrument; */
		tme->midiev.u.midinote.channel = tme->track->midichannel;
	} else if (me->me_type == ME_TYPE_ABSNOTE) {
		tme->track = me->u.absnote.track;
		/* XXX ? tme->instrument = me->u.absnote.instrument; */
		tme->midiev.u.midinote.channel = tme->track->midichannel;
	} else {
		assert(0);
	}

	return _mdl_stream_increment(midistream_es);
}

static int
compare_midistreamevents(const void *va, const void *vb)
{
	const struct midistreamevent *a, *b;
	int ret;

	a = va;
	b = vb;

	assert(a->evtype < MIDISTREV_TYPECOUNT);
	assert(b->evtype < MIDISTREV_TYPECOUNT);

	ret = (a->time_as_measures < b->time_as_measures) ? -1 :
	      (a->time_as_measures > b->time_as_measures) ?  1 :
	      (a->evtype           < b->evtype)           ? -1 :
	      (a->evtype           > b->evtype)           ?  1 : 0;

	if (ret != 0)
		return ret;

	assert(a->evtype == b->evtype);
	switch (a->evtype) {
	case MIDISTREV_NOTEOFF:
	case MIDISTREV_NOTEON:
	case MIDISTREV_VOLUMECHANGE:
		return compare_midievents(&a->u.tme.midiev, &b->u.tme.midiev);
	case MIDISTREV_TEMPOCHANGE:
		return (a->u.bpm < b->u.bpm) ? -1 :
		       (a->u.bpm > b->u.bpm) ?  1 : 0;
	default:
		assert(0);
	}

	return 0;
}

static int
compare_midievents(const struct midievent *a, const struct midievent *b)
{
	const struct midi_volumechange *vg_a, *vg_b;

	/* Provides order only for a subset of midievents. */

	assert(a->evtype == MIDIEV_NOTEOFF || a->evtype == MIDIEV_NOTEON ||
	    a->evtype == MIDIEV_VOLUMECHANGE);
	assert(a->evtype == b->evtype);

	if (a->evtype == MIDIEV_VOLUMECHANGE) {
		vg_a = &a->u.volumechange;
		vg_b = &b->u.volumechange;
		return (vg_a->channel < vg_b->channel) ? -1 :
		       (vg_a->channel > vg_b->channel) ?  1 :
		       (vg_a->volume  < vg_a->volume)  ? -1 :
		       (vg_a->volume  > vg_a->volume)  ?  1 : 0;
	}

	return
	    (a->u.midinote.channel  < b->u.midinote.channel)  ? -1 :
	    (a->u.midinote.channel  > b->u.midinote.channel)  ?  1 :
	    (a->u.midinote.note     < b->u.midinote.note)     ? -1 :
	    (a->u.midinote.note     > b->u.midinote.note)     ?  1 :
	    (a->u.midinote.velocity < b->u.midinote.velocity) ? -1 :
	    (a->u.midinote.velocity > b->u.midinote.velocity) ?  1 : 0;
}
