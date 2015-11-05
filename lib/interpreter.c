/* $Id: interpreter.c,v 1.15 2015/11/05 20:24:51 je Exp $ */
 
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "midi.h"
#include "musicexpr.h"
#include "util.h"

extern FILE	       *yyin;
extern enum notesym	parsetree;

static int	testwrite(int);
int		yyparse(void);

int
handle_musicfile_and_socket(int file_fd,
			    int main_socket,
			    int sequencer_socket,
			    int server_socket)
{
        if (mdl_sandbox("stdio") == -1) {
		warnx("sandbox error");
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

	(void) printf("parse ok, got parse result: %d\n", parsetree);

	testwrite(sequencer_socket);

	return 0;
}

static int
testwrite(int sequencer_socket)
{
	struct midievent events[9];
	ssize_t nw;
	char channel;

	channel = 0;

	bzero(events, sizeof(events));

	events[0].eventtype        = NOTEON;
	events[0].channel          = channel;
	events[0].note             = 60;
	events[0].velocity         = 127;
	events[0].time_as_measures = 0.0;

	events[1].eventtype        = NOTEOFF;
	events[1].channel          = channel;
	events[1].note             = 60;
	events[1].velocity         = 0;
	events[1].time_as_measures = 0.25;

	events[2].eventtype        = NOTEON;
	events[2].channel          = channel;
	events[2].note             = 60;
	events[2].velocity         = 127;
	events[2].time_as_measures = 0.25;

	events[3].eventtype        = NOTEOFF;
	events[3].channel          = channel;
	events[3].note             = 60;
	events[3].velocity         = 0;
	events[3].time_as_measures = 0.5;

	events[4].eventtype        = NOTEON;
	events[4].channel          = channel;
	events[4].note             = 60;
	events[4].velocity         = 127;
	events[4].time_as_measures = 0.5;

	events[5].eventtype        = NOTEOFF;
	events[5].channel          = channel;
	events[5].note             = 60;
	events[5].velocity         = 0;
	events[5].time_as_measures = 0.75;

	events[6].eventtype        = NOTEON;
	events[6].channel          = channel;
	events[6].note             = 64;
	events[6].velocity         = 127;
	events[6].time_as_measures = 0.75;

	events[7].eventtype        = NOTEOFF;
	events[7].channel          = channel;
	events[7].note             = 64;
	events[7].velocity         = 0;
	events[7].time_as_measures = 1.0;

	events[8].eventtype        = SONG_END;
	events[8].channel          = 0;
	events[8].note             = 0;
	events[8].velocity         = 0;
	events[8].time_as_measures = 0.0;

	/* XXX */
	nw = write(sequencer_socket, events, sizeof(events));
	if (nw == -1) {
		warn("error writing to sequencer");
		return 1;
	}

	return 0;
}
