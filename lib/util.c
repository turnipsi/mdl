/* $Id: util.c,v 1.24 2016/03/23 20:17:25 je Exp $ */

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
#define INDENTLEVELS 128

extern char *mdl_process_type;
extern char *__progname;

struct {
	int initialized;
	u_int32_t opts;
	struct {
		char *msg;
		enum logtype type;
	} messages[INDENTLEVELS];
} logstate = { 0, 0, {} };

static const char *logtype_strings[] = {
	"exprcloning",	/* MDLLOG_EXPRCLONING */
	"exprconv",	/* MDLLOG_EXPRCONV    */
	"joins",	/* MDLLOG_JOINS       */
	"midi",		/* MDLLOG_MIDI        */
	"midistream",	/* MDLLOG_MIDISTREAM  */
	"musicexpr",	/* MDLLOG_MUSICEXPR   */
	"parsing",	/* MDLLOG_PARSING     */
	"process",	/* MDLLOG_PROCESS     */
	"relative",	/* MDLLOG_RELATIVE    */
	"song",		/* MDLLOG_SONG        */
};

void
mdl_logging_init(void)
{
	int i;

	logstate.opts = 0;

	for (i = 0; i < INDENTLEVELS; i++)
		logstate.messages[i].msg = NULL;

	logstate.initialized = 1;
}

int
mdl_logging_setopts(char *optstring)
{
	char *opt;
	int found, logtype, loglevel;

	assert(logstate.initialized);

	logstate.opts = 0;

	for (;;) {
		if ((opt = strsep(&optstring, ",")) == NULL)
			break;

		found = 0;

		if (strcmp(opt, "all") == 0) {
			for (logtype = 0; logtype < MDLLOG_TYPECOUNT;
			    logtype++) {
				logstate.opts |= (1 << logtype);
			}
			continue;
		}

		for (logtype = 0; logtype < MDLLOG_TYPECOUNT; logtype++) {
			if (strcmp(opt, logtype_strings[logtype]) == 0) {
				logstate.opts |= (1 << logtype);
				found = 1;
				break;
			}
		}

		if (found)
			continue;

		loglevel = strtonum(opt, 1, 4, NULL);
		if (loglevel == 0) {
			warnx("unknown debugging option: %s", opt);
			return -1;
		}

		if (loglevel >= 1) {
			logstate.opts |= (1 << MDLLOG_PROCESS)
			    | (1 << MDLLOG_PARSING);
		}

		if (loglevel >= 2) {
			logstate.opts |= (1 << MDLLOG_RELATIVE)
			    | (1 << MDLLOG_SONG);
		}

		if (loglevel >= 3) {
			logstate.opts |= (1 << MDLLOG_MIDI)
			    | (1 << MDLLOG_MIDISTREAM);
		}

		if (loglevel >= 4) {
			logstate.opts |= (1 << MDLLOG_EXPRCLONING)
			    | (1 << MDLLOG_EXPRCONV)
			    | (1 << MDLLOG_JOINS)
			    | (1 << MDLLOG_MUSICEXPR);
		}
	}

	return 0;
}

void
mdl_logging_clear(void)
{
	int i;

	assert(logstate.initialized);

	for (i = 0; i < INDENTLEVELS; i++)
		if (logstate.messages[i].msg != NULL) {
			free(logstate.messages[i].msg);
			logstate.messages[i].msg = NULL;
		}
}

void
mdl_logging_close(void)
{
	assert(logstate.initialized);

	mdl_logging_clear();

	logstate.initialized = 0;
	logstate.opts = 0;
}

void
mdl_log(enum logtype logtype, int indentlevel, const char *fmt, ...)
{
	va_list va;
	int padding_length, ret, i;

	assert(logstate.initialized);

	assert(logtype < MDLLOG_TYPECOUNT);
	assert(indentlevel >= 0);

	if (indentlevel >= INDENTLEVELS) {
		warnx("maximum indentlevel reached: %d (maximum is %d)",
		    indentlevel, INDENTLEVELS);
		return;
	}

	if (logstate.messages[indentlevel].msg != NULL) {
		free(logstate.messages[indentlevel].msg);
		logstate.messages[indentlevel].msg = NULL;
	}

	va_start(va, fmt);
	ret = vasprintf(&logstate.messages[indentlevel].msg, fmt, va);
	va_end(va);
	if (ret == -1) {
		warnx("vasprintf error in mdl_log");
		logstate.messages[indentlevel].msg = NULL;
		return;
	}

	logstate.messages[indentlevel].type = logtype;

	if (((1 << logtype) & logstate.opts) == 0)
		return;

	for (i = 0; i <= indentlevel; i++) {
		if (logstate.messages[i].msg != NULL) {
			padding_length = sizeof("exprcloning") +
			    sizeof("interp") - strlen(mdl_process_type) - 1;
			assert(padding_length >= 0);
			ret = printf("%s.%s.%-*s: %*s%s", __progname,
			    mdl_process_type, padding_length,
			    logtype_strings[ logstate.messages[i].type ],
			    (2 * i), "", logstate.messages[i].msg);
			if (ret < 0) {
				warnx("printf error in mdl_log");
				break;
			}
		}
	}

	for (i = 0; i < INDENTLEVELS; i++) {
		if (logstate.messages[i].msg != NULL) {
			free(logstate.messages[i].msg);
			logstate.messages[i].msg = NULL;
		}
	}
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
