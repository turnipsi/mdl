/* $Id: interpreter.c,v 1.2 2015/10/09 19:48:13 je Exp $
 
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

int
handle_musicfile_and_socket(int file_fd,
			    int main_socket,
			    int sequencer_socket,
			    int server_socket)
{
#if 0
	fd_set readfds;
	int ret;
	pid_t musicinterp_pid;

	FD_ZERO(&readfds);
	FD_SET(file_fd, &readfds);

	if (server_socket >= 0)
		FD_SET(server_socket, &readfds);

	while ((ret = select(FD_SETSIZE, &readfds, NULL, NULL, NULL)) > 0) {
		if (mdl_shutdown)
			return 1;

		if (FD_ISSET(file_fd, &readfds)) {
			/* XXX */
		}

		if (server_socket >= 0 && FD_ISSET(server_socket, &readfds)) {
			/* XXX */
		}
	}
	if (ret == -1) {
		warn("error in select");
		return 1;
	}
#endif

	return 0;
}
