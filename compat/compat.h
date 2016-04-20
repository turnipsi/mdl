/* $Id: compat.h,v 1.1 2016/04/20 20:30:44 je Exp $ */

/*
 * Copyright (c) 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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

#ifndef MDL_COMPAT_H
#define MDL_COMPAT_H

#include <sys/types.h>

#ifndef HAVE_PLEDGE
int	pledge(const char *, const char *[]);
#endif /* !HAVE_PLEDGE */

#ifndef HAVE_STRLCPY
size_t	strlcpy(char *, const char *, size_t);
#endif /* !HAVE_STRLCPY */

#ifndef HAVE_STRSEP
char	*strsep(char **, const char *);
#endif /* !HAVE_STRSEP */

#ifndef HAVE_STRTONUM
long long	strtonum(const char *, long long, long long, const char **);
#endif /* !HAVE_STRTONUM */

#endif /* !MDL_COMPAT_H */
