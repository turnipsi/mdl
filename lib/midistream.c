/* $Id: midistream.c,v 1.73 2016/09/28 20:34:57 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkil� <je@turnipsi.no-ip.org>
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
#include "midi.h"
#include "midistream.h"
#include "relative.h"
#include "util.h"

#define DEFAULT_VELOCITY	80

struct miditrack {
	struct track		prev_values;
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

static int	add_marker_to_midistream(struct mdl_stream *, float);
static int	add_note_to_midistream(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int	add_tempochange_to_midistream(struct mdl_stream *,
    const struct tempochange *, float);
static int	add_volumechange_to_midistream(struct mdl_stream *,
    const struct volumechange *, float);

static int	add_instrument_change_to_midievents(struct mdl_stream *,
    struct instrument *, int, float);
static int	add_marker_to_midievents(struct mdl_stream *, float);
static int	add_noteoff_to_midievents(struct mdl_stream *,
    struct trackmidievent *, struct miditrack *, float, int);
static int	add_noteon_to_midievents(struct mdl_stream *,
    struct trackmidievent *, struct miditrack *, float, int);
static int	add_volumechange_to_midievents(struct mdl_stream *,
    u_int8_t, int, float);

static int	lookup_midichannel(struct trackmidievent *,
    struct miditrack *, int);

static int	handle_midistreamevent(struct midistreamevent *,
    struct mdl_stream *, struct miditrack *, int);

static int	add_musicexpr_to_midistream(struct mdl_stream *,
    const struct musicexpr *, float, int);
static int	compare_midievents(const struct midievent *,
    const struct midievent *);
static int	compare_midistreamevents(const void *, const void *);
static int	compare_timed_midievents(const void *, const void *);

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

	_mdl_musicexpr_relative_to_absolute(song, me, level+1);

	_mdl_musicexpr_tag_expressions_for_joining(me, level);

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
	size_t i;

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "writing midi stream to sequencer\n");

	level += 1;

	if ((SSIZE_MAX / sizeof(struct timed_midievent)) < s->count) {
		warnx("midistream size overflow, not writing anything");
		return -1;
	}

	total_wcount = 0;

	for (i = 0; i < s->count; i++) {
		_mdl_timed_midievent_log(MDLLOG_MIDISTREAM,
		    "sending to sequencer", &s->u.timed_midievents[i], level);
	}

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
		total_wcount += nw;
	}

	_mdl_log(MDLLOG_MIDISTREAM, level, "wrote %ld bytes to sequencer\n",
	    total_wcount);

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

	/*
	 * Sort midistream so that we get midistreamevents ordered by event
	 * type and timing, most specifically.
	 */
	qsort(midistream_es->u.midistreamevents, midistream_es->count,
	    sizeof(struct midistreamevent), compare_midistreamevents);

	midi_es = midistream_to_midievents(midistream_es, song_length,
	    level);
	if (midi_es == NULL)
		goto error;

	/*
	 * Sort again, because midi channels for notes have likely been
	 * changed (by allocating them dynamically) and we want the midi
	 * event order to be fully deterministic.
	 */
	qsort(midi_es->u.timed_midievents, midi_es->count,
	    sizeof(struct timed_midievent), compare_timed_midievents);

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
add_marker_to_midievents(struct mdl_stream *midi_es, float time_as_measures)
{
	struct timed_midievent *tmidiev;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev.evtype = MIDIEV_MARKER;

	return _mdl_stream_increment(midi_es);
}

static int
add_noteoff_to_midievents(struct mdl_stream *midi_es,
    struct trackmidievent *tme, struct miditrack *miditracks,
    float time_as_measures, int level)
{
	struct timed_midievent *tmidiev;
	int ch;

	assert(tme->midiev.evtype == MIDIEV_NOTEOFF);

	ch = lookup_midichannel(tme, miditracks, level);
	assert(ch >= 0);
	assert(miditracks[ch].track != NULL);
	assert(miditracks[ch].track == tme->track);

	miditracks[ch].notecount[ tme->midiev.u.midinote.note ] -= 1;
	miditracks[ch].total_notecount -= 1;
	if (miditracks[ch].total_notecount == 0)
		miditracks[ch].track = NULL;

	assert(miditracks[ch].total_notecount >= 0);

	if (miditracks[ch].notecount[ tme->midiev.u.midinote.note ] > 0) {
		/* This note must still be play, nothing to do. */
		assert(miditracks[ch].total_notecount > 0);
		return 0;
	}

	tme->midiev.u.midinote.channel = ch;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev = tme->midiev;

	return _mdl_stream_increment(midi_es);
}

