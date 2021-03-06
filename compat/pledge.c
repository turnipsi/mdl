/*	$Id: pledge.c,v 1.3 2016/05/03 09:33:06 je Exp $	*/

/*
 * Copyright (c) 2016 Juha Erkkil� <je@turnipsi.no-ip.org>
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

#ifndef HAVE_PLEDGE

#define UNUSED(x) (void)(x)

int
pledge(const char *promises, const char *paths[])
{
	/* Not much we can do, in case the OS does not support pledge(). */

	/* Suppress "unused parameter" compiler warnings. */
	UNUSED(paths);
	UNUSED(promises);

	return 0;
}

#endif /* !HAVE_PLEDGE */
