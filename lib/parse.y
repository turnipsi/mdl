/* $Id: parse.y,v 1.46 2016/03/03 20:54:57 je Exp $

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

struct musicexpr *parsed_expr = NULL;

void	yyerror(const char *fmt, ...);
int	yylex(void);
int	yyparse(void);

static float	countlength(int, int);
static void    *malloc_musicexpr(void);

%}

%union {
	struct chord		chord;
	struct joinexpr		joinexpr;
	struct melist		melist;
	struct musicexpr       *musicexpr;
	struct ontrack		ontrack;
	struct relnote		relnote;
	struct rest		rest;
	enum chordtype		chordtype;
	enum notesym		notesym;
	char		       *string;
	float			f;
	int			i;
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

%%

grammar:
	sequence_expr { parsed_expr = $1; }
	| /* empty */ {
		if ((parsed_expr = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		parsed_expr->me_type = ME_TYPE_EMPTY;
	}
	;

musicexpr:
	chord {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_CHORD;
		$$->u.chord = $1;
	  }
	| joinexpr {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_JOINEXPR;
		$$->u.joinexpr = $1;

	  }
	| relnote {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_RELNOTE;
		$$->u.relnote = $1;
	  }
	| relsimultence_expr { $$ = $1; }
	| rest {
		if (($$ = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_REST;
		$$->u.rest = $1;
	  }
	| SEQUENCE_START sequence_expr SEQUENCE_END { $$ = $2; }
	| SIMULTENCE_START simultence_expr SIMULTENCE_END { $$ = $2; }
	| track_expr { $$ = $1; }
	;

chord:
	relnote chordtype {
		$$.chordtype = $2;
		if (($$.me = malloc_musicexpr()) == NULL) {
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$.me->me_type = ME_TYPE_RELNOTE;
		$$.me->u.relnote = $1;
	}
	;

joinexpr:
	musicexpr JOINEXPR musicexpr {
		$$.a = $1;
		$$.b = $3;
	}
	;

relnote:
	notesym notemods octavemods notelength {
		$$.notesym    = $1;
		$$.notemods   = $2;
		$$.octavemods = $3;
		$$.length     = $4;
	  }
	| NOTETOKEN_ES notemods octavemods notelength {
		/* "es" is a special case */
		$$.notesym    = NOTE_E;
		$$.notemods   = - $1;
		$$.octavemods = $2;
		$$.length     = $3;
	}
	;

relsimultence_expr:
	RELSIMULTENCE_START simultence_expr RELSIMULTENCE_END notelength {
		if (($$ = malloc_musicexpr()) == NULL) {
			musicexpr_free($2);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_RELSIMULTENCE;
		$$->u.scaledexpr.me = $2;
		$$->u.scaledexpr.length = $4;
	  }
	;

rest:
	RESTTOKEN notelength {
		$$.length = $2;
	};

sequence_expr:
	expression_list {
		if (($$ = malloc_musicexpr()) == NULL) {
			musicexpr_free_melist($1);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_SEQUENCE;
		$$->u.melist = $1;
	}
	;

simultence_expr:
	expression_list {
		if (($$ = malloc_musicexpr()) == NULL) {
			musicexpr_free_melist($1);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_SIMULTENCE;
		$$->u.melist = $1;
	  }
	;

track_expr:
	QUOTED_STRING TRACK_OPERATOR musicexpr {
		if (($$ = malloc_musicexpr()) == NULL) {
			free($1);
			musicexpr_free($3);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->me_type = ME_TYPE_ONTRACK;
		$$->u.ontrack.me = $3;
		$$->u.ontrack.track = malloc(sizeof(struct track));
		if ($$->u.ontrack.track == NULL) {
			free($$);
			free($1);
			musicexpr_free($3);
			/* XXX YYERROR and memory leaks?
			 * XXX return NULL and handle on upper layer? */
			YYERROR;
		}
		$$->u.ontrack.track->name = $1;
	  }
	;

expression_list:
	musicexpr {
		TAILQ_INIT(&$$);
		TAILQ_INSERT_TAIL(&$$, $1, tq);
	  }
	| expression_list musicexpr {
		$$ = $1;
		TAILQ_INSERT_TAIL(&$$, $2, tq);
	  }
	;

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
	NOTEMOD_IS    { $$ = + $1; }
	| NOTEMOD_ES  { $$ = - $1; }
	| /* empty */ { $$ = 0;    }
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

chordtype:
	CHORDTOKEN_NONE		{ $$ = CHORDTYPE_NONE;     }
	| CHORDTOKEN_MAJ	{ $$ = CHORDTYPE_MAJ;      }
	| CHORDTOKEN_MIN	{ $$ = CHORDTYPE_MIN;      }
	| CHORDTOKEN_AUG	{ $$ = CHORDTYPE_AUG;      }
	| CHORDTOKEN_DIM	{ $$ = CHORDTYPE_DIM;      }
	| CHORDTOKEN_7		{ $$ = CHORDTYPE_7;        }
	| CHORDTOKEN_MAJ7	{ $$ = CHORDTYPE_MAJ7;     }
	| CHORDTOKEN_MIN7	{ $$ = CHORDTYPE_MIN7;     }
	| CHORDTOKEN_DIM7	{ $$ = CHORDTYPE_DIM7;     }
	| CHORDTOKEN_AUG7	{ $$ = CHORDTYPE_AUG7;     }
	| CHORDTOKEN_DIM5MIN7	{ $$ = CHORDTYPE_DIM5MIN7; }
	| CHORDTOKEN_MIN5MAJ7	{ $$ = CHORDTYPE_MIN5MAJ7; }
	| CHORDTOKEN_MAJ6	{ $$ = CHORDTYPE_MAJ6;     }
	| CHORDTOKEN_MIN6	{ $$ = CHORDTYPE_MIN6;     }
	| CHORDTOKEN_9		{ $$ = CHORDTYPE_9;        }
	| CHORDTOKEN_MAJ9	{ $$ = CHORDTYPE_MAJ9;     }
	| CHORDTOKEN_MIN9	{ $$ = CHORDTYPE_MIN9;     }
	| CHORDTOKEN_11		{ $$ = CHORDTYPE_11;       }
	| CHORDTOKEN_MAJ11	{ $$ = CHORDTYPE_MAJ11;    }
	| CHORDTOKEN_MIN11	{ $$ = CHORDTYPE_MIN11;    }
	| CHORDTOKEN_13		{ $$ = CHORDTYPE_13;       }
	| CHORDTOKEN_13_11	{ $$ = CHORDTYPE_13_11;    }
	| CHORDTOKEN_MAJ13_11	{ $$ = CHORDTYPE_MAJ13_11; }
	| CHORDTOKEN_MIN13_11	{ $$ = CHORDTYPE_MIN13_11; }
	| CHORDTOKEN_SUS2	{ $$ = CHORDTYPE_SUS2;     }
	| CHORDTOKEN_SUS4	{ $$ = CHORDTYPE_SUS4;     }
	| CHORDTOKEN_5		{ $$ = CHORDTYPE_5;        }
	| CHORDTOKEN_5_8	{ $$ = CHORDTYPE_5_8;      }
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

	if ((me = malloc(sizeof(struct musicexpr))) == NULL)
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
