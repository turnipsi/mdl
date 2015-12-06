/* $Id: parse.y,v 1.25 2015/12/06 20:59:20 je Exp $

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

struct musicexpr_t *parsed_expr = NULL;

void	yyerror(const char *fmt, ...);
int	yylex(void);
int	yyparse(void);

static float	countlength(int, int);
static void    *malloc_musicexpr(void);

%}

%union {
	struct sequence_t      *sequence;
	struct joinexpr_t	joinexpr;
	struct musicexpr_t     *musicexpr;
	struct relnote_t	relnote;
	struct rest_t		rest;
	enum notesym_t		notesym;
	float			f;
	int			i;
}

%token	<notesym>	NOTETOKEN_C	NOTETOKEN_D	NOTETOKEN_E
			NOTETOKEN_F	NOTETOKEN_G	NOTETOKEN_A
			NOTETOKEN_B
%token	<i>		NOTETOKEN_ES	NOTETOKEN_IS
			
%token			RESTTOKEN
%token	<i>		LENGTHDOT
%token	<i>		LENGTHNUMBER
%token	<i>		OCTAVEUP
%token	<i>		OCTAVEDOWN

%left			JOINEXPR
%left			WHITESPACE

%type	<musicexpr>	grammar musicexpr musicexpr_sequence
%type	<sequence>	sequence sp_sequence
%type	<joinexpr>	joinexpr
%type	<relnote>	relnote
%type	<rest>		rest
%type	<notesym>	notesym
%type	<i>		notemods octavemods lengthdots
%type	<f>		notelength

%%

grammar:
	musicexpr_sequence { parsed_expr = $1; }

musicexpr_sequence:
	sequence {
		$$ = malloc(sizeof(struct musicexpr_t));
		if ($$ == NULL) {
			musicexpr_free_sequence($1);
			warn("%s", "malloc error");
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_SEQUENCE;
		$$->sequence = $1;
	}
	;

sequence:
	sp_sequence { $$ = $1; }
	| WHITESPACE sp_sequence { $$ = $2; }
	;

sp_sequence:
	musicexpr {
		$$ = malloc(sizeof(struct sequence_t));
		if ($$ == NULL) {
			warn("%s", "malloc error");
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me = $1;
		$$->next = NULL;
	  }
	| musicexpr WHITESPACE sp_sequence {
		$$ = malloc(sizeof(struct sequence_t));
		if ($$ == NULL) {
			musicexpr_free($1);
			musicexpr_free_sequence($3);
			warn("%s", "malloc error");
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me = $1;
		$$->next = $3;
	  }
	| /* empty */ {}
	;

musicexpr:
	joinexpr {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_JOINEXPR;
		$$->joinexpr = $1;

	  }
	| relnote {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_RELNOTE;
		$$->relnote = $1;
	  }
	| rest {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_REST;
		$$->rest = $1;
	}
	;

joinexpr:
	musicexpr WHITESPACE JOINEXPR WHITESPACE musicexpr {
		$$.a = $1;
		$$.b = $5;
	}
	;

relnote:
	notesym notemods octavemods notelength {
		$$.notesym    = $1;
		$$.notemods   = $2;
		$$.octavemods = $3;
		$$.length     = $4;
	  }
	| NOTETOKEN_ES octavemods notelength {
		/* "es" is a special case */
		$$.notesym    = NOTE_E;
		$$.notemods   = - $1;
		$$.octavemods = $2;
		$$.length     = $3;
	}
	;

rest:
	RESTTOKEN notelength {
		$$.length = $2;
	};

notesym:
	NOTETOKEN_C   { $$ = NOTE_C; }
	| NOTETOKEN_D { $$ = NOTE_D; }
	| NOTETOKEN_E { $$ = NOTE_E; }
	| NOTETOKEN_F { $$ = NOTE_F; }
	| NOTETOKEN_G { $$ = NOTE_G; }
	| NOTETOKEN_A { $$ = NOTE_A; }
	| NOTETOKEN_B { $$ = NOTE_B; }
	;

notemods:
	NOTETOKEN_IS   { $$ = + $1; }
	| NOTETOKEN_ES { $$ = - $1; }
	| /* empty */  { $$ = 0;    }
	;

octavemods:
	OCTAVEUP      { $$ = + $1; }
	| OCTAVEDOWN  { $$ = - $1; }
	| /* empty */ { $$ = 0;    }
	;

notelength:
	LENGTHNUMBER lengthdots {
		$$ = countlength($1, $2);
	  }
	| /* empty */ { $$ = 0.0; }
	;

lengthdots:
	LENGTHDOT     { $$ = $1; }
	| /* empty */ { $$ = 0;  }
	;

%%

static float
countlength(int lengthbase, int dotcount)
{
	float length, lengthextender;
	int i;
	
	length = 1.0 / lengthbase;
	lengthextender = length;

	for (i = 0; i < dotcount; i++) {
		lengthextender /= 2;
		length += lengthextender;
	}

	return length;
}

static void *
malloc_musicexpr(void)
{
	void *me;

	if ((me = malloc(sizeof(struct musicexpr_t))) == NULL)
		warn("%s", "malloc error");

	return me;
}

void
yyerror(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vwarnx(fmt, va);
	va_end(va);
}
