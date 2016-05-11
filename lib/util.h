/* $Id: util.h,v 1.26 2016/05/11 09:11:37 je Exp $ */

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

#ifndef MDL_UTIL_H
#define MDL_UTIL_H

#include <unistd.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

/* There should not be more than 32 different MDLLOG_* types. */
enum logtype {
	MDLLOG_CLOCK,
	MDLLOG_EXPRCONV,
	MDLLOG_JOINS,
	MDLLOG_MIDI,
	MDLLOG_MIDISTREAM,
	MDLLOG_MM,		/* memory management */
	MDLLOG_PARSING,
	MDLLOG_PROCESS,
	MDLLOG_RELATIVE,
	MDLLOG_SONG,
	MDLLOG_TYPECOUNT,	/* not a logtype */
};

struct mdl_stream {
	size_t count, slotcount;
	enum streamtype {
		MIDIEVENTSTREAM,
		OFFSETEXPRSTREAM,
		TRACKMIDIEVENTSTREAM,
	} s_type;
	union {
		struct offsetexpr	*mexprs;
		struct midievent	*midievents;
		struct trackmidinote	*trackmidinotes;
	} u;
};

__BEGIN_DECLS
void	_mdl_mdl_log(enum logtype, int, const char *, ...);
void	_mdl_logging_init(void);
void	_mdl_logging_clear(void);
int	_mdl_logging_setopts(char *);
void	_mdl_logging_close(void);
int	_mdl_log_checkopt(enum logtype);

struct mdl_stream      *_mdl_mdl_stream_new(enum streamtype);
int			_mdl_mdl_stream_increment(struct mdl_stream *);
void			_mdl_mdl_stream_free(struct mdl_stream *);
void __dead		_mdl_unimplemented(void);
__END_DECLS

#endif /* !MDL_UTIL_H */
