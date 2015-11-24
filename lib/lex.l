%{
/* $Id: lex.l,v 1.10 2015/11/24 20:23:04 je Exp $ */

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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "musicexpr.h"
#include "y.tab.h"   

int	yywrap(void);
%}

%%
(es)+	{ yylval.i = strlen(yytext) / 2; return NOTETOKEN_ES; }
(is)+	{ yylval.i = strlen(yytext) / 2; return NOTETOKEN_IS; }

c	{ return NOTETOKEN_C; }
d	{ return NOTETOKEN_D; }
e	{ return NOTETOKEN_E; }
f	{ return NOTETOKEN_F; }
g	{ return NOTETOKEN_G; }
a	{ return NOTETOKEN_A; }
b	{ return NOTETOKEN_B; }

\.+		{ yylval.i = strlen(yytext); return LENGTHDOT; }
[1-9][0-9]*	{
			/* XXX error handling */
			yylval.i = strtonum(yytext, 1, INT_MAX, NULL);
			return LENGTHNUMBER;
		}
'+		{ yylval.i = strlen(yytext); return OCTAVEUP; }
,+		{ yylval.i = strlen(yytext); return OCTAVEDOWN; }

~ 		{ return JOINEXPR; }

[ \t\n]+	{ return WHITESPACE; }
%%

/* XXX I do not know why I need this */
int
yywrap(void)
{
	return 1;
}
