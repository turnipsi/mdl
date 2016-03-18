/* $Id: parse.y,v 1.51 2016/03/18 21:21:28 je Exp $ */

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

%{
#include <err.h>
#include <limits.h>
#include <stdarg.h>

#include "musicexpr.h"
#include "parse.h"

struct musicexpr *parsed_expr = NULL;
unsigned int	parse_errors = 0;

extern struct textloc mdl_lexer_textloc;

static float countlength(int, int);

%}

/* XXX these all(?) should pass textual location information */
%union {
	struct {
		struct chord	expr;
		struct textloc	textloc;
	} chord;

	struct {
		struct joinexpr	expr;
		struct textloc	textloc;
	} joinexpr;

	struct {
		struct melist	expr;
		struct textloc	textloc;
	} melist;

	struct musicexpr       *musicexpr;

	struct {
		struct ontrack	expr;
		struct textloc	textloc;
	} ontrack;

	struct {
		struct relnote	expr;
		struct textloc	textloc;
	} relnote;

	struct {
		struct rest	expr;
		struct textloc	textloc;
	} rest;

	struct {
		enum chordtype	expr;
		struct textloc	textloc;
	} chordtype;

	struct {
		enum notesym	expr;
		struct textloc	textloc;
	} notesym;

	struct {
		char	       *expr;
		struct textloc	textloc;
	} string;

	struct {
		float		expr;
		struct textloc	textloc;
	} f;

	struct {
		int		expr;
		struct textloc	textloc;
	} i;

	struct {
		struct textloc	textloc;
	} textloc;
}

%token	<notesym>	NOTETOKEN_C
			NOTETOKEN_D
			NOTETOKEN_ES
			NOTETOKEN_E
			NOTETOKEN_F
			NOTETOKEN_G
			NOTETOKEN_A
			NOTETOKEN_B

%token	<i>		NOTEMOD_ES
			NOTEMOD_IS

%token			RESTTOKEN
%token	<i>		LENGTHDOT
%token	<i>		LENGTHNUMBER
%token	<i>		OCTAVEUP
%token	<i>		OCTAVEDOWN

%token	<string>	QUOTED_STRING

%token	<chordtype>	CHORDTOKEN_NONE
			CHORDTOKEN_MAJ
			CHORDTOKEN_MIN
			CHORDTOKEN_AUG
			CHORDTOKEN_DIM
			CHORDTOKEN_7
			CHORDTOKEN_MAJ7
			CHORDTOKEN_MIN7
			CHORDTOKEN_DIM7
			CHORDTOKEN_AUG7
			CHORDTOKEN_DIM5MIN7
			CHORDTOKEN_MIN5MAJ7
			CHORDTOKEN_MAJ6
			CHORDTOKEN_MIN6
			CHORDTOKEN_9
			CHORDTOKEN_MAJ9
			CHORDTOKEN_MIN9
			CHORDTOKEN_11
			CHORDTOKEN_MAJ11
			CHORDTOKEN_MIN11
			CHORDTOKEN_13
			CHORDTOKEN_13_11
			CHORDTOKEN_MAJ13_11
			CHORDTOKEN_MIN13_11
			CHORDTOKEN_SUS2
			CHORDTOKEN_SUS4
			CHORDTOKEN_5
			CHORDTOKEN_5_8

%token			RELSIMULTENCE_START
			RELSIMULTENCE_END
			SEQUENCE_START
			SEQUENCE_END
			SIMULTENCE_START
			SIMULTENCE_END

%right			TRACK_OPERATOR
%left			JOINEXPR

%type	<musicexpr>	grammar musicexpr relsimultence_expr
			sequence_expr simultence_expr track_expr
%type	<melist>	expression_list
%type	<joinexpr>	joinexpr
%type	<relnote>	relnote
%type	<chord>		chord
%type	<chordtype>	chordtype
%type	<rest>		rest
%type	<notesym>	notesym
%type	<i>		notemods octavemods lengthdots
%type	<f>		notelength
%type	<textloc>	relsimultence_start relsimultence_end resttoken

%%

