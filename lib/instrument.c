/* $Id: instrument.c,v 1.6 2016/05/10 20:39:43 je Exp $ */

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

#include <sys/types.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "instrument.h"
#include "util.h"

static int	compare_instruments(const void *, const void *);

struct instrument *
_mdl_get_instrument(enum instrument_type type, char *instrument_name)
{
	static const struct instrument drumkits[] = {
		{ INSTR_DRUMKIT, "brush drums",       40 },
		{ INSTR_DRUMKIT, "brush kit",         40 },
		{ INSTR_DRUMKIT, "classical drums",   48 },
		{ INSTR_DRUMKIT, "cm-64 drums",      127 },
		{ INSTR_DRUMKIT, "cm-64 kit",        127 },
		{ INSTR_DRUMKIT, "drums",              0 },
			/* LONGEST DRUMKIT */
		{ INSTR_DRUMKIT, "electronic drums",  24 },
		{ INSTR_DRUMKIT, "electronic kit",    24 },
		{ INSTR_DRUMKIT, "jazz drums",        32 },
		{ INSTR_DRUMKIT, "jazz kit",          32 },
		{ INSTR_DRUMKIT, "mt-32 drums",      127 },
		{ INSTR_DRUMKIT, "mt-32 kit",        127 },
		{ INSTR_DRUMKIT, "orchestra drums",   48 },
		{ INSTR_DRUMKIT, "orchestra kit",     48 },
		{ INSTR_DRUMKIT, "power drums",       16 },
		{ INSTR_DRUMKIT, "power kit",         16 },
		{ INSTR_DRUMKIT, "rock drums",        16 },
		{ INSTR_DRUMKIT, "room drums",         8 },
		{ INSTR_DRUMKIT, "room kit",           8 },
		{ INSTR_DRUMKIT, "sfx drums",         56 },
		{ INSTR_DRUMKIT, "sfx kit",           56 },
		{ INSTR_DRUMKIT, "standard drums",     0 },
		{ INSTR_DRUMKIT, "standard kit",       0 },
		{ INSTR_DRUMKIT, "tr-808 drums",      25 },
		{ INSTR_DRUMKIT, "tr-808 kit",        25 },
	};

	static const struct instrument instruments[] = {
		{ INSTR_TONED, "accordion",                21 },
		{ INSTR_TONED, "acoustic bass",            32 },
		{ INSTR_TONED, "acoustic grand",            0 },
			/* LONGEST INSTR_TONED */
		{ INSTR_TONED, "acoustic guitar (nylon)",  24 },
		{ INSTR_TONED, "acoustic guitar (steel)",  25 },
		{ INSTR_TONED, "agogo",                   113 },
		{ INSTR_TONED, "alto sax",                 65 },
		{ INSTR_TONED, "applause",                126 },
		{ INSTR_TONED, "bagpipe",                 109 },
		{ INSTR_TONED, "banjo",                   105 },
		{ INSTR_TONED, "baritone sax",             67 },
		{ INSTR_TONED, "bassoon",                  70 },
		{ INSTR_TONED, "bird tweet",              123 },
		{ INSTR_TONED, "blown bottle",             76 },
		{ INSTR_TONED, "brass section",            61 },
		{ INSTR_TONED, "breath noise",            121 },
		{ INSTR_TONED, "bright acoustic",           1 },
		{ INSTR_TONED, "celesta",                   8 },
		{ INSTR_TONED, "cello",                    42 },
		{ INSTR_TONED, "choir aahs",               52 },
		{ INSTR_TONED, "church organ",             19 },
		{ INSTR_TONED, "clarinet",                 71 },
		{ INSTR_TONED, "clav",                      7 },
		{ INSTR_TONED, "concertina",               23 },
		{ INSTR_TONED, "contrabass",               43 },
		{ INSTR_TONED, "distorted guitar",         30 },
		{ INSTR_TONED, "drawbar organ",            16 },
		{ INSTR_TONED, "dulcimer",                 15 },
		{ INSTR_TONED, "electric bass (finger)",   33 },
		{ INSTR_TONED, "electric bass (pick)",     34 },
		{ INSTR_TONED, "electric grand",            2 },
		{ INSTR_TONED, "electric guitar (clean)",  27 },
		{ INSTR_TONED, "electric guitar (jazz)",   26 },
		{ INSTR_TONED, "electric guitar (muted)",  28 },
		{ INSTR_TONED, "electric piano 1",          4 },
		{ INSTR_TONED, "electric piano 2",          5 },
		{ INSTR_TONED, "english horn",             69 },
		{ INSTR_TONED, "fiddle",                  110 },
		{ INSTR_TONED, "flute",                    73 },
		{ INSTR_TONED, "french horn",              60 },
		{ INSTR_TONED, "fretless bass",            35 },
		{ INSTR_TONED, "fx 1 (rain)",              96 },
		{ INSTR_TONED, "fx 2 (soundtrack)",        97 },
		{ INSTR_TONED, "fx 3 (crystal)",           98 },
		{ INSTR_TONED, "fx 4 (atmosphere)",        99 },
		{ INSTR_TONED, "fx 5 (brightness)",       100 },
		{ INSTR_TONED, "fx 6 (goblins)",          101 },
		{ INSTR_TONED, "fx 7 (echoes)",           102 },
		{ INSTR_TONED, "fx 8 (sci-fi)",           103 },
		{ INSTR_TONED, "glockenspiel",              9 },
		{ INSTR_TONED, "guitar fret noise",       120 },
		{ INSTR_TONED, "guitar harmonics",         31 },
		{ INSTR_TONED, "gunshot",                 127 },
		{ INSTR_TONED, "harmonica",                22 },
		{ INSTR_TONED, "harpsichord",               6 },
		{ INSTR_TONED, "helicopter",              125 },
		{ INSTR_TONED, "honky-tonk",                3 },
		{ INSTR_TONED, "kalimba",                 108 },
		{ INSTR_TONED, "koto",                    107 },
		{ INSTR_TONED, "lead 1 (square)",          80 },
		{ INSTR_TONED, "lead 2 (sawtooth)",        81 },
		{ INSTR_TONED, "lead 3 (calliope)",        82 },
		{ INSTR_TONED, "lead 4 (chiff)",           83 },
		{ INSTR_TONED, "lead 5 (charang)",         84 },
		{ INSTR_TONED, "lead 6 (voice)",           85 },
		{ INSTR_TONED, "lead 7 (fifths)",          86 },
		{ INSTR_TONED, "lead 8 (bass+lead)",       87 },
		{ INSTR_TONED, "marimba",                  12 },
		{ INSTR_TONED, "melodic tom",             117 },
		{ INSTR_TONED, "music box",                10 },
		{ INSTR_TONED, "muted trumpet",            59 },
		{ INSTR_TONED, "oboe",                     68 },
		{ INSTR_TONED, "ocarina",                  79 },
		{ INSTR_TONED, "orchestra hit",            55 },
		{ INSTR_TONED, "orchestral harp",          46 },
		{ INSTR_TONED, "overdriven guitar",        29 },
		{ INSTR_TONED, "pad 1 (new age)",          88 },
		{ INSTR_TONED, "pad 2 (warm)",             89 },
		{ INSTR_TONED, "pad 3 (polysynth)",        90 },
		{ INSTR_TONED, "pad 4 (choir)",            91 },
		{ INSTR_TONED, "pad 5 (bowed)",            92 },
		{ INSTR_TONED, "pad 6 (metallic)",         93 },
		{ INSTR_TONED, "pad 7 (halo)",             94 },
		{ INSTR_TONED, "pad 8 (sweep)",            95 },
		{ INSTR_TONED, "pan flute",                75 },
		{ INSTR_TONED, "percussive organ",         17 },
		{ INSTR_TONED, "piccolo",                  72 },
		{ INSTR_TONED, "pizzicato strings",        45 },
		{ INSTR_TONED, "recorder",                 74 },
		{ INSTR_TONED, "reed organ",               20 },
		{ INSTR_TONED, "reverse cymbal",          119 },
		{ INSTR_TONED, "rock organ",               18 },
		{ INSTR_TONED, "seashore",                122 },
		{ INSTR_TONED, "shakuhachi",               77 },
		{ INSTR_TONED, "shamisen",                106 },
		{ INSTR_TONED, "shanai",                  111 },
		{ INSTR_TONED, "sitar",                   104 },
		{ INSTR_TONED, "slap bass 1",              36 },
		{ INSTR_TONED, "slap bass 2",              37 },
		{ INSTR_TONED, "soprano sax",              64 },
		{ INSTR_TONED, "steel drums",             114 },
		{ INSTR_TONED, "string ensemble 1",        48 },
		{ INSTR_TONED, "string ensemble 2",        49 },
		{ INSTR_TONED, "synth bass 1",             38 },
		{ INSTR_TONED, "synth bass 2",             39 },
		{ INSTR_TONED, "synth drum",              118 },
		{ INSTR_TONED, "synth voice",              54 },
		{ INSTR_TONED, "synthbrass 1",             62 },
		{ INSTR_TONED, "synthbrass 2",             63 },
		{ INSTR_TONED, "synthstrings 1",           50 },
		{ INSTR_TONED, "synthstrings 2",           51 },
		{ INSTR_TONED, "taiko drum",              116 },
		{ INSTR_TONED, "telephone ring",          124 },
		{ INSTR_TONED, "tenor sax",                66 },
		{ INSTR_TONED, "timpani",                  47 },
		{ INSTR_TONED, "tinkle bell",             112 },
		{ INSTR_TONED, "tremolo strings",          44 },
		{ INSTR_TONED, "trombone",                 57 },
		{ INSTR_TONED, "trumpet",                  56 },
		{ INSTR_TONED, "tuba",                     58 },
		{ INSTR_TONED, "tubular bells",            14 },
		{ INSTR_TONED, "vibraphone",               11 },
		{ INSTR_TONED, "viola",                    41 },
		{ INSTR_TONED, "violin",                   40 },
		{ INSTR_TONED, "voice oohs",               53 },
		{ INSTR_TONED, "whistle",                  78 },
		{ INSTR_TONED, "woodblock",               115 },
		{ INSTR_TONED, "xylophone",                13 },
	};
	const struct instrument *instrument_table;
	size_t tablesize;

	assert(type == INSTR_DRUMKIT || type == INSTR_TONED);

	if (type == INSTR_DRUMKIT) {
		instrument_table = drumkits;
		tablesize = sizeof(drumkits);
	} else {
		instrument_table = instruments;
		tablesize = sizeof(instruments);
	}

	/* This might return NULL, that is ok. */
	return bsearch(instrument_name, instrument_table,
	    (tablesize / sizeof(struct instrument)), sizeof(struct instrument),
	    compare_instruments);
}

static int
compare_instruments(const void *v_name, const void *v_instrument)
{
	const struct instrument *instrument;
	const char *name;

	name = v_name;
	instrument = v_instrument;

	return strncmp(name, instrument->name, LONGEST_INSTRUMENT_SIZE);
}
