/* $Id: song.h,v 1.9 2016/07/22 20:17:26 je Exp $ */

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

#ifndef MDL_SONG_H
#define MDL_SONG_H

#include "musicexpr.h"
#include "track.h"

SLIST_HEAD(tracklist, track);

struct song {
	struct tracklist	tracklist;
	struct track	       *default_drumtrack;
	struct track	       *default_tonedtrack;
};

__BEGIN_DECLS
struct song    *_mdl_song_new(struct musicexpr *, int);
struct track   *_mdl_song_find_track_or_new(struct song *, char *, int);
void		_mdl_song_free(struct song *);
__END_DECLS

#endif /* !MDL_SONG_H */