static int
add_noteon_to_midievents(struct mdl_stream *midi_es,
    struct trackmidievent *tme, struct miditrack *miditracks,
    float time_as_measures, int level)
{
	struct timed_midievent *tmidiev;
	struct miditrack *miditrack;
	struct track *track;
	int ch, ret;

	assert(tme->midiev.evtype == MIDIEV_NOTEON);

	if ((ch = lookup_midichannel(tme, miditracks, level)) == -1)
		return 1;

	track = tme->track;
	miditrack = &miditracks[ch];
	miditrack->track = track;

	if (miditrack->prev_values.instrument != track->instrument) {
		ret = add_instrument_change_to_midievents(midi_es,
		    track->instrument, ch, time_as_measures);
		if (ret != 0)
			return ret;
		miditrack->prev_values.instrument = track->instrument;
	}

	if (miditrack->prev_values.volume != track->volume) {
		ret = add_volumechange_to_midievents(midi_es,
		    track->volume, ch, time_as_measures);
		if (ret != 0)
			return ret;
		miditrack->prev_values.volume = track->volume;
	}

	miditracks[ch].notecount[ tme->midiev.u.midinote.note ] += 1;
	miditracks[ch].total_notecount += 1;

	if (miditracks[ch].notecount[ tme->midiev.u.midinote.note ] > 1) {
		/* This note is xlready playing, go to next event. */
		/* XXX Actually retriggering note would be better...
		 * XXX t-play-notes-already-playing.mdl is a testcase that
		 * XXX needs fixing.  But when fixing, consider also
		 * XXX how sequencer is handling the joined expressions. */
		return 0;
	}

	tme->midiev.u.midinote.channel = ch;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev = tme->midiev;

	return _mdl_stream_increment(midi_es);
}

static int
add_instrument_change_to_midievents(struct mdl_stream *midi_es,
    struct instrument *instrument, int ch, float time_as_measures)
{
	struct timed_midievent *tmidiev;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev.evtype = MIDIEV_INSTRUMENT_CHANGE;
	tmidiev->midiev.u.instr_change.channel = ch;
	tmidiev->midiev.u.instr_change.code = instrument->code;

	return _mdl_stream_increment(midi_es);
}

static int
add_volumechange_to_midievents(struct mdl_stream *midi_es,
    u_int8_t volume, int ch, float time_as_measures)
{
	struct timed_midievent *tmidiev;

	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = time_as_measures;
	tmidiev->midiev.evtype = MIDIEV_VOLUMECHANGE;
	tmidiev->midiev.u.volumechange.channel = ch;
	tmidiev->midiev.u.volumechange.volume = volume;

	return _mdl_stream_increment(midi_es);
}

static int
lookup_midichannel(struct trackmidievent *tme, struct miditrack *miditracks,
    int level)
{
	int ch, old_ch;

	old_ch = ch = tme->track->midichannel;

	/*
	 * If autoallocation is not on, just provide the preferred channel of
	 * the track.
	 */
	if (!tme->track->autoallocate_channel) {
		assert(ch >= 0);
		return ch;
	}

	if (ch >= 0) {
		/*
		 * Test if the midichannel this track previously used is still
		 * reserved by this track, and use that if it is so.
		 */
		if (miditracks[ch].track == tme->track)
			goto found;

		/*
		 * Next look if the preferred channel is currently unused,
		 * and reserve it for this track if that is so.
		 */
		if (miditracks[ch].track == NULL) {
			miditracks[ch].track = tme->track;
			goto found;
		}
	}

	/* Lookup an available channel if there is any available. */
	for (ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
		/* Midi channel 10 (index 9) is reserved for drums. */
		if (ch == MIDI_DRUMCHANNEL)
			continue;
		if (miditracks[ch].track == NULL) {
			/*
			 * Found an available track.  Reserve this track for
			 * us and mark it as our preferred midichannel.
			 */
			miditracks[ch].track = tme->track;
			tme->track->midichannel = ch;
			goto found;
		}
	}

	warnx("out of available midi tracks");

	return -1;

found:
	if (old_ch != ch) {
		if (old_ch == -1) {
			_mdl_log(MDLLOG_MIDISTREAM, level,
			    "putting track \"%s\" to midichannel %d\n",
			    tme->track->name, ch);
		} else {
			_mdl_log(MDLLOG_MIDISTREAM, level,
			    "changing track \"%s\" from midichannel %d to"
			    " %d\n", tme->track->name, old_ch, ch);
		}
	}

	assert(ch >= 0);
	return ch;
}

