/* $Id: midi.h,v 1.33 2016/09/02 20:53:53 je Exp $ */

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
#define MIDI_DEFAULTCHANNEL	0
#define MIDI_DRUMCHANNEL	9
#define MIDI_NOTE_COUNT		128

enum midievent_type {
	MIDIEV_NOTEOFF,
	MIDIEV_INSTRUMENT_CHANGE,
	MIDIEV_NOTEON,
	MIDIEV_SONG_END,
	MIDIEV_TEMPOCHANGE,
	MIDIEV_VOLUMECHANGE,
	MIDIEV_TYPECOUNT,	/* not a type */
};

/* XXX Introduce a control_change structure instead of this? */
struct instrument_change {
	u_int8_t	channel;
	u_int8_t	code;
};

/* XXX Introduce a control_change structure instead of this? */
struct midi_volumechange {
	u_int8_t	channel;
	u_int8_t	volume;
};

struct midinote {
	u_int8_t	channel;
	u_int8_t	note;
	u_int8_t	velocity;
};

struct midievent {
	enum midievent_type	evtype;
	union {
		struct instrument_change	instr_change;
		struct midinote			midinote;
		struct midi_volumechange	volumechange;
		float				bpm;
	} u;
};

struct timed_midievent {
	struct midievent	midiev;
	float			time_as_measures;
};

enum mididev_type { MIDIDEV_NONE, MIDIDEV_RAW, MIDIDEV_SNDIO };

__BEGIN_DECLS
int	_mdl_midi_open_device(enum mididev_type, const char *);
int	_mdl_midi_check_timed_midievent(struct timed_midievent, float);
int	_mdl_midi_play_midievent(struct midievent *, int);
void	_mdl_midi_close_device(void);

enum mididev_type	_mdl_midi_get_mididev_type(const char *);

void	_mdl_timed_midievent_log(enum logtype, const char *,
    struct timed_midievent *, int);
__END_DECLS

#endif /* !MDL_MIDI_H */
