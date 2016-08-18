/* $Id: midistream.c,v 1.51 2016/08/18 20:03:56 je Exp $ */

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

#define DEFAULT_MIDICHANNEL		0
#define DEFAULT_VELOCITY		80
#define DRUM_MIDICHANNEL		9

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

static int	add_noteoff_to_midievents(struct mdl_stream *,
    struct trackmidinote *, struct miditracks *, float, int);
static int	add_noteon_to_midievents(struct mdl_stream *,
    struct trackmidinote *, struct miditracks *, float, int);

static int	add_musicexpr_to_midistream(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int	compare_midistreamevents(const void *, const void *);
static int	compare_trackmidinotes(const struct trackmidinote *,
    const struct trackmidinote *);

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

	if ((SSIZE_MAX / sizeof(struct midievent)) < s->count) {
		warnx("midistream size overflow, not writing anything");
		return -1;
	}

	total_wcount = 0;

	wsize = s->count * sizeof(struct midievent);

	while (total_wcount < wsize) {
		nw = write(sequencer_read_pipe,
		    ((char *) s->u.midievents + total_wcount),
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

	qsort(midistream_es->u.midievents, midistream_es->count,
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
    struct trackmidinote *tmn, struct miditracks *tracks,
    float time_as_measures, int level)
{
	struct midievent *midievent;
	unsigned int ch;

	assert(midi_es->s_type == MIDIEVENTS);

	for (ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
		if (tmn->track == tracks[ch].track) {
			tracks[ch].notecount[ tmn->note.note ] -= 1;
			tracks[ch].total_notecount -= 1;
			if (tracks[ch].total_notecount == 0)
				tracks[ch].track = NULL;
			break;
		}
	}
	assert(ch < MIDI_CHANNEL_COUNT);
	assert(tracks[ch].total_notecount >= 0);

	if (tracks[ch].notecount[tmn->note.note] > 0) {
		/* This note must still be play, nothing to do. */
		return 0;
	}

	tmn->note.channel = ch;

	midievent = &midi_es->u.midievents[ midi_es->count ];
	memset(midievent, 0, sizeof(struct midievent));
	midievent->evtype = MIDIEV_NOTEOFF;
	midievent->time_as_measures = time_as_measures;
	midievent->u.note = tmn->note;

	_mdl_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    midievent, level);

	return _mdl_stream_increment(midi_es);
}

static int
add_noteon_to_midievents(struct mdl_stream *midi_es,
    struct trackmidinote *tmn, struct miditracks *tracks,
    float time_as_measures, int level)
{
	struct midievent *midievent;
	unsigned int ch;
	int ret;

	assert(midi_es->s_type == MIDIEVENTS);

	if (tmn->autoallocate_channel) {
		for (ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
			/* Midi channel 10 (index 9) is reserved for drums. */
			if (ch == DRUM_MIDICHANNEL)
				continue;
			if (tracks[ch].track == NULL)
				tracks[ch].track = tmn->track;
			if (tracks[ch].track == tmn->track)
				break;
		}
		if (ch == MIDI_CHANNEL_COUNT) {
			warnx("out of available midi tracks");
			return 1;
		}
		tmn->note.channel = ch;
	} else {
		ch = tmn->note.channel;
		tracks[ch].track = tmn->track;
	}

	tracks[ch].notecount[ tmn->note.note ] += 1;
	tracks[ch].total_notecount += 1;

	if (tracks[ch].notecount[tmn->note.note] > 1) {
		/* This note is already playing, go to next event. */
		return 0;
	}

	if (tracks[ch].instrument != tmn->instrument) {
		assert(tmn->instrument != NULL);
		midievent = &midi_es->u.midievents[ midi_es->count ];
		memset(midievent, 0, sizeof(struct midievent));
		midievent->evtype = MIDIEV_INSTRUMENT_CHANGE;
		midievent->time_as_measures = time_as_measures;
		midievent->u.instr_change.channel = ch;
		midievent->u.instr_change.code = tmn->instrument->code;

		_mdl_midievent_log(MDLLOG_MIDISTREAM,
		    "sending to sequencer", midievent, level);

		ret = _mdl_stream_increment(midi_es);
		if (ret != 0)
			return ret;
	}

	tracks[ch].instrument = tmn->instrument;

	midievent = &midi_es->u.midievents[ midi_es->count ];
	memset(midievent, 0, sizeof(struct midievent));
	midievent->evtype = MIDIEV_NOTEON;
	midievent->time_as_measures = time_as_measures;
	midievent->u.note = tmn->note;

	_mdl_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    midievent, level);

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
	struct midievent *midievent;
	struct midistreamevent *mse;
	int ret;
	struct miditracks tracks[MIDI_CHANNEL_COUNT];
	size_t i, j;

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
			ret = add_noteoff_to_midievents(midi_es, &mse->u.tmn,
			    tracks, mse->time_as_measures, level);
			if (ret != 0)
				goto error;
			break;
		case MIDISTREV_NOTEON:
			ret = add_noteon_to_midievents(midi_es, &mse->u.tmn,
			    tracks, mse->time_as_measures, level);
			if (ret != 0)
				goto error;
			break;
		case MIDISTREV_TEMPOCHANGE:
			midievent = &midi_es->u.midievents[ midi_es->count ];
			memset(midievent, 0, sizeof(struct midievent));
			midievent->evtype = MIDIEV_TEMPOCHANGE;
			midievent->time_as_measures = mse->time_as_measures;
			midievent->u.bpm = mse->u.bpm;
			_mdl_midievent_log(MDLLOG_MIDISTREAM,
			    "sending to sequencer", midievent, level);
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
	midievent = &midi_es->u.midievents[ midi_es->count ];
	memset(midievent, 0, sizeof(struct midievent));
	midievent->evtype = MIDIEV_SONG_END;
	midievent->time_as_measures = song_length;

	_mdl_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    midievent, level);

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
	    me->me_type == ME_TYPE_TEMPOCHANGE);

	if (me->me_type == ME_TYPE_TEMPOCHANGE)
		return add_tempochange_to_midistream(midistream_es,
		    &me->u.tempochange, timeoffset);

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
add_note_to_midistream(struct mdl_stream *midistream_es,
    const struct musicexpr *me, float timeoffset, int level)
{
	struct midistreamevent *mse;
	struct trackmidinote *tmn;
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
	tmn = &mse->u.tmn;
	tmn->note.note = new_note;
	tmn->note.velocity = DEFAULT_VELOCITY;

	if (me->me_type == ME_TYPE_ABSDRUM) {
		tmn->autoallocate_channel = 0;
		tmn->note.channel = DRUM_MIDICHANNEL;
		tmn->instrument = me->u.absdrum.instrument;
		tmn->track = me->u.absdrum.track;
	} else if (me->me_type == ME_TYPE_ABSNOTE) {
		tmn->autoallocate_channel = 1;
		tmn->note.channel = DEFAULT_MIDICHANNEL;
		tmn->instrument = me->u.absnote.instrument;
		tmn->track = me->u.absnote.track;
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
	tmn = &mse->u.tmn;
	tmn->autoallocate_channel = 1;
	tmn->note.note = new_note;
	tmn->note.velocity = 0;

	if (me->me_type == ME_TYPE_ABSDRUM) {
		tmn->autoallocate_channel = 0;
		tmn->note.channel = DRUM_MIDICHANNEL;
		tmn->instrument = me->u.absdrum.instrument;
		tmn->track = me->u.absdrum.track;
	} else if (me->me_type == ME_TYPE_ABSNOTE) {
		tmn->autoallocate_channel = 1;
		tmn->note.channel = DEFAULT_MIDICHANNEL;
		tmn->instrument = me->u.absnote.instrument;
		tmn->track = me->u.absnote.track;
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
		return compare_trackmidinotes(&a->u.tmn, &b->u.tmn);
	case MIDISTREV_TEMPOCHANGE:
		return (a->u.bpm < b->u.bpm) ? -1 :
		       (a->u.bpm > b->u.bpm) ?  1 : 0;
	default:
		assert(0);
	}

	return 0;
}

static int
compare_trackmidinotes(const struct trackmidinote *a,
    const struct trackmidinote *b)
{
	return
	    (a->note.channel  < b->note.channel)  ? -1 :
	    (a->note.channel  > b->note.channel)  ?  1 :
	    (a->note.note     < b->note.note)     ? -1 :
	    (a->note.note     > b->note.note)     ?  1 :
	    (a->note.velocity < b->note.velocity) ? -1 :
	    (a->note.velocity > b->note.velocity) ?  1 : 0;
}
