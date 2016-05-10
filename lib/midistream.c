/* $Id: midistream.c,v 1.37 2016/05/10 20:39:43 je Exp $ */

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

#include <sys/queue.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "joinexpr.h"
#include "midi.h"
#include "midistream.h"
#include "relative.h"

#define DEFAULT_MIDICHANNEL		0
#define DEFAULT_VELOCITY		80
#define INSTRUMENT_CHANNEL_COUNT	(MIDI_CHANNEL_COUNT - 1)

struct miditracks {
	struct instrument *instrument;
	struct track *track;
	int notecount[MIDI_NOTE_COUNT];
	int total_notecount;
};

static struct mdl_stream *midi_eventstream_new(void);
static struct mdl_stream *offsetexprstream_new(void);
static struct mdl_stream *offsetexprstream_to_midievents(struct mdl_stream *,
    float, int);
static int add_noteoff_to_midievents(struct mdl_stream *, struct trackmidinote,
    struct miditracks *, int);
static int add_noteon_to_midievents(struct mdl_stream *, struct trackmidinote,
    struct miditracks *, int);
static struct mdl_stream *trackmidievents_to_midievents(struct mdl_stream *,
    float, int);
static int add_musicexpr_to_trackmidievents(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int compare_trackmidievents(const void *, const void *);

struct mdl_stream *
_mdl_musicexpr_to_midievents(struct musicexpr *me, int level)
{
	struct mdl_stream *offset_es, *midi_es;
	struct musicexpr *flatme, *p;
	struct song *song;

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
	    "converting music expression to midi stream\n");

	midi_es = NULL;
	flatme = NULL;

	if ((offset_es = offsetexprstream_new()) == NULL) {
		warnx("could not setup new offsetexprstream");
		return NULL;
	}

	song = _mdl_mdl_song_new(me, level+1);
	if (song == NULL) {
		warnx("could not create a new song");
		_mdl_mdl_stream_free(offset_es);
		return NULL;
	}

	/*
	 * First convert relative->absolute,
	 * _mdl_joinexpr_musicexpr() can not handle relative expressions.
	 */
	_mdl_musicexpr_relative_to_absolute(song, me, level+1);

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level, "joining music expressions\n");
	if (_mdl_joinexpr_musicexpr(me, level+1) != 0) {
		warnx("error occurred in joining music expressions");
		goto finish;
	}

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
	    "converting expression to a (flat) simultence\n");
	flatme = _mdl_musicexpr_to_flat_simultence(me, level+1);
	if (flatme == NULL) {
		warnx("Could not flatten music expression to create offset"
		    " expression stream");
		goto finish;
	}

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level, "making offset expression stream\n");
	TAILQ_FOREACH(p, &flatme->u.flatsimultence.me->u.melist, tq) {
		assert(p->me_type == ME_TYPE_OFFSETEXPR);
		offset_es->u.mexprs[ offset_es->count ] = p->u.offsetexpr;
		if (_mdl_mdl_stream_increment(offset_es) != 0)
			goto finish;
	}

	midi_es = offsetexprstream_to_midievents(offset_es,
	    flatme->u.flatsimultence.length, level);

finish:
	_mdl_mdl_stream_free(offset_es);

	if (flatme != NULL)
		_mdl_musicexpr_free(flatme, level);

	return midi_es;
}

ssize_t
_mdl_midi_write_midistream(int sequencer_socket, struct mdl_stream *s, int level)
{
	ssize_t nw, total_wcount, wsize;

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
	    "writing midi stream to sequencer\n");

	level += 1;

	if ((SSIZE_MAX / sizeof(struct midievent)) < s->count) {
		warnx("midistream size overflow, not writing anything");
		return -1;
	}

	total_wcount = 0;

	wsize = s->count * sizeof(struct midievent);

	while (total_wcount < wsize) {
		/* XXX what if nw == 0 (continuously)?  can that happen? */
		nw = write(sequencer_socket,
		    ((char *) s->u.midievents + total_wcount),
		    (wsize - total_wcount));
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to sequencer");
			return -1;
		}
		_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
		    "wrote %ld bytes to sequencer\n", nw);
		total_wcount += nw;
	}

	return total_wcount;
}

