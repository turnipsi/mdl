%{
/* $Id: lex.l,v 1.25 2016/04/07 18:49:07 je Exp $ */

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
#include "parse.h"
#include "y.tab.h"

static int	new_chordtype(enum chordtype);
static int	new_notesym(enum notesym);
static int	new_texttoken(int);
static void	update_textloc(struct textloc *);

%}

%option noyywrap

%x quoted

%%
\"			{ update_textloc(NULL); BEGIN(quoted); }

<quoted>[[:alnum:] ]+	{
				/* XXX "-characters do not go into textloc */
				update_textloc(&yylval.string.textloc);
				yylval.string.expr = strdup(yytext);
				if (yylval.string.expr == NULL) {
					yyerror("strdup failed for %s",
					    yytext);
					yyterminate();
				}
				return QUOTED_STRING;
			}

<quoted>\"	{ update_textloc(NULL); BEGIN(INITIAL); }

#.*\n		{ update_textloc(NULL); }

[ \t\n]+es	{
			update_textloc(&yylval.notesym.textloc);
			yylval.notesym.expr = NOTE_E;
			return NOTETOKEN_ES;
		}

(es)+	{
		update_textloc(&yylval.i.textloc);
		yylval.i.expr = strlen(yytext) / 2;
		return NOTEMOD_ES;
	}
(is)+	{
		update_textloc(&yylval.i.textloc);
		yylval.i.expr = strlen(yytext) / 2;
		return NOTEMOD_IS;
	}

c	{ return new_notesym(NOTE_C); }
d	{ return new_notesym(NOTE_D); }
e	{ return new_notesym(NOTE_E); }
f	{ return new_notesym(NOTE_F); }
g	{ return new_notesym(NOTE_G); }
a	{ return new_notesym(NOTE_A); }
b	{ return new_notesym(NOTE_B); }

r	{ update_textloc(&yylval.textloc.textloc); return RESTTOKEN; }

\.+	{
		update_textloc(&yylval.i.textloc);
		yylval.i.expr = strlen(yytext);
		return LENGTHDOT;
	}

[1-9][0-9]*	{
			update_textloc(&yylval.i.textloc);
			/* Things later presume this is > 0,
			 * so do not change this to allow zero without
			 * thinking the implications. */
			yylval.i.expr = strtonum(yytext, 1, INT_MAX, NULL);
			if (yylval.i.expr == 0) {
				yyerror("invalid numeric conversion for %s",
				    yytext);
				yyterminate();
			}
			return LENGTHNUMBER;
		}
'+		{
			update_textloc(&yylval.i.textloc);
			yylval.i.expr = strlen(yytext);
			return OCTAVEUP;
		}
,+		{
			update_textloc(&yylval.i.textloc);
			yylval.i.expr = strlen(yytext);
			return OCTAVEDOWN;
		}

~		{ update_textloc(&yylval.textloc.textloc); return JOINEXPR; }

:5		{ return new_chordtype(CHORDTYPE_MAJ);      }
:m		{ return new_chordtype(CHORDTYPE_MIN);      }
:m5		{ return new_chordtype(CHORDTYPE_MIN);      }
:aug		{ return new_chordtype(CHORDTYPE_AUG);      }
:dim		{ return new_chordtype(CHORDTYPE_DIM);      }
:7		{ return new_chordtype(CHORDTYPE_7);        }
:maj7		{ return new_chordtype(CHORDTYPE_MAJ7);     }
:maj		{ return new_chordtype(CHORDTYPE_MAJ7);     }
:m7		{ return new_chordtype(CHORDTYPE_MIN7);     }
:dim7		{ return new_chordtype(CHORDTYPE_DIM7);     }
:aug7		{ return new_chordtype(CHORDTYPE_AUG7);     }
:m7\.5-		{ return new_chordtype(CHORDTYPE_DIM5MIN7); }
:m7\+		{ return new_chordtype(CHORDTYPE_MIN5MAJ7); }
:6		{ return new_chordtype(CHORDTYPE_MAJ6);     }
:m6		{ return new_chordtype(CHORDTYPE_MIN6);     }
:9		{ return new_chordtype(CHORDTYPE_9);        }
:maj9		{ return new_chordtype(CHORDTYPE_MAJ9);     }
:m9		{ return new_chordtype(CHORDTYPE_MIN9);     }
:11		{ return new_chordtype(CHORDTYPE_11);       }
:maj11		{ return new_chordtype(CHORDTYPE_MAJ11);    }
:m11		{ return new_chordtype(CHORDTYPE_MIN11);    }
:13		{ return new_chordtype(CHORDTYPE_13);       }
:13\.11		{ return new_chordtype(CHORDTYPE_13_11);    }
:maj13\.11	{ return new_chordtype(CHORDTYPE_MAJ13_11); }
:m13\.11	{ return new_chordtype(CHORDTYPE_MIN13_11); }
:sus2		{ return new_chordtype(CHORDTYPE_SUS2);     }
:sus4		{ return new_chordtype(CHORDTYPE_SUS4);     }
:1\.5		{ return new_chordtype(CHORDTYPE_5);        }
:1\.5\.8	{ return new_chordtype(CHORDTYPE_5_8);      }
:		{ return new_chordtype(CHORDTYPE_NONE);     }

\{		{ return new_texttoken(SEQUENCE_START); }
\}		{ return new_texttoken(SEQUENCE_END);   }

\<\<		{ return new_texttoken(SIMULTENCE_START); }
\>\>		{ return new_texttoken(SIMULTENCE_END);   }

\<		{ return new_texttoken(RELSIMULTENCE_START); }
\>		{ return new_texttoken(RELSIMULTENCE_END);   }

::		{ return new_texttoken(TRACK_OPERATOR); }

[ \t\n]+	{ update_textloc(NULL); }

<*>.|\n		{
			/* XXX add current textloc to error message */
			yyerror("unknown token: %s", yytext);
			yyterminate();
		}
%%

static int
new_chordtype(enum chordtype chordtype)
{
	update_textloc(&yylval.chordtype.textloc);
	yylval.chordtype.expr = chordtype;
	return CHORDTOKEN;
}

static int
new_notesym(enum notesym notesym)
{
	update_textloc(&yylval.notesym.textloc);
	yylval.notesym.expr = notesym;
	return NOTETOKEN;
}

static int
new_texttoken(int token)
{
	update_textloc(&yylval.textloc.textloc);
	return token;
}

static void
update_textloc(struct textloc *tl)
{
	static int current_line = 1;
	static int current_column = 1;
	char *s;

	if (tl != NULL) {
		tl->first_line = current_line;
		tl->first_column = current_column;
	}

	for (s = yytext; *s != '\0'; s++) {
		if (*s == '\n') {
			current_line++;
			current_column = 1;
		} else {
			current_column++;
		}
	}

	if (tl != NULL) {
		tl->last_line = current_line;
		tl->last_column = current_column - 1;
	}
}
