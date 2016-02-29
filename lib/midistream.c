/* $Id: midistream.c,v 1.19 2016/02/29 21:09:29 je Exp $ */

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
#include <strings.h>
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
	int notecount;
};

static struct mdl_stream *midi_eventstream_new(void);
static struct mdl_stream *offsetexprstream_new(void);
static struct mdl_stream *offsetexprstream_to_midievents(struct mdl_stream *,
    int);
static struct mdl_stream *trackmidievents_to_midievents(struct mdl_stream *,
    int);
static int add_musicexpr_to_trackmidievents(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int compare_trackmidievents(const void *, const void *);

struct mdl_stream *
musicexpr_to_midievents(struct musicexpr *me, int level)
{
	struct mdl_stream *offset_es, *midi_es;
	struct musicexpr *me_workcopy, *flatme, *p;
	struct song *song;

	mdl_log(MDLLOG_MIDISTREAM, level,
	    "converting music expression to midi stream\n");

	midi_es = NULL;
	flatme = NULL;

	if ((offset_es = offsetexprstream_new()) == NULL) {
		warnx("could not setup new offsetexprstream");
		return NULL;
	}

	if ((me_workcopy = musicexpr_clone(me, level + 1)) == NULL) {
		warnx("could not clone music expressions");
		mdl_stream_free(offset_es);
		return NULL;
	}

	song = mdl_song_new(me_workcopy, level + 1);
	if (song == NULL) {
		warnx("could not create a new song");
		musicexpr_free(me_workcopy);
		mdl_stream_free(offset_es);
		return NULL;
	}

	/*
	 * First convert relative->absolute,
	 * joinexpr_musicexpr() can not handle relative expressions.
	 */
	musicexpr_relative_to_absolute(song, me_workcopy, level + 1);

	mdl_log(MDLLOG_MIDISTREAM, (level + 1),
	    "joining all music expressions\n");
	if (joinexpr_musicexpr(me_workcopy, level + 1) != 0) {
		warnx("error occurred in joining music expressions");
		goto finish;
	}

	mdl_log(MDLLOG_MIDISTREAM, (level + 1),
	    "converting expression to a (flat) simultence\n");
	flatme = musicexpr_to_flat_simultence(me_workcopy, level + 1);
	if (flatme == NULL) {
		warnx("Could not flatten music expression to create offset"
		    " expression stream");
		goto finish;
	}

	mdl_log(MDLLOG_MIDISTREAM, (level + 1),
	    "making offset expression stream\n");
	TAILQ_FOREACH(p, &flatme->u.flatsimultence.me->u.melist, tq) {
		assert(p->me_type == ME_TYPE_OFFSETEXPR);
		offset_es->mexprs[ offset_es->count ] = p->u.offsetexpr;
		if (mdl_stream_increment(offset_es) != 0)
			goto finish;
	}

	midi_es = offsetexprstream_to_midievents(offset_es, level + 1);

finish:
	mdl_stream_free(offset_es);

	if (flatme != NULL)
		musicexpr_free(flatme);

	musicexpr_free(me_workcopy);

	return midi_es;
}

ssize_t
midi_write_midistream(int sequencer_socket, struct mdl_stream *s, int level)
{
	ssize_t nw, total_wcount, wsize;

	if ((SSIZE_MAX / sizeof(struct midievent)) < s->count) {
		warnx("midistream size overflow, not writing anything");
		return -1;
	}

	total_wcount = 0;

	wsize = s->count * sizeof(struct midievent);

	while (total_wcount < wsize) {
		nw = write(sequencer_socket,
		    ((char *) s->midievents + total_wcount),
		    (wsize - total_wcount));
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to sequencer");
			return -1;
		}
		mdl_log(MDLLOG_MIDISTREAM, level,
		    "wrote %ld bytes to sequencer\n", nw);
		total_wcount += nw;
	}

	return total_wcount;
}

static struct mdl_stream *
midi_eventstream_new(void)
{
	return mdl_stream_new(MIDIEVENTSTREAM);
}

static struct mdl_stream *
offsetexprstream_new(void)
{
	return mdl_stream_new(OFFSETEXPRSTREAM);
}

static struct mdl_stream *
trackmidi_eventstream_new(void)
{
	return mdl_stream_new(TRACKMIDIEVENTSTREAM);
}

