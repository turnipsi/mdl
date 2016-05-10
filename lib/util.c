/* $Id: util.c,v 1.30 2016/05/10 20:39:43 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkil� <je@turnipsi.no-ip.org>
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
	"clock",	/* MDLLOG_CLOCK                  */
	"exprconv",	/* MDLLOG_EXPRCONV               */
	"joins",	/* MDLLOG_JOINS                  */
	"midi",		/* MDLLOG_MIDI                   */
	"midistream",	/* MDLLOG_MIDISTREAM             */
	"mm",		/* MDLLOG_MM (memory management) */
	"parsing",	/* MDLLOG_PARSING                */
	"process",	/* MDLLOG_PROCESS                */
	"relative",	/* MDLLOG_RELATIVE               */
	"song",		/* MDLLOG_SONG                   */
};

void
_mdl__mdl_mdl_logging_init(void)
{
	int i;

	assert(MDLLOG_TYPECOUNT <= 32);

	logstate.opts = 0;

	for (i = 0; i < INDENTLEVELS; i++)
		logstate.messages[i].msg = NULL;

	logstate.initialized = 1;
}

int
_mdl__mdl_mdl_logging_setopts(char *optstring)
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
			logstate.opts |= (1 << MDLLOG_CLOCK)
			    | (1 << MDLLOG_EXPRCONV)
			    | (1 << MDLLOG_JOINS)
			    | (1 << MDLLOG_MM);
		}
	}

	return 0;
}

void
_mdl__mdl_mdl_logging_clear(void)
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
_mdl__mdl_mdl_logging_close(void)
{
	assert(logstate.initialized);

	_mdl__mdl_mdl_logging_clear();

	logstate.initialized = 0;
	logstate.opts = 0;
}

int
_mdl__mdl_mdl_log_checkopt(enum logtype logtype)
{
	assert(logtype < MDLLOG_TYPECOUNT);

	return (logstate.opts & (1 << logtype));
}

void
_mdl_mdl_log(enum logtype logtype, int level, const char *fmt, ...)
{
	va_list va;
	int padding_length, ret, i;

	assert(logstate.initialized);

	assert(logtype < MDLLOG_TYPECOUNT);
	assert(level >= 0);

	if (level >= INDENTLEVELS) {
		warnx("maximum indentlevel reached: %d (maximum is %d)",
		    level, INDENTLEVELS);
		return;
	}

	if (logstate.messages[level].msg != NULL) {
		free(logstate.messages[level].msg);
		logstate.messages[level].msg = NULL;
	}

	va_start(va, fmt);
	ret = vasprintf(&logstate.messages[level].msg, fmt, va);
	va_end(va);
	if (ret == -1) {
		warnx("vasprintf error in _mdl_mdl_log");
		logstate.messages[level].msg = NULL;
		return;
	}

	logstate.messages[level].type = logtype;

	if (((1 << logtype) & logstate.opts) == 0)
		return;

	for (i = 0; i <= level; i++) {
		if (logstate.messages[i].msg != NULL) {
			padding_length = sizeof("exprcloning") +
			    sizeof("interp") - strlen(mdl_process_type) - 1;
			assert(padding_length >= 0);
			ret = printf("%s.%s.%-*s: %*s%s", __progname,
			    mdl_process_type, padding_length,
			    logtype_strings[ logstate.messages[i].type ],
			    (2 * i), "", logstate.messages[i].msg);
			if (ret < 0) {
				warnx("printf error in _mdl_mdl_log");
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
_mdl_mdl_stream_new(enum streamtype s_type)
{
	struct mdl_stream *s;

	if ((s = malloc(sizeof(struct mdl_stream))) == NULL) {
		warn("malloc in _mdl_mdl_stream_new");
		return NULL;
	}

	s->count = 0;
	s->slotcount = DEFAULT_SLOTCOUNT;
	s->s_type = s_type;

	switch (s->s_type) {
	case MIDIEVENTSTREAM:
		s->u.midievents = calloc(s->slotcount,
		    sizeof(struct midievent));
		if (s->u.midievents == NULL) {
			warn("calloc in _mdl_mdl_stream_new");
			free(s);
			return NULL;
		}
		break;
	case OFFSETEXPRSTREAM:
		s->u.mexprs = calloc(s->slotcount, sizeof(struct offsetexpr));
		if (s->u.mexprs == NULL) {
			warn("calloc in _mdl_mdl_stream_new");
			free(s);
			return NULL;
		}
		break;
	case TRACKMIDIEVENTSTREAM:
		s->u.trackmidinotes = calloc(s->slotcount,
		    sizeof(struct trackmidinote));
		if (s->u.trackmidinotes == NULL) {
			warn("calloc in _mdl_mdl_stream_new");
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
_mdl_mdl_stream_increment(struct mdl_stream *s)
{
	void *new_items;

	s->count += 1;
	if (s->count == s->slotcount) {
		_mdl_mdl_log(MDLLOG_MIDISTREAM, 0,
		    "mdl_stream now contains %d items\n", s->count);
		s->slotcount *= 2;
		switch (s->s_type) {
		case MIDIEVENTSTREAM:
			new_items = reallocarray(s->u.midievents, s->slotcount,
			    sizeof(struct midievent));
			if (new_items == NULL) {
				warn("reallocarray in _mdl_mdl_stream_increment");
				return 1;
			}
			s->u.midievents = new_items;
			break;
		case OFFSETEXPRSTREAM:
			new_items = reallocarray(s->u.mexprs, s->slotcount,
			    sizeof(struct offsetexpr));
			if (new_items == NULL) {
				warn("reallocarray in _mdl_mdl_stream_increment");
				return 1;
			}
			s->u.mexprs = new_items;
			break;
		case TRACKMIDIEVENTSTREAM:
			new_items = reallocarray(s->u.trackmidinotes,
			    s->slotcount, sizeof(struct trackmidinote));
			if (new_items == NULL) {
				warn("reallocarray in _mdl_mdl_stream_increment");
				return 1;
			}
			s->u.trackmidinotes = new_items;
			break;
		default:
			assert(0);
		}
	}

	return 0;
}

void
_mdl_mdl_stream_free(struct mdl_stream *s)
{
	switch (s->s_type) {
	case MIDIEVENTSTREAM:
		free(s->u.midievents);
		break;
	case OFFSETEXPRSTREAM:
		free(s->u.mexprs);
		break;
	case TRACKMIDIEVENTSTREAM:
		free(s->u.trackmidinotes);
		break;
	default:
		assert(0);
	}

	free(s);
}

void __dead
_mdl_unimplemented(void)
{
	warnx("_mdl_unimplemented functionality triggered");
	abort();
}
