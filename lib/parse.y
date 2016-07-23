/* $Id: parse.y,v 1.60 2016/07/23 20:31:36 je Exp $ */

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
		struct reldrum	expr;
		struct textloc	textloc;
	} reldrum;

	struct {
		enum drumsym	expr;
		struct textloc	textloc;
	} drumsym;

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

%token	<notesym>	NOTETOKEN
			NOTETOKEN_ES

%token	<i>		NOTEMODTOKEN_ES
			NOTEMODTOKEN_IS

%token	<textloc>	RESTTOKEN
%token	<i>		LENGTHDOT
%token	<i>		LENGTHNUMBER
%token	<i>		OCTAVEUP
%token	<i>		OCTAVEDOWN

%token	<string>	QUOTED_STRING

%token	<chordtype>	CHORDTOKEN
%token	<drumsym>	DRUMTOKEN

%token	<textloc>	RELSIMULTENCE_START
			RELSIMULTENCE_END
			SEQUENCE_START
			SEQUENCE_END
			SIMULTENCE_START
			SIMULTENCE_END

%right	<textloc>	TRACK_OPERATOR
%left	<textloc>	JOINEXPR

%type	<musicexpr>	grammar musicexpr relsimultence_expr
			sequence_expr simultence_expr track_expr
%type	<melist>	expression_list
%type	<joinexpr>	joinexpr
%type	<relnote>	relnote
%type	<chord>		chord
%type	<reldrum>	reldrum
%type	<rest>		rest
%type	<i>		notemods octavemods lengthdots
%type	<f>		notelength

%%

