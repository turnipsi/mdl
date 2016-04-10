/* $Id: compat.h.m4,v 1.1 2016/04/10 19:48:28 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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

/*
 * This file is mostly a collection of definitions/subroutines copied from
 * OpenBSD sources.  This should function as a compatibility layer for other
 * systems.  Some snippets may contain its own copyright information relevant
 * only to that snippet.
 */

#ifndef MDL_COMPAT_H
#define MDL_COMPAT_H

#include <sys/types.h>

#include "config.h"

__BEGIN_DECLS

#if !HAVE_PLEDGE
int	pledge(const char *, const char *[]);
#endif /* !HAVE_PLEDGE */

#if !HAVE_STRLCPY
size_t	strlcpy(char *, const char *, size_t);
#endif /* !HAVE_STRLCPY */

__END_DECLS

#endif /* !MDL_COMPAT_H */
