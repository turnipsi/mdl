/* $Id: instrument.c,v 1.1 2016/01/24 21:04:35 je Exp $ */

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

#include "instrument.h"

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#define LONGEST_DRUMKIT_NAME "electronic drums"
struct drumkit_t {
	const char name[ sizeof(LONGEST_DRUMKIT_NAME) ];
	u_int8_t code;
};

#define LONGEST_INSTRUMENT_NAME "acoustic guitar (nylon)"
struct instrument_t {
	const char name[ sizeof(LONGEST_INSTRUMENT_NAME) ];
	u_int8_t code;
};

static int	compare_drumkits(const void *, const void *);
static int	compare_instruments(const void *, const void *);

int
get_drumkit_code(char *drumkit_name)
{
	static const struct drumkit_t codes[] = {
		{ "brush drums",       40 },
		{ "brush kit",         40 },
		{ "classical drums",   48 },
		{ "cm-64 drums",      127 },
		{ "cm-64 kit",        127 },
		{ "drums",              0 },
			/* LONGEST_DRUMKIT_NAME: */
		{ "electronic drums",  24 },
		{ "electronic kit",    24 },
		{ "jazz drums",        32 },
		{ "jazz kit",          32 },
		{ "mt-32 drums",      127 },
		{ "mt-32 kit",        127 },
		{ "orchestra drums",   48 },
		{ "orchestra kit",     48 },
		{ "power drums",       16 },
		{ "power kit",         16 },
		{ "rock drums",        16 },
		{ "room drums",         8 },
		{ "room kit",           8 },
		{ "sfx drums",         56 },
		{ "sfx kit",           56 },
		{ "standard drums",     0 },
		{ "standard kit",       0 },
		{ "tr-808 drums",      25 },
		{ "tr-808 kit",        25 },
	};
	struct drumkit_t *drumkit;

	drumkit = bsearch(drumkit_name,
			  codes,
			  sizeof(codes) / sizeof(struct drumkit_t),
			  sizeof(struct drumkit_t),
			  compare_drumkits);
	if (drumkit == NULL)
		return -1;

	return drumkit->code;
}

static int
compare_drumkits(const void *a, const void *b)
{
	const struct drumkit_t *drumkit_a, *drumkit_b;

	drumkit_a = a;
	drumkit_b = b;

	return strncmp(drumkit_a->name,
		       drumkit_b->name,
		       sizeof(LONGEST_DRUMKIT_NAME));
}

