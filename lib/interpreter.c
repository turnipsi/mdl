/* $Id: interpreter.c,v 1.28 2015/11/20 21:47:33 je Exp $ */
 
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

#include <sys/select.h>

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
	struct musicexpr_t *abs_me;
	struct midieventstream *eventstream;

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

	if (parsed_expr->me_type != ME_TYPE_SEQUENCE) {
		warnx("expected sequence");
		musicexpr_free(parsed_expr);
		return 1;
	}

	(void) mdl_log(1, "parse ok, got parse result:\n");
	(void) musicexpr_log(0, parsed_expr);

	abs_me = musicexpr_relative_to_absolute(parsed_expr);
	if (abs_me == NULL) {
		warn("could not convert relative musicexpr to absolute");
		musicexpr_free(parsed_expr);
		return 1;
	}

	(void) mdl_log(1, "\n");
	(void) mdl_log(1, "music expression as an absolute expression:\n");
	(void) musicexpr_log(0, abs_me);

	(void) mdl_log(1, "converting to midi stream\n");
	if ((eventstream = musicexpr_to_midievents(abs_me)) == NULL) {
		warnx("error converting music expression to midi stream");
		goto finish;
	}

	(void) mdl_log(1, "writing midi stream to sequencer\n");
	(void) write_midistream_to_sequencer(sequencer_socket, eventstream);

finish:
	musicexpr_free(parsed_expr);
	musicexpr_free(abs_me);

	return 0;
}

static ssize_t
write_midistream_to_sequencer(int sequencer_socket, struct midieventstream *es)
{
	size_t wsize;
	ssize_t nw, total_wcount;

	total_wcount = 0;
	wsize = es->eventcount * sizeof(struct midievent); /* XXX overflow? */

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