static struct mdl_stream *
midi_eventstream_new(void)
{
	return _mdl_mdl_stream_new(MIDIEVENTSTREAM);
}

static struct mdl_stream *
offsetexprstream_new(void)
{
	return _mdl_mdl_stream_new(OFFSETEXPRSTREAM);
}

static struct mdl_stream *
trackmidi_eventstream_new(void)
{
	return _mdl_mdl_stream_new(TRACKMIDIEVENTSTREAM);
}

static struct mdl_stream *
offsetexprstream_to_midievents(struct mdl_stream *offset_es, float song_length,
    int level)
{
	struct mdl_stream *midi_es, *trackmidi_es;
	struct musicexpr *me;
	struct offsetexpr offsetexpr;
	float timeoffset;
	size_t i;
	int ret;

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level+1,
	    "offset expression stream to midi events\n");

	midi_es = NULL;
	trackmidi_es = NULL;

	if ((trackmidi_es = trackmidi_eventstream_new()) == NULL)
		goto error;

	for (i = 0; i < offset_es->count; i++) {
		offsetexpr = offset_es->u.mexprs[i];
		me = offsetexpr.me;
		timeoffset = offsetexpr.offset;

		ret = add_musicexpr_to_trackmidievents(trackmidi_es, me,
		    timeoffset, level+2);
		if (ret != 0)
			goto error;
	}

	qsort(trackmidi_es->u.midievents, trackmidi_es->count,
	    sizeof(struct trackmidinote), compare_trackmidievents);

	midi_es = trackmidievents_to_midievents(trackmidi_es, song_length,
	    level);
	if (midi_es == NULL)
		goto error;

	_mdl_mdl_stream_free(trackmidi_es);

	return midi_es;

error:
	warnx("could not convert offset-expression-stream to midi stream");
	if (trackmidi_es)
		_mdl_mdl_stream_free(trackmidi_es);
	if (midi_es)
		_mdl_mdl_stream_free(midi_es);

	return NULL;
}

static int
add_noteoff_to_midievents(struct mdl_stream *midi_es, struct trackmidinote tmn,
	struct miditracks *instr_tracks, int level)
{
	struct midievent *midievent;
	unsigned int ch, midichannel;

	for (ch = 0; ch < INSTRUMENT_CHANNEL_COUNT; ch++) {
		if (tmn.track == instr_tracks[ch].track) {
			instr_tracks[ch].notecount[ tmn.note.note ] -= 1;
			instr_tracks[ch].total_notecount -= 1;
			if (instr_tracks[ch].total_notecount == 0)
				instr_tracks[ch].track = NULL;
			break;
		}
	}
	assert(ch < INSTRUMENT_CHANNEL_COUNT);
	assert(instr_tracks[ch].total_notecount >= 0);

	if (instr_tracks[ch].notecount[tmn.note.note] > 0) {
		/* This note must still be play, nothing to do. */
		return 0;
	}

	/* Midi channel 10 (index 9) is reserved for drums. */
	midichannel = (ch <= 8 ? ch : (ch + 1));
	tmn.note.channel = midichannel;

	midievent = &midi_es->u.midievents[ midi_es->count ];
	memset(midievent, 0, sizeof(struct midievent));
	midievent->eventtype = NOTEOFF;
	midievent->time_as_measures = tmn.time_as_measures;
	midievent->u.note = tmn.note;

	_mdl_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer", midievent,
	    level);

	return _mdl_mdl_stream_increment(midi_es);
}

static int
add_noteon_to_midievents(struct mdl_stream *midi_es, struct trackmidinote tmn,
	struct miditracks *instr_tracks, int level)
{
	struct midievent *midievent;
	unsigned int ch, midichannel;
	int ret;