int
get_instrument_code(char *instrument_name)
{
	static const struct instrument_t codes[] = {
		{ "acoustic grand",            0 },
		{ "bright acoustic",           1 },
		{ "electric grand",            2 },
		{ "honky-tonk",                3 },
		{ "electric piano 1",          4 },
		{ "electric piano 2",          5 },
		{ "harpsichord",               6 },
		{ "clav",                      7 },

		{ "celesta",                   8 },
		{ "glockenspiel",              9 },
		{ "music box",                10 },
		{ "vibraphone",               11 },
		{ "marimba",                  12 },
		{ "xylophone",                13 },
		{ "tubular bells",            14 },
		{ "dulcimer",                 15 },

		{ "drawbar organ",            16 },
		{ "percussive organ",         17 },
		{ "rock organ",               18 },
		{ "church organ",             19 },
		{ "reed organ",               20 },
		{ "accordion",                21 },
		{ "harmonica",                22 },
		{ "concertina",               23 },

			/* LONGEST_INSTRUMENT_NAME: */
		{ "acoustic guitar (nylon)",  24 },
		{ "acoustic guitar (steel)",  25 },
		{ "electric guitar (jazz)",   26 },
		{ "electric guitar (clean)",  27 },
		{ "electric guitar (muted)",  28 },
		{ "overdriven guitar",        29 },
		{ "distorted guitar",         30 },
		{ "guitar harmonics",         31 },

		{ "acoustic bass",            32 },
		{ "electric bass (finger)",   33 },
		{ "electric bass (pick)",     34 },
		{ "fretless bass",            35 },
		{ "slap bass 1",              36 },
		{ "slap bass 2",              37 },
		{ "synth bass 1",             38 },
		{ "synth bass 2",             39 },

		{ "violin",                   40 },
		{ "viola",                    41 },
		{ "cello",                    42 },
		{ "contrabass",               43 },
		{ "tremolo strings",          44 },
		{ "pizzicato strings",        45 },
		{ "orchestral harp",          46 },
		{ "timpani",                  47 },

		{ "string ensemble 1",        48 },
		{ "string ensemble 2",        49 },
		{ "synthstrings 1",           50 },
		{ "synthstrings 2",           51 },
		{ "choir aahs",               52 },
		{ "voice oohs",               53 },
		{ "synth voice",              54 },
		{ "orchestra hit",            55 },

		{ "trumpet",                  56 },
		{ "trombone",                 57 },
		{ "tuba",                     58 },
		{ "muted trumpet",            59 },
		{ "french horn",              60 },
		{ "brass section",            61 },
		{ "synthbrass 1",             62 },
		{ "synthbrass 2",             63 },

		{ "soprano sax",              64 },
		{ "alto sax",                 65 },
		{ "tenor sax",                66 },
		{ "baritone sax",             67 },
		{ "oboe",                     68 },
		{ "english horn",             69 },
		{ "bassoon",                  70 },
		{ "clarinet",                 71 },

		{ "piccolo",                  72 },
		{ "flute",                    73 },
		{ "recorder",                 74 },
		{ "pan flute",                75 },
		{ "blown bottle",             76 },
		{ "shakuhachi",               77 },
		{ "whistle",                  78 },
		{ "ocarina",                  79 },

		{ "lead 1 (square)",          80 },
		{ "lead 2 (sawtooth)",        81 },
		{ "lead 3 (calliope)",        82 },
		{ "lead 4 (chiff)",           83 },
		{ "lead 5 (charang)",         84 },
		{ "lead 6 (voice)",           85 },
		{ "lead 7 (fifths)",          86 },
		{ "lead 8 (bass+lead)",       87 },

		{ "pad 1 (new age)",          88 },
		{ "pad 2 (warm)",             89 },
		{ "pad 3 (polysynth)",        90 },
		{ "pad 4 (choir)",            91 },
		{ "pad 5 (bowed)",            92 },
		{ "pad 6 (metallic)",         93 },
		{ "pad 7 (halo)",             94 },
		{ "pad 8 (sweep)",            95 },

		{ "fx 1 (rain)",              96 },
		{ "fx 2 (soundtrack)",        97 },
		{ "fx 3 (crystal)",           98 },
		{ "fx 4 (atmosphere)",        99 },
		{ "fx 5 (brightness)",       100 },
		{ "fx 6 (goblins)",          101 },
		{ "fx 7 (echoes)",           102 },
		{ "fx 8 (sci-fi)",           103 },

		{ "sitar",                   104 },
		{ "banjo",                   105 },
		{ "shamisen",                106 },
		{ "koto",                    107 },
		{ "kalimba",                 108 },
		{ "bagpipe",                 109 },
		{ "fiddle",                  110 },
		{ "shanai",                  111 },

		{ "tinkle bell",             112 },
		{ "agogo",                   113 },
		{ "steel drums",             114 },
		{ "woodblock",               115 },
		{ "taiko drum",              116 },
		{ "melodic tom",             117 },
		{ "synth drum",              118 },
		{ "reverse cymbal",          119 },

		{ "guitar fret noise",       120 },
		{ "breath noise",            121 },
		{ "seashore",                122 },
		{ "bird tweet",              123 },
		{ "telephone ring",          124 },
		{ "helicopter",              125 },
		{ "applause",                126 },
		{ "gunshot",                 127 },
	};
	struct instrument_t *instrument;

	instrument = bsearch(instrument_name,
			     codes,
			     sizeof(codes) / sizeof(struct instrument_t),
			     sizeof(struct instrument_t),
			     compare_instruments);
	if (instrument == NULL)
		return -1;

	return instrument->code;
}

static int
compare_instruments(const void *a, const void *b)
{
	const struct instrument_t *instrument_a, *instrument_b;

	instrument_a = a;
	instrument_b = b;

	return strncmp(instrument_a->name,
		       instrument_b->name,
		       sizeof(LONGEST_INSTRUMENT_NAME));
}
