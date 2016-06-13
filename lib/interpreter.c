/* $Id: interpreter.c,v 1.58 2016/06/13 20:55:31 je Exp $ */

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
extern const char	*_mdl_process_type;

int
_mdl_start_interpreter_process(struct interpreter_process *interp,
    int mdlfile_fd, int sequencer_socket)
{
	int is_pipe[2];	/* interpreter-sequencer pipe */
	int ret;
	pid_t interpreter_pid;

	/* Setup pipe for interpreter --> sequencer communication. */
	if (pipe(is_pipe) == -1) {
		warn("could not setup pipe for interpreter -> sequencer");
		return 1;
	}

	if (fflush(NULL) == EOF)
		warn("error flushing streams before interpreter fork");

	if ((interpreter_pid = fork()) == -1) {
		warn("could not fork interpreter pid");
		if (close(is_pipe[1]) == -1)
			warn("error closing write end of is_pipe");
		if (close(is_pipe[0]) == -1)
			warn("error closing read end of is_pipe");
		return 1;
	}

	if (interpreter_pid == 0) {
		/*
		 * We are in the interpreter process.
		 */

		if (pledge("stdio", NULL) == -1) {
			warn("pledge");
			_exit(1);
		}

		_mdl_logging_clear();
		_mdl_process_type = "interp";
		_mdl_log(MDLLOG_PROCESS, 0,
		    "new interpreter process, pid %d\n", getpid());

		/*
		 * Be strict here when closing file descriptors so that we
		 * do not leak file descriptors to interpreter process.
		 */
		if (close(sequencer_socket) == -1) {
			warn("error closing sequencer socket");
			ret = 1;
			goto interpreter_out;
		}
		if (close(is_pipe[0]) == -1) {
			warn("error closing read end of is_pipe");
			ret = 1;
			goto interpreter_out;
		}

		ret = _mdl_interpret_musicfile(mdlfile_fd, is_pipe[1]);

		if (mdlfile_fd != fileno(stdin) && close(mdlfile_fd) == -1)
			warn("error closing music file");

		if (close(is_pipe[1]) == -1)
			warn("error closing write end of is_pipe");

interpreter_out:
		if (fflush(NULL) == EOF) {
			warn("error flushing streams in interpreter"
			    " before exit");
		}

		_mdl_logging_close();

		_exit(ret);
	}

	if (close(is_pipe[1]) == -1)
		warn("error closing write end of is_pipe");

	interp->pid = interpreter_pid;
	interp->sequencer_read_pipe = is_pipe[0];

	return 0;
}

int
_mdl_interpret_musicfile(int mdlfile_fd, int sequencer_read_pipe)
{
	struct mdl_stream *eventstream;
	ssize_t wcount;
	int level, ret;

	assert(mdlfile_fd >= 0);
	assert(sequencer_read_pipe >= 0);

	eventstream = NULL;
	level = 0;
	ret = 0;

	if ((yyin = fdopen(mdlfile_fd, "r")) == NULL) {
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

	wcount = _mdl_midi_write_midistream(sequencer_read_pipe, eventstream,
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
