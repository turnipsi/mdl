/* $Id: midistream.c,v 1.6 2016/01/31 20:33:46 je Exp $ */

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
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "joinexpr.h"
#include "midi.h"
#include "midistream.h"
#include "relative.h"

#define DEFAULT_MIDICHANNEL	0
#define DEFAULT_VELOCITY	80

static struct mdl_stream *midi_eventstream_new(void);
static struct mdl_stream *offsetexprstream_new(void);
static struct mdl_stream *offsetexprstream_to_midievents(struct mdl_stream *,
							 int);

static int	add_musicexpr_to_midievents(struct mdl_stream *,
					    const struct musicexpr_t *,
					    float,
					    int);
static int	compare_midievents(const void *, const void *);

struct mdl_stream *
musicexpr_to_midievents(struct musicexpr_t *me, int level)
{
	struct musicexpr_t *me_workcopy, *simultence, *p;
	struct mdl_stream *offset_es, *midi_es;
	struct song_t *song;

	mdl_log(1, level, "converting music expression to midi stream\n");

	midi_es = NULL;
	simultence = NULL;

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

	/* first convert relative->absolute,
	 * joinexpr_musicexpr() can not handle relative expressions */
	musicexpr_relative_to_absolute(song, me_workcopy, level + 1);

	mdl_log(1, level + 1, "joining all music expressions\n");
	if (joinexpr_musicexpr(me_workcopy, level + 1) != 0) {
		warnx("error occurred in joining music expressions");
		goto finish;
	}

	mdl_log(1,
		level + 1,
		"converting expression to a (flat) simultence\n");
	simultence = musicexpr_to_flat_simultence(me_workcopy, level + 1);
	if (simultence == NULL) {
		warnx("Could not flatten music expression to create offset" \
			" expression stream");
		goto finish;
	}

	mdl_log(1, level + 1, "making offset expression stream\n");
	TAILQ_FOREACH(p, &simultence->u.melist, tq) {
		assert(p->me_type == ME_TYPE_OFFSETEXPR);
		offset_es->mexprs[ offset_es->count ] = p->u.offsetexpr;
		if (mdl_stream_increment(offset_es) != 0)
			goto finish;
	}

	midi_es = offsetexprstream_to_midievents(offset_es, level + 1);
	if (midi_es == NULL)
		warnx("could not convert offset-expression-stream" \
			" to midistream");

finish:
	mdl_stream_free(offset_es);

	if (simultence != NULL)
		musicexpr_free(simultence);

	musicexpr_free(me_workcopy);

	return midi_es;
}

ssize_t
midi_write_midistream(int sequencer_socket,
		      struct mdl_stream *s,
		      int level)
{
	size_t wsize;
	ssize_t nw, total_wcount;

	total_wcount = 0;

	/* XXX overflow? */
	wsize = s->count * sizeof(struct midievent);

	while (total_wcount < wsize) {
		nw = write(sequencer_socket,
			   (char *) s->midievents + total_wcount,
			   wsize - total_wcount);
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to sequencer");
			return -1;
		}
		mdl_log(2, level, "wrote %ld bytes to sequencer\n", nw);
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
offsetexprstream_to_midievents(struct mdl_stream *offset_es, int level)
{
	struct mdl_stream *midi_es;
	struct midievent *midievent;
	struct offsetexpr_t offsetexpr;
	struct musicexpr_t *me;
	float timeoffset;
	int i, ret;

	mdl_log(2, level, "offset expression stream to midi events\n");

	if ((midi_es = midi_eventstream_new()) == NULL)
		goto error;

	for (i = 0; i < offset_es->count; i++) {
		offsetexpr = offset_es->mexprs[i];
		me = offsetexpr.me;
		timeoffset = offsetexpr.offset;

		mdl_log(4,
			level + 1,
			"handling expression with offset %.3f\n",
			timeoffset);
		musicexpr_log(me, 4, level + 2, NULL);

		ret = add_musicexpr_to_midievents(midi_es,
						  me,
						  timeoffset,
						  level + 1);
		if (ret != 0)
			goto error;
	}

	ret = heapsort(midi_es->midievents,
		       midi_es->count,
		       sizeof(struct midievent),
		       compare_midievents);
	if (ret == -1) {
		warn("could not sort midieventstream");
		goto error;
	}

	/* add SONG_END midievent */
	midievent = &midi_es->midievents[ midi_es->count ];
	bzero(midievent, sizeof(struct midievent));
	midievent->eventtype = SONG_END;

	ret = mdl_stream_increment(midi_es);
	if (ret != 0)
		goto error;

	return midi_es;

error:
	warnx("could not convert offset-expression-stream to midi stream");
	if (midi_es)
		mdl_stream_free(midi_es);

	return NULL;
}

static int
add_musicexpr_to_midievents(struct mdl_stream *midi_es,
			    const struct musicexpr_t *me,
			    float timeoffset,
			    int level)
{
	struct midievent *midievent;
	int ret, new_note;

	ret = 0;

	/* XXX what about ME_TYPE_REST, that might almost be currently
	 * XXX possible? */

	switch (me->me_type) {
	case ME_TYPE_ABSNOTE:
		new_note = me->u.absnote.note;

		/* we accept and ignore notes that are out-of-range */
		if (new_note < 0 || MIDI_NOTE_MAX < new_note) {
			mdl_log(2,
				level,
				"skipping note with value %d",
				new_note);
			ret = 0;
			break;
		}
		/* length can never be non-positive here, that is a bug */
		assert(me->u.absnote.length > 0);

		midievent = &midi_es->midievents[ midi_es->count ];
		bzero(midievent, sizeof(struct midievent));
		midievent->eventtype = NOTEON;
		midievent->channel = DEFAULT_MIDICHANNEL;
		midievent->note = new_note;
		midievent->time_as_measures = timeoffset;
		midievent->velocity = DEFAULT_VELOCITY;

		ret = mdl_stream_increment(midi_es);
		if (ret != 0)
			break;

		midievent = &midi_es->midievents[ midi_es->count ];
		bzero(midievent, sizeof(struct midievent));
		midievent->eventtype = NOTEOFF;
		midievent->channel = DEFAULT_MIDICHANNEL;
		midievent->note = new_note;
		midievent->time_as_measures
		    = timeoffset + me->u.absnote.length;
		midievent->velocity = 0;

		ret = mdl_stream_increment(midi_es);
		break;
	default:
		assert(0);
		break;
	}

	return ret;
}

static int
compare_midievents(const void *a, const void *b)
{
	const struct midievent *ma, *mb;

	ma = a;
	mb = b;

	return (ma->time_as_measures < mb->time_as_measures)          ? -1 : 
	       (ma->time_as_measures > mb->time_as_measures)          ?  1 :
	       (ma->eventtype == NOTEOFF && mb->eventtype == NOTEON)  ? -1 :
	       (ma->eventtype == NOTEON  && mb->eventtype == NOTEOFF) ?  1 :
	       0;
}
