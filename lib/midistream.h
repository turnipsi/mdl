/* $Id: midistream.h,v 1.12 2016/08/23 20:22:58 je Exp $ */

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

#ifndef MDL_MIDISTREAM_H
#define MDL_MIDISTREAM_H

#include <sys/types.h>

#include "midi.h"
#include "musicexpr.h"

enum midistreamevent_type {
	/*
	 * Order matters here, because that is used in
	 * compare_midistreamevents() to put events in proper order.
	 * Particularly, MIDISTREV_NOTEOFF must come before MIDISTREV_NOTEON.
	 */
	MIDISTREV_NOTEOFF,
	MIDISTREV_TEMPOCHANGE,
	MIDISTREV_VOLUMECHANGE,
	MIDISTREV_NOTEON,
	MIDISTREV_TYPECOUNT,	/* not a type */
};

struct trackmidievent {
	struct midievent	midiev;
	struct track	       *track;
};

struct midistreamevent {
	enum midistreamevent_type	evtype;
	float				time_as_measures;
	union {
		struct trackmidievent	tme;
		float			bpm;
	} u;
};

__BEGIN_DECLS
struct mdl_stream      *_mdl_musicexpr_to_midievents(struct musicexpr *, int);
ssize_t			_mdl_midi_write_midistream(int, struct mdl_stream *,
    int);
__END_DECLS

#endif /* !MDL_MIDISTREAM_H */
