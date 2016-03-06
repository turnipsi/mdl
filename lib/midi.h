/* $Id: midi.h,v 1.14 2016/03/06 19:18:04 je Exp $ */

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

#include "instrument.h"
#include "util.h"

#define MIDI_CHANNEL_COUNT	16
#define MIDI_NOTE_COUNT		128

enum eventtype {
	SONG_END,
	NOTEOFF,
	NOTEON,
	INSTRUMENT_CHANGE,
	EVENTTYPE_COUNT,
};

struct instrument_change {
	u_int8_t	channel, code;
};

struct midinote {
	u_int8_t	channel, note, velocity;
};

struct trackmidinote {
	enum eventtype		eventtype;
	struct instrument      *instrument;
	struct midinote		note;
	float			time_as_measures;
	struct track	       *track;
};

struct midievent {
	enum eventtype	eventtype;
	float		time_as_measures;
	union {
		struct midinote			note;
		struct instrument_change	instrument_change;
	} u;
};

__BEGIN_DECLS
int	midi_open_device(void);
int	midi_check_midievent(struct midievent, float);
int	midi_send_midievent(struct midievent *, int);
void	midi_close_device(void);

void	midievent_log(const char *, struct midievent *, int);
__END_DECLS

#endif /* !MDL_MIDI_H */
