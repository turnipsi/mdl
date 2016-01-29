/* $Id: song.h,v 1.2 2016/01/29 20:51:26 je Exp $ */

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

#include "instrument.h"
#include "midi.h"

SLIST_HEAD(tracklist_t, track_t);
struct track_t {
	char *trackname;
	SLIST_ENTRY(track_t) sl;
};

struct song_t {
	struct tracklist_t tracklist;
	struct track_t *default_track;
};

struct song_t  *mdl_song_new(int);
struct track_t *mdl_song_find_track_or_new(struct song_t *, char *, int);
void		mdl_song_free(struct song_t *);

#endif
