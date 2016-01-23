/* $Id: interpreter.c,v 1.37 2016/01/23 19:15:42 je Exp $ */
 
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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compat.h"
#include "midistream.h"
#include "musicexpr.h"
#include "util.h"

extern FILE			*yyin;
extern struct musicexpr_t	*parsed_expr;

int	yyparse(void);

int
handle_musicfile_and_socket(int file_fd,
			    int main_socket,
			    int sequencer_socket,
			    int server_socket)
{
	struct mdl_stream *eventstream;
	int level, ret;

	/* XXX main_socket not yet used for anything */

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

	if (yyparse() != 0) {
		warnx("yyparse returned error");
		return 1;
	}

	/* if yyparse() returned ok, we should have parsed_expr != NULL
	 * and available for us now */

	if (parsed_expr->me_type != ME_TYPE_SEQUENCE) {
		warnx("expected sequence");
		ret = 1;
		goto finish;
	}

	mdl_log(1, level, "parse ok, got parse result:\n");
	musicexpr_log(parsed_expr, 2, level + 1, NULL);

	eventstream = musicexpr_to_midievents(parsed_expr, level);
	if (eventstream == NULL) {
		warnx("error converting music expression to midi stream");
		ret = 1;
		goto finish;
	}

	mdl_log(1, level, "writing midi stream to sequencer\n");
	/* XXX */
	(void) midi_write_midistream(sequencer_socket,
				     eventstream,
				     level + 1);

finish:
	if (eventstream)
		mdl_stream_free(eventstream);
	if (parsed_expr)
		musicexpr_free(parsed_expr);

	return ret;
}
