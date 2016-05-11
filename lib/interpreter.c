/* $Id: interpreter.c,v 1.53 2016/05/11 20:30:01 je Exp $ */

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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "interpreter.h"
#include "midistream.h"
#include "musicexpr.h"
#include "util.h"

extern FILE		*yyin;
extern struct musicexpr	*parsed_expr;
extern unsigned int	 parse_errors;

int
_mdl_handle_musicfile_and_socket(int file_fd, int main_socket,
    int sequencer_socket, int server_socket)
{
	struct mdl_stream *eventstream;
	ssize_t wcount;
	int level, ret;

	assert(file_fd >= 0);
	assert(main_socket >= 0 || 1);		/* XXX not used yet */
	assert(sequencer_socket >= 0);
	assert(server_socket >= 0 || 1);	/* XXX not used yet */

	eventstream = NULL;
	level = 0;
	ret = 0;

        if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		return 1;
	}

	if ((yyin = fdopen(file_fd, "r")) == NULL) {
		warn("could not setup input stream for lex");
		return 1;
	}

	if (yyparse() != 0 || parse_errors > 0) {
		warnx("_mdl_parse returned error");
		return 1;
	}

	/*
	 * If _mdl_parse() returned ok, we should have parsed_expr != NULL
	 * and available for us now.
	 */

	_mdl_log(MDLLOG_PARSING, level, "parse ok, result:\n");
	_mdl_musicexpr_log(parsed_expr, MDLLOG_PARSING, level+1, NULL);

	eventstream = _mdl_musicexpr_to_midievents(parsed_expr, level);
	if (eventstream == NULL) {
		warnx("error converting music expression to midi stream");
		ret = 1;
		goto finish;
	}

	wcount = _mdl_midi_write_midistream(sequencer_socket, eventstream,
	    level);
	if (wcount == -1)
		ret = 1;

finish:
	if (eventstream)
		_mdl_stream_free(eventstream);
	if (parsed_expr)
		_mdl_musicexpr_free(parsed_expr, level);

	return ret;
}
