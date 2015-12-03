/* $Id: midi.h,v 1.6 2015/12/03 20:46:25 je Exp $ */

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

#ifndef MDL_MIDI_H
#define MDL_MIDI_H

#include "util.h"

#define MIDI_CHANNEL_MAX	15
#define MIDI_NOTE_MAX		127

enum eventtype_t { SONG_END, NOTEOFF, NOTEON, EVENTTYPE_COUNT };

struct midievent {
	enum eventtype_t        eventtype;
	u_int8_t                channel, note, velocity;
	float                   time_as_measures;
};

int	midi_open_device(void);
int	midi_check_midievent(struct midievent, float);
int	midi_send_midievent(struct midievent *);
void	midi_close_device(void);

struct mdl_stream      *midi_eventstream_new(void);

ssize_t	midi_write_midistream(int, struct mdl_stream *, int);

#endif
