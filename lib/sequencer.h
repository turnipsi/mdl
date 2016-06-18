/* $Id: sequencer.h,v 1.24 2016/06/18 20:25:27 je Exp $ */

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

#ifndef MDL_SEQUENCER_H
#define MDL_SEQUENCER_H

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <imsg.h>

#include "midi.h"

struct sequencer_connection {
	int		socket;
	struct imsgbuf	ibuf;
};

struct sequencer_process {
	pid_t				pid;
	struct sequencer_connection	conn;
};

__BEGIN_DECLS
int	_mdl_disconnect_sequencer_connection(struct sequencer_connection *);
int	_mdl_disconnect_sequencer_process(struct sequencer_process *);
int	_mdl_start_sequencer_process(struct sequencer_process *,
    enum mididev_type, const char *, int);
__END_DECLS

#endif /* !MDL_SEQUENCER_H */