static struct mdl_stream *
offsetexprstream_to_midievents(struct mdl_stream *offset_es, int level)
{
	struct mdl_stream *midi_es, *trackmidi_es;
	struct musicexpr *me;
	struct offsetexpr offsetexpr;
	float timeoffset;
	size_t i;
	int ret;

	mdl_log(MDLLOG_MIDISTREAM, level,
	    "offset expression stream to midi events\n");

	midi_es = NULL;
	trackmidi_es = NULL;

	if ((trackmidi_es = trackmidi_eventstream_new()) == NULL)
		goto error;

	for (i = 0; i < offset_es->count; i++) {
		offsetexpr = offset_es->mexprs[i];
		me = offsetexpr.me;
		timeoffset = offsetexpr.offset;

		ret = add_musicexpr_to_trackmidievents(trackmidi_es, me,
		    timeoffset, (level + 1));
		if (ret != 0)
			goto error;
	}

	ret = heapsort(trackmidi_es->midievents, trackmidi_es->count,
	    sizeof(struct trackmidinote), compare_trackmidievents);
	if (ret == -1) {
		warn("could not sort midieventstream");
		goto error;
	}

	midi_es = trackmidievents_to_midievents(trackmidi_es, level);
	if (midi_es == NULL)
		goto error;

	mdl_stream_free(trackmidi_es);

	return midi_es;

error:
	warnx("could not convert offset-expression-stream to midi stream");
	if (trackmidi_es)
		mdl_stream_free(trackmidi_es);
	if (midi_es)
		mdl_stream_free(midi_es);

	return NULL;
}

static struct mdl_stream *
trackmidievents_to_midievents(struct mdl_stream *trackmidi_es, int level)
{
	struct mdl_stream *midi_es;
	struct midievent *midievent;
	struct trackmidinote tmn;
	int ret;
	unsigned int ch, midichannel;
	struct miditracks instr_tracks[INSTRUMENT_CHANNEL_COUNT];
	size_t i;

	tmn.time_as_measures = 0.0;

	if ((midi_es = midi_eventstream_new()) == NULL) {
		warn("could not create new midievent stream");
		return NULL;
	}

	for (i = 0; i < INSTRUMENT_CHANNEL_COUNT; i++) {
		instr_tracks[i].notecount = 0;
		instr_tracks[i].instrument = NULL;
		instr_tracks[i].track = NULL;
	}

	mdl_log(MDLLOG_MIDISTREAM, level,
	    "adding midievents to send queue:\n");

	for (i = 0; i < trackmidi_es->count; i++) {
		tmn = trackmidi_es->trackmidinotes[i];

		switch (tmn.eventtype) {
		case NOTEOFF:
			for (ch = 0; ch < INSTRUMENT_CHANNEL_COUNT; ch++) {
				if (tmn.track == instr_tracks[ch].track) {
					instr_tracks[ch].notecount--;
					if (instr_tracks[ch].notecount == 0)
						instr_tracks[ch].track = NULL;
					break;
				}
			}
			assert(ch < INSTRUMENT_CHANNEL_COUNT);
			assert(instr_tracks[ch].notecount >= 0);

			/* Midi channel 10 (index 9) is reserved for drums. */
			midichannel = (ch <= 8 ? ch : (ch + 1));
			tmn.note.channel = midichannel;

			midievent = &midi_es->midievents[ midi_es->count ];
			bzero(midievent, sizeof(struct midievent));
			midievent->eventtype = NOTEOFF;
			midievent->time_as_measures = tmn.time_as_measures;
			midievent->u.note = tmn.note;

			midievent_log("sending", midievent, level + 1);

			ret = mdl_stream_increment(midi_es);
			if (ret != 0)
				goto error;

			break;
		case NOTEON:
			for (ch = 0; ch < INSTRUMENT_CHANNEL_COUNT; ch++) {
				if (instr_tracks[ch].track == NULL) {
					instr_tracks[ch].notecount++;
					instr_tracks[ch].track = tmn.track;
					break;
				} else if (tmn.track ==
				    instr_tracks[ch].track) {
					instr_tracks[ch].notecount++;
					break;
				}
			}
			if (ch == INSTRUMENT_CHANNEL_COUNT) {
				warnx("out of available midi tracks");
				goto error;
			}

			/* Midi channel 10 (index 9) is reserved for drums. */
			midichannel = (ch <= 8 ? ch : (ch + 1));
			tmn.note.channel = midichannel;

			if (instr_tracks[ch].instrument != tmn.instrument) {
				assert(tmn.instrument != NULL);
				midievent =
				    &midi_es->midievents[ midi_es->count ];
				bzero(midievent, sizeof(struct midievent));
				midievent->eventtype = INSTRUMENT_CHANGE;
				midievent->time_as_measures =
				    tmn.time_as_measures;
				midievent->u.instrument_change.channel =
				    midichannel;
				midievent->u.instrument_change.code =
				    tmn.instrument->code;

				midievent_log("sending", midievent,
				    (level + 1));

				ret = mdl_stream_increment(midi_es);
				if (ret != 0)
					goto error;
			}

			instr_tracks[ch].instrument = tmn.instrument;

			midievent = &midi_es->midievents[ midi_es->count ];
			bzero(midievent, sizeof(struct midievent));
			midievent->eventtype = NOTEON;
			midievent->time_as_measures = tmn.time_as_measures;
			midievent->u.note = tmn.note;

			midievent_log("sending", midievent, level + 1);

			ret = mdl_stream_increment(midi_es);
			if (ret != 0)
				goto error;

			break;
		default:
			assert(0);
		}
	}

	for (i = 0; i < INSTRUMENT_CHANNEL_COUNT; i++)
		assert(instr_tracks[i].notecount == 0);

	/* Add SONG_END midievent. */
	/* XXX This should use information from flatsimultence length,
	 * XXX so we can have a rest at the end. */
	midievent = &midi_es->midievents[ midi_es->count ];
	bzero(midievent, sizeof(struct midievent));
	midievent->eventtype = SONG_END;
	midievent->time_as_measures = tmn.time_as_measures;

	midievent_log("sending", midievent, level + 1);

	ret = mdl_stream_increment(midi_es);
	if (ret != 0)
		goto error;

	return midi_es;

error:
	mdl_stream_free(midi_es);
	return NULL;
}