grammar:
	sequence_expr { parsed_expr = $1; }
	| /* empty */ {
		parsed_expr = musicexpr_new(ME_TYPE_EMPTY, textloc_zero());
		if (parsed_expr == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
	}
	;

musicexpr:
	chord {
		$$ = musicexpr_new(ME_TYPE_CHORD, $1.textloc);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.chord = $1.expr;
	  }
	| joinexpr {
		$$ = musicexpr_new(ME_TYPE_JOINEXPR, $1.textloc);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.joinexpr = $1.expr;

	  }
	| relnote {
		$$ = musicexpr_new(ME_TYPE_RELNOTE, $1.textloc);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.relnote = $1.expr;
	  }
	| relsimultence_expr { $$ = $1; }
	| rest {
		$$ = musicexpr_new(ME_TYPE_REST, $1.textloc);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.rest = $1.expr;
	  }
	| SEQUENCE_START sequence_expr SEQUENCE_END { $$ = $2; }
	| SIMULTENCE_START simultence_expr SIMULTENCE_END { $$ = $2; }
	| track_expr { $$ = $1; }
	;

chord:
	relnote chordtype {
		$$.expr.chordtype = $2.expr;
		$$.expr.me = musicexpr_new(ME_TYPE_RELNOTE,
		    join_textlocs($1.textloc, $2.textloc));
		if ($$.expr.me == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$.expr.me->u.relnote = $1.expr;
	}
	;

joinexpr:
	musicexpr JOINEXPR musicexpr {
		$$.expr.a = $1;
		$$.expr.b = $3;
		$$.textloc = join_textlocs($1->id.textloc, $3->id.textloc);
	}
	;

relnote:
	notesym notemods octavemods notelength {
		$$.expr.notesym    = $1.expr;
		$$.expr.notemods   = $2.expr;
		$$.expr.octavemods = $3.expr;
		$$.expr.length     = $4.expr;

		$$.textloc = join_textlocs($1.textloc, $4.textloc);
	  }
	| NOTETOKEN_ES notemods octavemods notelength {
		/* "es" is a special case */
		$$.expr.notesym    = NOTE_E;
		$$.expr.notemods   = - $1.expr;
		$$.expr.octavemods = $2.expr;
		$$.expr.length     = $3.expr;

		$$.textloc = join_textlocs($1.textloc, $4.textloc);
	}
	;

relsimultence_expr:
	relsimultence_start simultence_expr relsimultence_end notelength {
		$$ = musicexpr_new(ME_TYPE_RELSIMULTENCE,
		    join_textlocs($1.textloc, $4.textloc));
		if ($$ == NULL) {
			musicexpr_free($2);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.scaledexpr.me = $2;
		$$->u.scaledexpr.length = $4.expr;
	  }
	;

relsimultence_start: RELSIMULTENCE_START { $$.textloc = mdl_lexer_textloc; };
relsimultence_end:   RELSIMULTENCE_END   { $$.textloc = mdl_lexer_textloc; };
resttoken:           RESTTOKEN           { $$.textloc = mdl_lexer_textloc; };

rest:
	resttoken notelength {
		$$.expr.length = $2.expr;
		$$.textloc = join_textlocs($1.textloc, $2.textloc);
	};

sequence_expr:
	expression_list {
		$$ = musicexpr_new(ME_TYPE_SEQUENCE, $1.textloc);
		if ($$ == NULL) {
			musicexpr_free_melist($1.expr);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.melist = $1.expr;
	}
	;

simultence_expr:
	expression_list {
		$$ = musicexpr_new(ME_TYPE_SIMULTENCE, $1.textloc);
		if ($$ == NULL) {
			musicexpr_free_melist($1.expr);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.melist = $1.expr;
	  }
	;

track_expr:
	QUOTED_STRING TRACK_OPERATOR musicexpr {
		$$ = musicexpr_new(ME_TYPE_ONTRACK,
		    join_textlocs($1.textloc, $3->id.textloc));
		if ($$ == NULL) {
			free($1.expr);
			musicexpr_free($3);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.ontrack.me = $3;
		$$->u.ontrack.track = malloc(sizeof(struct track));
		if ($$->u.ontrack.track == NULL) {
			free($$);
			free($1.expr);
			musicexpr_free($3);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.ontrack.track->name = $1.expr;
	  }
	;

expression_list:
	musicexpr {
		TAILQ_INIT(&$$.expr);
		TAILQ_INSERT_TAIL(&$$.expr, $1, tq);
		$$.textloc = $1->id.textloc;
	  }
	| expression_list musicexpr {
		$$.expr = $1.expr;
		$$.textloc = join_textlocs($1.textloc, $2->id.textloc);
		TAILQ_INSERT_TAIL(&$$.expr, $2, tq);
	  }
	;

notesym:
	NOTETOKEN_C   { $$.expr = NOTE_C; $$.textloc = mdl_lexer_textloc; }
	| NOTETOKEN_D { $$.expr = NOTE_D; $$.textloc = mdl_lexer_textloc; }
	| NOTETOKEN_E { $$.expr = NOTE_E; $$.textloc = mdl_lexer_textloc; }
	| NOTETOKEN_F { $$.expr = NOTE_F; $$.textloc = mdl_lexer_textloc; }
	| NOTETOKEN_G { $$.expr = NOTE_G; $$.textloc = mdl_lexer_textloc; }
	| NOTETOKEN_A { $$.expr = NOTE_A; $$.textloc = mdl_lexer_textloc; }
	| NOTETOKEN_B { $$.expr = NOTE_B; $$.textloc = mdl_lexer_textloc; }

notemods:
	NOTEMOD_IS    { $$.expr = + $1.expr; $$.textloc = mdl_lexer_textloc; }
	| NOTEMOD_ES  { $$.expr = - $1.expr; $$.textloc = mdl_lexer_textloc; }
	| /* empty */ { $$.expr = 0;         $$.textloc = mdl_lexer_textloc; }
	;

octavemods:
	OCTAVEUP      { $$.expr = + $1.expr; $$.textloc = mdl_lexer_textloc; }
	| OCTAVEDOWN  { $$.expr = - $1.expr; $$.textloc = mdl_lexer_textloc; }
	| /* empty */ { $$.expr = 0;         $$.textloc = mdl_lexer_textloc; }
	;

notelength:
	LENGTHNUMBER lengthdots {
		$$.expr = countlength($1.expr, $2.expr);
		$$.textloc = join_textlocs($1.textloc, $2.textloc);
	  }
	| /* empty */ { $$.expr = 0.0; $$.textloc = mdl_lexer_textloc; }
	;

lengthdots:
	LENGTHDOT     { $$.expr = $1.expr; $$.textloc = $1.textloc; }
	| /* empty */ { $$.expr = 0;       $$.textloc = mdl_lexer_textloc; }
	;

chordtype:
	CHORDTOKEN_NONE	{
		$$.expr = CHORDTYPE_NONE;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MAJ {
		$$.expr = CHORDTYPE_MAJ;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN {
		$$.expr = CHORDTYPE_MIN;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_AUG {
		$$.expr = CHORDTYPE_AUG;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_DIM {
		$$.expr = CHORDTYPE_DIM;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_7 {
		$$.expr = CHORDTYPE_7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MAJ7 {
		$$.expr = CHORDTYPE_MAJ7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN7 {
		$$.expr = CHORDTYPE_MIN7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_DIM7 {
		$$.expr = CHORDTYPE_DIM7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_AUG7 {
		$$.expr = CHORDTYPE_AUG7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_DIM5MIN7 {
		$$.expr = CHORDTYPE_DIM5MIN7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN5MAJ7 {
		$$.expr = CHORDTYPE_MIN5MAJ7;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MAJ6 {
		$$.expr = CHORDTYPE_MAJ6;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN6 {
		$$.expr = CHORDTYPE_MIN6;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_9 {
		$$.expr = CHORDTYPE_9;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MAJ9 {
		$$.expr = CHORDTYPE_MAJ9;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN9 {
		$$.expr = CHORDTYPE_MIN9;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_11 {
		$$.expr = CHORDTYPE_11;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MAJ11 {
		$$.expr = CHORDTYPE_MAJ11;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN11 {
		$$.expr = CHORDTYPE_MIN11;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_13 {
		$$.expr = CHORDTYPE_13;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_13_11 {
		$$.expr = CHORDTYPE_13_11;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MAJ13_11 {
		$$.expr = CHORDTYPE_MAJ13_11;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_MIN13_11 {
		$$.expr = CHORDTYPE_MIN13_11;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_SUS2 {
		$$.expr = CHORDTYPE_SUS2;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_SUS4 {
		$$.expr = CHORDTYPE_SUS4;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_5 {
		$$.expr = CHORDTYPE_5;
		$$.textloc = mdl_lexer_textloc;
	}
	| CHORDTOKEN_5_8 {
		$$.expr = CHORDTYPE_5_8;
		$$.textloc = mdl_lexer_textloc;
	}
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

void
yyerror(const char *fmt, ...)
{
	va_list va;

	if (parse_errors < UINT_MAX)
		parse_errors++;

	va_start(va, fmt);
	vwarnx(fmt, va);
	va_end(va);
}