	for (ch = 0; ch < INSTRUMENT_CHANNEL_COUNT; ch++) {
		if (instr_tracks[ch].track == NULL) {
			instr_tracks[ch].notecount[ tmn.note.note ] += 1;
			instr_tracks[ch].total_notecount += 1;
			instr_tracks[ch].track = tmn.track;
			break;
		} else if (tmn.track == instr_tracks[ch].track) {
			instr_tracks[ch].notecount[ tmn.note.note ] += 1;
			instr_tracks[ch].total_notecount += 1;
			break;
		}
	}
	if (ch == INSTRUMENT_CHANNEL_COUNT) {
		warnx("out of available midi tracks");
		return 1;
	}
	if (instr_tracks[ch].notecount[tmn.note.note] > 1) {
		/* This note is already playing, go to next event. */
		return 0;
	}

	/* Midi channel 10 (index 9) is reserved for drums. */
	midichannel = (ch <= 8 ? ch : (ch + 1));
	tmn.note.channel = midichannel;

	if (instr_tracks[ch].instrument != tmn.instrument) {
		assert(tmn.instrument != NULL);
		midievent = &midi_es->u.midievents[ midi_es->count ];
		memset(midievent, 0, sizeof(struct midievent));
		midievent->eventtype = INSTRUMENT_CHANGE;
		midievent->time_as_measures = tmn.time_as_measures;
		midievent->u.instrument_change.channel = midichannel;
		midievent->u.instrument_change.code = tmn.instrument->code;

		_mdl_midievent_log(MDLLOG_MIDISTREAM,
		    "sending to sequencer", midievent, level);

		ret = _mdl_mdl_stream_increment(midi_es);
		if (ret != 0)
			return ret;
	}

	instr_tracks[ch].instrument = tmn.instrument;

	midievent = &midi_es->u.midievents[ midi_es->count ];
	memset(midievent, 0, sizeof(struct midievent));
	midievent->eventtype = NOTEON;
	midievent->time_as_measures = tmn.time_as_measures;
	midievent->u.note = tmn.note;

	_mdl_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer",
	    midievent, level);

	ret = _mdl_mdl_stream_increment(midi_es);
	if (ret != 0)
		return ret;

	return 0;
}

static struct mdl_stream *
trackmidievents_to_midievents(struct mdl_stream *trackmidi_es,
    float song_length, int level)
{
	struct mdl_stream *midi_es;
	struct midievent *midievent;
	struct trackmidinote tmn;
	int ret;
	struct miditracks instr_tracks[INSTRUMENT_CHANNEL_COUNT];
	size_t i, j;

	tmn.time_as_measures = 0.0;

	if ((midi_es = midi_eventstream_new()) == NULL) {
		warn("could not create new midievent stream");
		return NULL;
	}

	for (i = 0; i < INSTRUMENT_CHANNEL_COUNT; i++) {
		instr_tracks[i].instrument = NULL;
		instr_tracks[i].track = NULL;
		for (j = 0; j < MIDI_NOTE_COUNT; j++)
			instr_tracks[i].notecount[j] = 0;
		instr_tracks[i].total_notecount = 0;
	}

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
	    "adding midievents to send queue:\n");

	level += 1;

	for (i = 0; i < trackmidi_es->count; i++) {
		tmn = trackmidi_es->u.trackmidinotes[i];

		switch (tmn.eventtype) {
		case INSTRUMENT_CHANGE:
		case SONG_END:
			/* These events should not occur here. */
			assert(0);
			break;
		case NOTEOFF:
			ret = add_noteoff_to_midievents(midi_es, tmn,
			    instr_tracks, level);
			if (ret != 0)
				goto error;
			break;
		case NOTEON:
			ret = add_noteon_to_midievents(midi_es, tmn,
			    instr_tracks, level);
			if (ret != 0)
				goto error;
			break;
		default:
			assert(0);
		}
	}

	for (i = 0; i < INSTRUMENT_CHANNEL_COUNT; i++)
		assert(instr_tracks[i].total_notecount == 0);

	assert(song_length >= 0.0);
	assert(song_length >= tmn.time_as_measures);

	/* Add SONG_END midievent. */
	midievent = &midi_es->u.midievents[ midi_es->count ];
	memset(midievent, 0, sizeof(struct midievent));
	midievent->eventtype = SONG_END;
	midievent->time_as_measures = song_length;

	_mdl_midievent_log(MDLLOG_MIDISTREAM, "sending to sequencer", midievent,
	    level);

	ret = _mdl_mdl_stream_increment(midi_es);
	if (ret != 0)
		goto error;

	return midi_es;