static int
add_musicexpr_to_trackmidievents(struct mdl_stream *trackmidi_es,
    const struct musicexpr *me, float timeoffset, int level)
{
	struct trackmidinote *tmnote;
	int ret, new_note;

	mdl_log(MDLLOG_MIDISTREAM, (level + 1),
	    "adding expression with offset %.3f to trackmidievents\n",
	    timeoffset);
	musicexpr_log(me, 4, (level + 2), NULL);

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
		mdl_log(MDLLOG_MIDISTREAM, level,
		    "skipping note with value %d", new_note);
		return 0;
	}
	assert(me->u.absnote.length > 0);

	tmnote = &trackmidi_es->trackmidinotes[ trackmidi_es->count ];
	bzero(tmnote, sizeof(struct trackmidinote));
	tmnote->eventtype = NOTEON;
	tmnote->instrument = me->u.absnote.instrument;
	tmnote->note.channel = DEFAULT_MIDICHANNEL;
	tmnote->note.note = new_note;
	tmnote->note.velocity = DEFAULT_VELOCITY;
	tmnote->time_as_measures = timeoffset;
	tmnote->track = me->u.absnote.track;

	ret = mdl_stream_increment(trackmidi_es);
	if (ret != 0)
		return ret;

	tmnote = &trackmidi_es->trackmidinotes[ trackmidi_es->count ];
	bzero(tmnote, sizeof(struct trackmidinote));
	tmnote->eventtype = NOTEOFF;
	tmnote->instrument = me->u.absnote.instrument;
	tmnote->note.channel = DEFAULT_MIDICHANNEL;
	tmnote->note.note = new_note;
	tmnote->note.velocity = 0;
	tmnote->time_as_measures = timeoffset + me->u.absnote.length;
	tmnote->track = me->u.absnote.track;

	return mdl_stream_increment(trackmidi_es);
}

static int
compare_trackmidievents(const void *a, const void *b)
{
	const struct trackmidinote *ta, *tb;

	ta = a;
	tb = b;

	return (ta->time_as_measures < tb->time_as_measures) ? -1 :
	    (ta->time_as_measures > tb->time_as_measures) ? 1 :
	    (ta->eventtype == NOTEOFF && tb->eventtype == NOTEON)  ? -1 :
	    (ta->eventtype == NOTEON  && tb->eventtype == NOTEOFF) ?  1 :
	    0;
}
