/* $Id: util.c,v 1.20 2016/02/24 20:29:08 je Exp $ */

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

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "midi.h"
#include "musicexpr.h"
#include "util.h"

#define DEFAULT_SLOTCOUNT 1024

extern char *mdl_process_type;
extern char *__progname;

u_int32_t logopts;

static const char *logtype_strings[] = {
	"exprcloning",	/* MDLLOG_EXPRCLONING */
	"exprconv",	/* MDLLOG_EXPRCONV    */
	"joins",	/* MDLLOG_JOINS       */
	"midi",		/* MDLLOG_MIDI        */
	"midistream",	/* MDLLOG_MIDISTREAM  */
	"parsing",	/* MDLLOG_PARSING     */
	"process",	/* MDLLOG_PROCESS     */
	"relative",	/* MDLLOG_RELATIVE    */
	"song",		/* MDLLOG_SONG        */
};

int
setup_logging_opts(char *optstring)
{
	char *opt;
	int found, logtype, loglevel;

	logopts = 0;

	for (;;) {
		if ((opt = strsep(&optstring, ",")) == NULL)
			break;

		found = 0;

		for (logtype = 0; logtype < MDLLOG_TYPECOUNT; logtype++) {
			if (strcmp(opt, logtype_strings[logtype]) == 0) {
				logopts |= (1 << logtype);
				found = 1;
				break;
			}
		}

		if (!found) {
			loglevel = strtonum(opt, 1, 4, NULL);
			if (loglevel == 0) {
				warnx("unknown debugging option: %s", opt);
				return -1;
			}

			if (loglevel >= 1) {
				logopts |= (1 << MDLLOG_PROCESS)
				    | (1 << MDLLOG_PARSING);
			}

			if (loglevel >= 2) {
				logopts |= (1 << MDLLOG_RELATIVE)
				    | (1 << MDLLOG_SONG);
			}

			if (loglevel >= 3) {
				logopts |= (1 << MDLLOG_MIDI)
				    | (1 << MDLLOG_MIDISTREAM);
			}

			if (loglevel >= 4) {
				logopts |= (1 << MDLLOG_EXPRCLONING)
				    | (1 << MDLLOG_EXPRCONV)
				    | (1 << MDLLOG_JOINS);
			}
		}
	}

	return 0;
}

void
mdl_log(enum logtype logtype, int indentlevel, const char *fmt, ...)
{
	va_list va;
	int ret, i;

	assert(logtype < MDLLOG_TYPECOUNT);
	assert(indentlevel >= 0);

	if (((1 << logtype) & logopts) == 0)
		return;

	ret = printf("%s/%s(%s): ", __progname, mdl_process_type,
	    logtype_strings[logtype]);
	if (ret < 0)
		return;

	for (i = 0; i < indentlevel; i++) {
		ret = printf("  ");
		if (ret < 0)
			return;
	}

	va_start(va, fmt);
	(void) vprintf(fmt, va);
	va_end(va);
}

struct mdl_stream *
mdl_stream_new(enum streamtype s_type)
{
	struct mdl_stream *s;

	if ((s = malloc(sizeof(struct mdl_stream))) == NULL) {
		warn("malloc in mdl_stream_new");
		return NULL;
	}

	s->count = 0;
	s->slotcount = DEFAULT_SLOTCOUNT;
	s->s_type = s_type;

	switch (s->s_type) {
	case MIDIEVENTSTREAM:
		s->midievents = calloc(s->slotcount, sizeof(struct midievent));
		if (s->midievents == NULL) {
			warn("calloc in mdl_stream_new");
			free(s);
			return NULL;
		}
		break;
	case OFFSETEXPRSTREAM:
		s->mexprs = calloc(s->slotcount, sizeof(struct offsetexpr));
		if (s->mexprs == NULL) {
			warn("calloc in mdl_stream_new");
			free(s);
			return NULL;
		}
		break;
	case TRACKMIDIEVENTSTREAM:
		s->trackmidinotes = calloc(s->slotcount,
		    sizeof(struct trackmidinote));
		if (s->trackmidinotes == NULL) {
			warn("calloc in mdl_stream_new");
			free(s);
			return NULL;
		}
		break;
	default:
		assert(0);
	}

	return s;
}

int
mdl_stream_increment(struct mdl_stream *s)
{
	void *new_items;

	s->count += 1;
	if (s->count == s->slotcount) {
		mdl_log(MDLLOG_MIDISTREAM, 0,
		    "mdl_stream now contains %d items\n", s->count);
		s->slotcount *= 2;
		switch (s->s_type) {
		case MIDIEVENTSTREAM:
			new_items = reallocarray(s->midievents, s->slotcount,
			    sizeof(struct midievent));
			if (new_items == NULL) {
				warn("reallocarray in mdl_stream_increment");
				return 1;
			}
			s->midievents = new_items;
			break;
		case OFFSETEXPRSTREAM:
			new_items = reallocarray(s->mexprs, s->slotcount,
			    sizeof(struct offsetexpr));
			if (new_items == NULL) {
				warn("reallocarray in mdl_stream_increment");
				return 1;
			}
			s->mexprs = new_items;
			break;
		case TRACKMIDIEVENTSTREAM:
			new_items = reallocarray(s->trackmidinotes,
			    s->slotcount, sizeof(struct trackmidinote));
			if (new_items == NULL) {
				warn("reallocarray in mdl_stream_increment");
				return 1;
			}
			s->trackmidinotes = new_items;
			break;
		default:
			assert(0);
		}
	}

	return 0;
}

void
mdl_stream_free(struct mdl_stream *s)
{
	switch (s->s_type) {
	case MIDIEVENTSTREAM:
		free(s->midievents);
		break;
	case OFFSETEXPRSTREAM:
		free(s->mexprs);
		break;
	case TRACKMIDIEVENTSTREAM:
		free(s->trackmidinotes);
		break;
	default:
		assert(0);
	}

	free(s);
}

void __dead
unimplemented(void)
{
	warnx("unimplemented functionality triggered");
	exit(1);
}
