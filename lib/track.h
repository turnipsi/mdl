/* $Id: track.h,v 1.12 2016/08/27 18:53:31 je Exp $ */

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

#ifndef MDL_TRACK_H
#define MDL_TRACK_H

#include <sys/queue.h>
#include <sys/types.h>

#include "instrument.h"

#define TRACK_DEFAULT_VOLUME 64

struct track {
	struct instrument      *instrument;
	char		       *name;
	int			autoallocate_channel;
	u_int8_t		midichannel;
	u_int8_t		volume;
	SLIST_ENTRY(track)	sl;
};

__BEGIN_DECLS
struct track	*_mdl_track_new(enum instrument_type, const char *);
__END_DECLS

#endif /* !MDL_TRACK_H */