grammar:
	sequence_expr { parsed_expr = $1; }
	| /* empty */ {
		parsed_expr = _mdl_musicexpr_new(ME_TYPE_EMPTY,
		    _mdl_textloc_zero(), 0);
		if (parsed_expr == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
	}
	;

musicexpr:
	chord {
		$$ = _mdl_musicexpr_new(ME_TYPE_CHORD, $1.textloc, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.chord = $1.expr;
	  }
	| joinexpr {
		$$ = _mdl_musicexpr_new(ME_TYPE_JOINEXPR, $1.textloc, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.joinexpr = $1.expr;

	  }
	| reldrum {
		$$ = _mdl_musicexpr_new(ME_TYPE_RELDRUM, $1.textloc, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.reldrum = $1.expr;
	  }
	| relnote {
		$$ = _mdl_musicexpr_new(ME_TYPE_RELNOTE, $1.textloc, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.relnote = $1.expr;
	  }
	| relsimultence_expr { $$ = $1; }
	| rest {
		$$ = _mdl_musicexpr_new(ME_TYPE_REST, $1.textloc, 0);
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
	relnote CHORDTOKEN {
		$$.expr.chordtype = $2.expr;
		$$.expr.me = _mdl_musicexpr_new(ME_TYPE_RELNOTE, $1.textloc,
		    0);
		if ($$.expr.me == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$.expr.me->u.relnote = $1.expr;
		$$.textloc = _mdl_join_textlocs($1.textloc, $2.textloc);
	}
	;

reldrum:
	DRUMTOKEN notelength {
		$$.expr.drumsym = $1.expr;
		$$.expr.length  = $2.expr;

		$$.textloc = _mdl_join_textlocs($1.textloc, $2.textloc);
	};

joinexpr:
	musicexpr JOINEXPR musicexpr {
		$$.expr.a = $1;
		$$.expr.b = $3;
		$$.textloc = _mdl_join_textlocs($1->id.textloc,
		    $3->id.textloc);
	}
	;

relnote:
	NOTETOKEN notemods octavemods notelength {
		struct textloc tl;

		$$.expr.notesym    = $1.expr;
		$$.expr.notemods   = $2.expr;
		$$.expr.octavemods = $3.expr;
		$$.expr.length     = $4.expr;

		tl = $1.textloc;
		tl = _mdl_join_textlocs(tl, $2.textloc);
		tl = _mdl_join_textlocs(tl, $3.textloc);
		tl = _mdl_join_textlocs(tl, $4.textloc);

		$$.textloc = tl;
	  }
	| NOTETOKEN_ES notemods octavemods notelength {
		struct textloc tl;

		/* "es" is a special case */
		$$.expr.notesym    = $1.expr;
		$$.expr.notemods   = $2.expr - 1;
		$$.expr.octavemods = $3.expr;
		$$.expr.length     = $4.expr;

		tl = $1.textloc;
		tl = _mdl_join_textlocs(tl, $2.textloc);
		tl = _mdl_join_textlocs(tl, $3.textloc);
		tl = _mdl_join_textlocs(tl, $4.textloc);

		$$.textloc = tl;
	}
	;

relsimultence_expr:
	RELSIMULTENCE_START simultence_expr RELSIMULTENCE_END notelength {
		struct textloc tl;

		tl = $1.textloc;
		tl = _mdl_join_textlocs(tl, $2->id.textloc);
		tl = _mdl_join_textlocs(tl, $3.textloc);
		tl = _mdl_join_textlocs(tl, $4.textloc);

		$$ = _mdl_musicexpr_new(ME_TYPE_RELSIMULTENCE, tl, 0);
		if ($$ == NULL) {
			_mdl_musicexpr_free($2, 0);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.scaledexpr.me = $2;
		$$->u.scaledexpr.length = $4.expr;
	  }
	;

rest:
	RESTTOKEN notelength {
		$$.expr.length = $2.expr;
		$$.textloc = _mdl_join_textlocs($1.textloc, $2.textloc);
	};

sequence_expr:
	expression_list {
		$$ = _mdl_musicexpr_new(ME_TYPE_SEQUENCE, $1.textloc, 0);
		if ($$ == NULL) {
			_mdl_musicexpr_free_melist($1.expr, 0);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.melist = $1.expr;
	}
	;

simultence_expr:
	expression_list {
		$$ = _mdl_musicexpr_new(ME_TYPE_SIMULTENCE, $1.textloc, 0);
		if ($$ == NULL) {
			_mdl_musicexpr_free_melist($1.expr, 0);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.melist = $1.expr;
	  }
	;

track_expr:
	QUOTED_STRING TRACK_OPERATOR musicexpr {
		struct textloc tl;

		tl = $1.textloc;
		tl = _mdl_join_textlocs(tl, $2.textloc);
		tl = _mdl_join_textlocs(tl, $3->id.textloc);

		$$ = _mdl_musicexpr_new(ME_TYPE_ONTRACK, tl, 0);
		if ($$ == NULL) {
			free($1.expr);
			_mdl_musicexpr_free($3, 0);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.ontrack.me = $3;
		$$->u.ontrack.track = malloc(sizeof(struct track));
		if ($$->u.ontrack.track == NULL) {
			free($$);
			free($1.expr);
			_mdl_musicexpr_free($3, 0);
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
		$$.textloc = _mdl_join_textlocs($1.textloc, $2->id.textloc);
		TAILQ_INSERT_TAIL(&$$.expr, $2, tq);
	  }
	;

notemods:
	NOTEMODTOKEN_IS   { $$.expr = + $1.expr; $$.textloc = $1.textloc; }
	| NOTEMODTOKEN_ES { $$.expr = - $1.expr; $$.textloc = $1.textloc; }
	| /* empty */     { $$.expr = 0; $$.textloc = _mdl_textloc_zero(); }
	;

octavemods:
	OCTAVEUP      { $$.expr = + $1.expr; $$.textloc = $1.textloc; }
	| OCTAVEDOWN  { $$.expr = - $1.expr; $$.textloc = $1.textloc; }
	| /* empty */ { $$.expr = 0; $$.textloc = _mdl_textloc_zero(); }
	;

notelength:
	LENGTHNUMBER lengthdots {
		$$.expr = countlength($1.expr, $2.expr);
		$$.textloc = _mdl_join_textlocs($1.textloc, $2.textloc);
	  }
	| /* empty */ { $$.expr = 0.0; $$.textloc = _mdl_textloc_zero(); }
	;

lengthdots:
	LENGTHDOT     { $$.expr = $1.expr; $$.textloc = $1.textloc; }
	| /* empty */ { $$.expr = 0;       $$.textloc = _mdl_textloc_zero(); }
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
		parse_errors += 1;

	va_start(va, fmt);
	vwarnx(fmt, va);
	va_end(va);
}
