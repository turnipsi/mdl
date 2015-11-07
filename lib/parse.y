/* $Id: parse.y,v 1.10 2015/11/07 20:24:59 je Exp $

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

%{
#include <err.h>
#include <stdarg.h>

#include "musicexpr.h"

struct musicexpr_t *parsetree;

void	yyerror(const char *fmt, ...);
int	yylex(void);
int	yyparse(void);
%}

%union {
	enum notesym_t		notesym;
	struct relnote_t	relnote;
	struct musicexpr_t     *musicexpr;
}

%token	<note>	NOTETOKEN_C   NOTETOKEN_CIS NOTETOKEN_DES NOTETOKEN_D
		NOTETOKEN_DIS NOTETOKEN_ES  NOTETOKEN_E   NOTETOKEN_F
		NOTETOKEN_FIS NOTETOKEN_GES NOTETOKEN_G   NOTETOKEN_GIS
		NOTETOKEN_AES NOTETOKEN_A   NOTETOKEN_AIS NOTETOKEN_BES
		NOTETOKEN_B
%token		WHITESPACE

%type	<notesym>	notesym
%type	<relnote>	relnote
%type	<musicexpr>	grammar musicexpr smusicexpr

%%

grammar:	musicexpr { parsetree = $1; }

musicexpr:	smusicexpr { $$ = $1; }
		| WHITESPACE smusicexpr { $$ = $2; }
		;

smusicexpr:	relnote {
			$$ = malloc(sizeof(struct musicexpr_t));
			if ($$ == NULL) {
				warn("%s", "malloc error");
				YYERROR;
			}
			$$->relnote = $1;
			$$->next = NULL;
		}
		| relnote WHITESPACE smusicexpr {
			$$ = malloc(sizeof(struct musicexpr_t));
			if ($$ == NULL) {
				warn("%s", "malloc error");
				free_musicexpr($3);
				YYERROR;
			}
			$$->relnote = $1;
			$$->next = $3;
		  }
		| {} /* empty */
		;



relnote:	notesym {
			$$.notesym    = $1;
			$$.dotcount   = 0;
			$$.lengthbase = 0;
			$$.updown_mod = 0;
		}
		;

notesym:	NOTETOKEN_C	{ $$ = NOTE_C;   }
		| NOTETOKEN_CIS { $$ = NOTE_CIS; }
		| NOTETOKEN_DES { $$ = NOTE_DES; }
		| NOTETOKEN_D   { $$ = NOTE_D;   }
		| NOTETOKEN_DIS { $$ = NOTE_DIS; }
		| NOTETOKEN_ES  { $$ = NOTE_ES;  }
		| NOTETOKEN_E   { $$ = NOTE_E;   }
		| NOTETOKEN_F   { $$ = NOTE_F;   }
		| NOTETOKEN_FIS { $$ = NOTE_FIS; }
		| NOTETOKEN_GES { $$ = NOTE_GES; }
		| NOTETOKEN_G   { $$ = NOTE_G;   }
		| NOTETOKEN_GIS { $$ = NOTE_GIS; }
		| NOTETOKEN_AES { $$ = NOTE_AES; }
		| NOTETOKEN_A   { $$ = NOTE_A;   }
		| NOTETOKEN_AIS { $$ = NOTE_AIS; }
		| NOTETOKEN_BES { $$ = NOTE_AES; }
		| NOTETOKEN_B   { $$ = NOTE_B;   }
		;

%%

void
yyerror(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vwarnx(fmt, va);
	va_end(va);
}