error:
	_mdl_mdl_stream_free(midi_es);
	return NULL;
}

static int
add_musicexpr_to_trackmidievents(struct mdl_stream *trackmidi_es,
    const struct musicexpr *me, float timeoffset, int level)
{
	struct trackmidinote *tmnote;
	int ret, new_note;

	_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
	    "adding expression with offset %.3f to trackmidievents\n",
	    timeoffset);
	_mdl_musicexpr_log(me, MDLLOG_MIDISTREAM, level+1, NULL);

	level += 1;

	/*
	 * XXX What about ME_TYPE_REST, that might almost be currently
	 * XXX possible? (it is probably better if SONG_END could have
	 * XXX offset information so that the final rest would be
	 * XXX unnecessary and we could expect only ME_TYPE_ABSNOTEs
	 * XXX here).
	 */

	assert(me->me_type == ME_TYPE_ABSNOTE);

	new_note = me->u.absnote.note;

	/* We accept and ignore notes that are out-of-range. */
	if (new_note < 0 || MIDI_NOTE_COUNT <= new_note) {
		_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
		    "skipping note with value %d\n", new_note);
		return 0;
	}
	assert(me->u.absnote.length > 0);

	/*
	 * Ignore notes that are less than MINIMUM_MUSICEXPR_LENGTH.
	 * Notes that have zero length trigger issues after midi events
	 * are sorted.
	 */
	if (me->u.absnote.length < MINIMUM_MUSICEXPR_LENGTH) {
		_mdl_mdl_log(MDLLOG_MIDISTREAM, level,
		    "skipping note with length %.9f\n", me->u.absnote.length);
		return 0;
	}

	tmnote = &trackmidi_es->u.trackmidinotes[ trackmidi_es->count ];
	memset(tmnote, 0, sizeof(struct trackmidinote));
	tmnote->eventtype = NOTEON;
	tmnote->instrument = me->u.absnote.instrument;
	tmnote->note.channel = DEFAULT_MIDICHANNEL;
	tmnote->note.note = new_note;
	tmnote->note.velocity = DEFAULT_VELOCITY;
	tmnote->time_as_measures = timeoffset;
	tmnote->track = me->u.absnote.track;

	ret = _mdl_mdl_stream_increment(trackmidi_es);
	if (ret != 0)
		return ret;

	tmnote = &trackmidi_es->u.trackmidinotes[ trackmidi_es->count ];
	memset(tmnote, 0, sizeof(struct trackmidinote));
	tmnote->eventtype = NOTEOFF;
	tmnote->instrument = me->u.absnote.instrument;
	tmnote->note.channel = DEFAULT_MIDICHANNEL;
	tmnote->note.note = new_note;
	tmnote->note.velocity = 0;
	tmnote->time_as_measures = timeoffset + me->u.absnote.length;
	tmnote->track = me->u.absnote.track;

	return _mdl_mdl_stream_increment(trackmidi_es);
}

static int
compare_trackmidievents(const void *a, const void *b)
{
	const struct trackmidinote *ta, *tb;

	ta = a;
	tb = b;

	assert(ta->eventtype == NOTEOFF || ta->eventtype == NOTEON);
	assert(tb->eventtype == NOTEOFF || tb->eventtype == NOTEON);

	return (ta->time_as_measures < tb->time_as_measures) ? -1 :
	    (ta->time_as_measures > tb->time_as_measures)    ?  1 :
	    (ta->eventtype < tb->eventtype)                  ? -1 :
	    (ta->eventtype > tb->eventtype)                  ?  1 :
	    (ta->note.channel < tb->note.channel)            ? -1 :
	    (ta->note.channel > tb->note.channel)            ?  1 :
	    (ta->note.note < tb->note.note)                  ? -1 :
	    (ta->note.note > tb->note.note)                  ?  1 :
	    (ta->note.velocity < tb->note.velocity)          ? -1 :
	    (ta->note.velocity > tb->note.velocity)          ?  1 : 0;
}
