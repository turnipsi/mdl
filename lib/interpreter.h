/* $Id: interpreter.h,v 1.13 2016/07/14 20:41:43 je Exp $ */

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

#ifndef MDL_MUSICINTERP_H
#define MDL_MUSICINTERP_H

struct interpreter_process {
	int	sequencer_read_pipe;
	pid_t	pid;
};

struct interpreter_handler {
	struct client_connection       *client_conn;
	struct interpreter_process	process;
	int				is_active;
	int				next_musicfile_fd;
};

__BEGIN_DECLS
int	_mdl_interpreter_do_musicfile(int, int);
int	_mdl_interpreter_start_process(struct interpreter_process *, int, int);
int	yyparse(void);
__END_DECLS

#endif /* !MDL_MUSICINTERP_H */