static struct mdl_stream *
midistream_to_midievents(struct mdl_stream *midistream_es, float song_length,
    int level)
{
	struct mdl_stream *midi_es;
	struct midistreamevent *mse;
	struct timed_midievent *tmidiev;
	struct miditrack miditracks[MIDI_CHANNEL_COUNT];
	size_t i, j;
	int ret;

	assert(midistream_es->s_type == MIDISTREAMEVENTS);

	mse = NULL;

	if ((midi_es = midi_mdlstream_new()) == NULL) {
		warn("could not create new midievent stream");
		return NULL;
	}

	/* Init miditracks. */
	for (i = 0; i < MIDI_CHANNEL_COUNT; i++) {
		miditracks[i].prev_values.instrument = NULL;
		miditracks[i].prev_values.volume = TRACK_DEFAULT_VOLUME;

		miditracks[i].track = NULL;

		for (j = 0; j < MIDI_NOTE_COUNT; j++)
			miditracks[i].notecount[j] = 0;
		miditracks[i].total_notecount = 0;
	}

	_mdl_log(MDLLOG_MIDISTREAM, level,
	    "adding midievents to send queue:\n");

	level += 1;

	for (i = 0; i < midistream_es->count; i++) {
		mse = &midistream_es->u.midistreamevents[i];
		ret = handle_midistreamevent(mse, midi_es, miditracks, level);
		if (ret != 0)
			goto error;
	}

	for (i = 0; i < MIDI_CHANNEL_COUNT; i++)
		assert(miditracks[i].total_notecount == 0);

	assert(song_length >= 0.0);
	assert(mse == NULL || song_length >= mse->time_as_measures);

	/* Add SONG_END midievent. */
	tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
	memset(tmidiev, 0, sizeof(struct timed_midievent));
	tmidiev->time_as_measures = song_length;
	tmidiev->midiev.evtype = MIDIEV_SONG_END;

	if ((ret = _mdl_stream_increment(midi_es)) != 0)
		goto error;

	return midi_es;

error:
	_mdl_stream_free(midi_es);
	return NULL;
}

static int
handle_midistreamevent(struct midistreamevent *mse, struct mdl_stream *midi_es,
    struct miditrack *miditracks, int level)
{
	struct timed_midievent *tmidiev;
	int ch, ret;

	ret = 0;

	assert(midi_es->s_type == MIDIEVENTS);

	switch (mse->evtype) {
	case MIDISTREV_MARKER:
		ret = add_marker_to_midievents(midi_es, mse->time_as_measures);
		break;
	case MIDISTREV_NOTEOFF:
		ret = add_noteoff_to_midievents(midi_es, &mse->u.tme,
		    miditracks, mse->time_as_measures, level);
		break;
	case MIDISTREV_NOTEON:
		ret = add_noteon_to_midievents(midi_es, &mse->u.tme,
		    miditracks, mse->time_as_measures, level);
		break;
	case MIDISTREV_TEMPOCHANGE:
		tmidiev = &midi_es->u.timed_midievents[ midi_es->count ];
		memset(tmidiev, 0, sizeof(struct timed_midievent));
		tmidiev->time_as_measures = mse->time_as_measures;
		tmidiev->midiev.evtype = MIDIEV_TEMPOCHANGE;
		tmidiev->midiev.u.bpm = mse->u.bpm;
		ret = _mdl_stream_increment(midi_es);
		break;
	case MIDISTREV_VOLUMECHANGE:
		assert(mse->u.tme.midiev.evtype == MIDIEV_VOLUMECHANGE);
		ch = lookup_midichannel(&mse->u.tme, miditracks, level);
		if (ch == -1) {
			ret = 1;
			break;
		}
		ret = add_volumechange_to_midievents(midi_es,
		    mse->u.tme.midiev.u.volumechange.volume, ch,
		    mse->time_as_measures);
		break;
	default:
		assert(0);
	}

	return ret;
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
	    me->me_type == ME_TYPE_MARKER ||
	    me->me_type == ME_TYPE_TEMPOCHANGE ||
	    me->me_type == ME_TYPE_VOLUMECHANGE);

	if (me->me_type == ME_TYPE_MARKER)
		return add_marker_to_midistream(midistream_es, timeoffset);

	if (me->me_type == ME_TYPE_TEMPOCHANGE)
		return add_tempochange_to_midistream(midistream_es,
		    &me->u.tempochange, timeoffset);

	if (me->me_type == ME_TYPE_VOLUMECHANGE)
		return add_volumechange_to_midistream(midistream_es,
		    &me->u.volumechange, timeoffset);

	return add_note_to_midistream(midistream_es, me, timeoffset, level);
}

