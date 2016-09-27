/* $Id: parse.y,v 1.74 2016/09/27 09:22:18 je Exp $ */

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

#include "functions.h"
#include "musicexpr.h"
#include "parse.h"
#include "track.h"

struct musicexpr *parsed_expr = NULL;
unsigned int	parse_errors = 0;

static float		 countlength(int, int);
static struct musicexpr *maybe_apply_scaling(struct musicexpr *, float,
    struct textloc);

%}

/* XXX these all(?) should pass textual location information */
%union {
	struct {
		struct chord	expr;
		struct textloc	textloc;
	} chord;

	struct {
		enum chordtype	expr;
		struct textloc	textloc;
	} chordtype;

	struct {
		enum drumsym	expr;
		struct textloc	textloc;
	} drumsym;

	struct {
		float		expr;
		struct textloc	textloc;
	} f;

	struct funcarg	       *funcarg;
	struct funcarglist	funcarglist;
	struct function		function;

	struct {
		int		expr;
		struct textloc	textloc;
	} i;

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
		enum notesym	expr;
		struct textloc	textloc;
	} notesym;

	struct {
		struct ontrack	expr;
		struct textloc	textloc;
	} ontrack;

	struct {
		struct reldrum	expr;
		struct textloc	textloc;
	} reldrum;

	struct {
		struct relnote	expr;
		struct textloc	textloc;
	} relnote;

	struct {
		struct rest	expr;
		struct textloc	textloc;
	} rest;

	struct {
		char	       *expr;
		struct textloc	textloc;
	} string;

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
			LENGTHNUMBER
			OCTAVEUP
			OCTAVEDOWN

%token	<string>	FUNCARG_TOKEN
			FUNCNAME_TOKEN
			QUOTED_STRING

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

%type	<chord>		chord
%type	<f>		notelength
%type	<funcarg>	funcarg
%type	<funcarglist>	funcarglist
%type	<function>	function
%type	<i>		notemods octavemods lengthdots
%type	<joinexpr>	joinexpr
%type	<musicexpr>	grammar
			musicexpr
			relsimultence_expr_with_enclosers_and_length
			sequence_expr
			sequence_expr_with_enclosers
			sequence_expr_with_enclosers_and_length
			simultence_expr
			simultence_expr_with_enclosers
			simultence_expr_with_enclosers_and_length
			track_expr
%type	<melist>	expression_list
%type	<relnote>	relnote
%type	<reldrum>	reldrum
%type	<rest>		rest

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
	| function {
		$$ = _mdl_musicexpr_new(ME_TYPE_FUNCTION, $1.textloc, 0);
		if ($$ == NULL) {
			/* XXX stuff in function should be freed */

			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.function = $1;
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
	| relsimultence_expr_with_enclosers_and_length { $$ = $1; }
	| rest {
		$$ = _mdl_musicexpr_new(ME_TYPE_REST, $1.textloc, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.rest = $1.expr;
	  }
	| sequence_expr_with_enclosers_and_length   { $$ = $1; }
	| simultence_expr_with_enclosers_and_length { $$ = $1; }
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
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	  }
	;

function:
	FUNCNAME_TOKEN funcarglist {
		$$.name = $1.expr;
		$$.args = $2;
		$$.textloc = $1.textloc;
	  }
	;

funcarg:
	FUNCARG_TOKEN {
		$$ = malloc(sizeof(struct funcarg));
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->arg = $1.expr;
		$$->textloc  = $1.textloc;
	  }
	;

funcarglist:
	funcarg {
		TAILQ_INIT(&$$);
		TAILQ_INSERT_TAIL(&$$, $1, tq);
	  }
	| funcarglist funcarg {
		$$ = $1;
		TAILQ_INSERT_TAIL(&$$, $2, tq);
	  }
	;

reldrum:
	DRUMTOKEN notelength {
		$$.expr.drumsym = $1.expr;
		$$.expr.length  = $2.expr;

		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	  };

joinexpr:
	musicexpr JOINEXPR musicexpr {
		$$.expr.a = $1;
		$$.expr.b = $3;
		$$.textloc = $2.textloc;
	  }
	;

relnote:
	NOTETOKEN notemods octavemods notelength {
		$$.expr.notesym    = $1.expr;
		$$.expr.notemods   = $2.expr;
		$$.expr.octavemods = $3.expr;
		$$.expr.length     = $4.expr;

		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    &$3.textloc, &$4.textloc, NULL);
	  }
	| NOTETOKEN_ES notemods octavemods notelength {
		/* "es" is a special case */
		$$.expr.notesym    = $1.expr;
		$$.expr.notemods   = $2.expr - 1;
		$$.expr.octavemods = $3.expr;
		$$.expr.length     = $4.expr;

		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    &$3.textloc, &$4.textloc, NULL);
	  }
	;

