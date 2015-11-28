/* $Id: interpreter.c,v 1.31 2015/11/28 08:14:37 je Exp $ */
 
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
#include "midi.h"
#include "musicexpr.h"
#include "util.h"

extern FILE			*yyin;
extern struct musicexpr_t	*parsed_expr;

int		yyparse(void);
static ssize_t	write_midistream_to_sequencer(int, struct midieventstream *);

int
handle_musicfile_and_socket(int file_fd,
			    int main_socket,
			    int sequencer_socket,
			    int server_socket)
{
	struct midieventstream *eventstream;
	int ret;

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

	/* if yyparse() returned ok, we should have parsed_expr available
	 * for us now */

	if (parsed_expr->me_type != ME_TYPE_SEQUENCE) {
		warnx("expected sequence");
		ret = 1;
		goto finish;
	}

	(void) mdl_log(1, "parse ok, got parse result:\n");
	(void) musicexpr_log(0, parsed_expr);

	(void) mdl_log(1, "converting to midi stream\n");
	if ((eventstream = musicexpr_to_midievents(parsed_expr)) == NULL) {
		warnx("error converting music expression to midi stream");
		ret = 1;
		goto finish;
	}

	(void) mdl_log(1, "writing midi stream to sequencer\n");
	(void) write_midistream_to_sequencer(sequencer_socket, eventstream);

finish:
	musicexpr_free(parsed_expr);

	return ret;
}

static ssize_t
write_midistream_to_sequencer(int sequencer_socket, struct midieventstream *es)
{
	size_t wsize;
	ssize_t nw, total_wcount;

	total_wcount = 0;

	/* XXX overflow? */
	wsize = es->params.count * sizeof(struct midievent);

	while (total_wcount < wsize) {
		nw = write(sequencer_socket,
			   (char *) es->events + total_wcount,
			   wsize - total_wcount);
		if (nw == -1) {
			if (errno == EAGAIN)
				continue;
			warn("error writing to sequencer");
			return -1;
		}
		mdl_log(2, "wrote %ld bytes to sequencer\n", nw);
		total_wcount += nw;
	}

	return total_wcount;
}
