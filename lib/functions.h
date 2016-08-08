/* $Id: functions.h,v 1.3 2016/08/08 08:47:33 je Exp $ */

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

#ifndef MDL_FUNCTIONS_H
#define MDL_FUNCTIONS_H

#include <sys/queue.h>

struct funcarg {
	char		       *arg;
	TAILQ_ENTRY(funcarg)	tq;
};

TAILQ_HEAD(funcarglist, funcarg);

struct function {
	char		       *name;
	struct funcarglist	args;
};

struct musicexpr;

__BEGIN_DECLS
int	_mdl_functions_apply(struct musicexpr *, int);
void	_mdl_functions_free(struct musicexpr *);
__END_DECLS

#endif /* !MDL_FUNCTIONS_H */