relsimultence_expr_with_enclosers_and_length:
	RELSIMULTENCE_START simultence_expr RELSIMULTENCE_END notelength {
		struct textloc tl;

		tl = _mdl_join_textlocs(&$1.textloc, &$2->id.textloc,
		    &$3.textloc, &$4.textloc, NULL);

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
	| RELSIMULTENCE_START RELSIMULTENCE_END notelength {
		/* XXX A smarter way to accept empty relsimultences? */
		struct musicexpr *simultence;
		struct textloc tl;

		tl = _mdl_join_textlocs(&$1.textloc, &$2.textloc, &$3.textloc,
		    NULL);

		$$ = _mdl_musicexpr_new(ME_TYPE_RELSIMULTENCE, tl, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		simultence = _mdl_musicexpr_new(ME_TYPE_SIMULTENCE, tl, 0);
		if (simultence == NULL) {
			free($$);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}

		$$->u.scaledexpr.me = simultence;
		$$->u.scaledexpr.length = $3.expr;
	  }
	;

rest:
	RESTTOKEN notelength {
		$$.expr.length = $2.expr;
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	  }
	;

sequence_expr_with_enclosers_and_length:
	sequence_expr_with_enclosers notelength {
		$$ = maybe_apply_scaling($1, $2.expr, $2.textloc);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
	  }
	;
	
sequence_expr_with_enclosers:
	SEQUENCE_START sequence_expr SEQUENCE_END {
		$$ = $2;
		$$->id.textloc = _mdl_join_textlocs(&$1.textloc,
		    &$2->id.textloc, &$3.textloc, NULL);
	  }
	| SEQUENCE_START SEQUENCE_END {
		/* XXX A smarter way to accept empty sequences? */
		struct textloc tl;

		tl = _mdl_join_textlocs(&$1.textloc, &$2.textloc, NULL);

		$$ = _mdl_musicexpr_new(ME_TYPE_SEQUENCE, tl, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		TAILQ_INIT(&$$->u.melist);
	  }
	;

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

simultence_expr_with_enclosers_and_length:
	simultence_expr_with_enclosers notelength {
		$$ = maybe_apply_scaling($1, $2.expr, $2.textloc);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
	  }
	;

simultence_expr_with_enclosers:
	SIMULTENCE_START simultence_expr SIMULTENCE_END {
		$$ = $2;
		$$->id.textloc = _mdl_join_textlocs(&$1.textloc,
		    &$2->id.textloc, &$3.textloc, NULL);
	  }
	| SIMULTENCE_START SIMULTENCE_END {
		/* XXX A smarter way to accept empty simultences? */
		struct textloc tl;

		tl = _mdl_join_textlocs(&$1.textloc, &$2.textloc, NULL);

		$$ = _mdl_musicexpr_new(ME_TYPE_SIMULTENCE, tl, 0);
		if ($$ == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		TAILQ_INIT(&$$->u.melist);
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

		tl = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    &$3->id.textloc, NULL);

		$$ = _mdl_musicexpr_new(ME_TYPE_ONTRACK, tl, 0);
		if ($$ == NULL) {
			free($1.expr);
			_mdl_musicexpr_free($3, 0);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.ontrack.me = $3;

		/* XXX what about drumtracks? */
		$$->u.ontrack.track = _mdl_track_new(INSTR_TONED, $1.expr);
		if ($$->u.ontrack.track == NULL) {
			free($$);
			free($1.expr);
			_mdl_musicexpr_free($3, 0);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
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
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2->id.textloc,
		    NULL);
		TAILQ_INSERT_TAIL(&$$.expr, $2, tq);
	  }
	;

notemods:
	notemods NOTEMODTOKEN_IS {
		$$.expr = $1.expr + $2.expr;
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	}
	| notemods NOTEMODTOKEN_ES {
		$$.expr = $1.expr - $2.expr;
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	}
	| /* empty */ { $$.expr = 0; $$.textloc = _mdl_textloc_zero(); }
	;

octavemods:
	octavemods OCTAVEUP {
		$$.expr = $1.expr + $2.expr;
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	}
	| octavemods OCTAVEDOWN {
		$$.expr = $1.expr - $2.expr;
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
	}
	| /* empty */ { $$.expr = 0; $$.textloc = _mdl_textloc_zero(); }
	;

notelength:
	LENGTHNUMBER lengthdots {
		$$.expr = countlength($1.expr, $2.expr);
		$$.textloc = _mdl_join_textlocs(&$1.textloc, &$2.textloc,
		    NULL);
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

static struct musicexpr *
maybe_apply_scaling(struct musicexpr *me, float length,
    struct textloc extending_tl)
{
	struct musicexpr *scaled_expr;
	struct textloc tl;

	/* Length is zero so no scaling applied to music expression. */
	if (length == 0.0)
		return me;

	tl = _mdl_join_textlocs(&me->id.textloc, &extending_tl, NULL);

	scaled_expr = _mdl_musicexpr_new(ME_TYPE_SCALEDEXPR, tl, 0);
	if (scaled_expr == NULL)
		return NULL;

	scaled_expr->u.scaledexpr.me = me;
	scaled_expr->u.scaledexpr.length = length;

	return scaled_expr;
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