static int
add_marker_to_midistream(struct mdl_stream *midistream_es, float timeoffset)
{
	struct midistreamevent *mse;

	/*
	 * XXX This may seem pointless, but makes more sense once note
	 * XXX expression identities and textual locations are passed to
	 * XXX sequencer as well.
	 */

	mse = &midistream_es->u.midistreamevents[ midistream_es->count ];
	memset(mse, 0, sizeof(struct midistreamevent));
	mse->evtype = MIDISTREV_MARKER;
	mse->time_as_measures = timeoffset;

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

	length = 0.0;
	new_note = -1;

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
	tme->midiev.u.midinote.channel = MIDI_DEFAULTCHANNEL;
	tme->midiev.u.midinote.joining = me->joining;
	tme->midiev.u.midinote.note = new_note;
	tme->midiev.u.midinote.velocity = DEFAULT_VELOCITY;

	if (me->me_type == ME_TYPE_ABSDRUM) {
		tme->instrument = me->u.absdrum.instrument;
		tme->track = me->u.absdrum.track;
	} else if (me->me_type == ME_TYPE_ABSNOTE) {
		tme->instrument = me->u.absnote.instrument;
		tme->track = me->u.absnote.track;
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
	tme->midiev.u.midinote.channel = MIDI_DEFAULTCHANNEL;
	tme->midiev.u.midinote.joining = me->joining;
	tme->midiev.u.midinote.note = new_note;
	tme->midiev.u.midinote.velocity = 0;

	if (me->me_type == ME_TYPE_ABSDRUM) {
		tme->instrument = me->u.absdrum.instrument;
		tme->track = me->u.absdrum.track;
	} else if (me->me_type == ME_TYPE_ABSNOTE) {
		tme->instrument = me->u.absnote.instrument;
		tme->track = me->u.absnote.track;
	} else {
		assert(0);
	}

	return _mdl_stream_increment(midistream_es);
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
	tme->midiev.u.volumechange.volume = volumechg->volume;
	tme->track = volumechg->track;

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
	case MIDISTREV_MARKER:
		/*
		 * XXX Just order these randomly.  With textual locations
		 * XXX one might be able to do a more rational choice.
		 */
		return 1;
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
compare_timed_midievents(const void *va, const void *vb)
{
	const struct timed_midievent *a, *b;

	a = va;
	b = vb;

	return
	    (a->time_as_measures < b->time_as_measures) ? -1 :
	    (a->time_as_measures > b->time_as_measures) ?  1 :
	    compare_midievents(&a->midiev, &b->midiev);
}

static int
compare_midievents(const struct midievent *a, const struct midievent *b)
{
	const struct instrument_change *ic_a, *ic_b;
	const struct midi_volumechange *vc_a, *vc_b;

	assert(a->evtype < MIDIEV_TYPECOUNT);

	if (a->evtype < b->evtype)
		return -1;
	if (a->evtype > b->evtype)
		return 1;

	assert(a->evtype == b->evtype);

	switch (a->evtype) {
	case MIDIEV_INSTRUMENT_CHANGE:
		ic_a = &a->u.instr_change;
		ic_b = &b->u.instr_change;
		return
		    (ic_a->channel < ic_b->channel) ? -1 :
		    (ic_a->channel > ic_b->channel) ?  1 :
		    (ic_a->code    < ic_b->code)    ? -1 :
		    (ic_a->code    > ic_b->code)    ?  1 : 0;
	case MIDIEV_MARKER:
		/*
		 * XXX Just order these randomly.  With textual locations
		 * XXX one might be able to do a more rational choice.
		 */
		return 1;
	case MIDIEV_NOTEOFF:
	case MIDIEV_NOTEON:
		return
		    (a->u.midinote.channel  < b->u.midinote.channel)  ? -1 :
		    (a->u.midinote.channel  > b->u.midinote.channel)  ?  1 :
		    (a->u.midinote.note     < b->u.midinote.note)     ? -1 :
		    (a->u.midinote.note     > b->u.midinote.note)     ?  1 :
		    (a->u.midinote.velocity < b->u.midinote.velocity) ? -1 :
		    (a->u.midinote.velocity > b->u.midinote.velocity) ?  1 : 0;
	case MIDIEV_SONG_END:
		return 0;
	case MIDIEV_TEMPOCHANGE:
		return (a->u.bpm < b->u.bpm) ? -1 :
		       (a->u.bpm > b->u.bpm) ?  1 : 0;
	case MIDIEV_VOLUMECHANGE:
		vc_a = &a->u.volumechange;
		vc_b = &b->u.volumechange;
		return (vc_a->channel < vc_b->channel) ? -1 :
		       (vc_a->channel > vc_b->channel) ?  1 :
		       (vc_a->volume  < vc_b->volume)  ? -1 :
		       (vc_a->volume  > vc_b->volume)  ?  1 : 0;
	default:
		assert(0);
	}

	return 0;
}
