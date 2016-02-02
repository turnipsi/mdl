%{
/* $Id: lex.l,v 1.18 2016/02/02 21:08:19 je Exp $ */

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

%x quotedstring

%%
\"			BEGIN(quotedstring);

<quotedstring>[[:alnum:] ]+	{
					yylval.string = strdup(yytext);
					if (yylval.string == NULL) {
						/* XXX error handling */
					}
					return QUOTED_STRING;
				}

<quotedstring>\"	BEGIN(INITIAL);

[ \t\n]+es { return NOTETOKEN_ES; }

(es)+	{ yylval.i = strlen(yytext) / 2; return NOTEMOD_ES; }
(is)+	{ yylval.i = strlen(yytext) / 2; return NOTEMOD_IS; }

c	{ return NOTETOKEN_C; }
d	{ return NOTETOKEN_D; }
e	{ return NOTETOKEN_E; }
f	{ return NOTETOKEN_F; }
g	{ return NOTETOKEN_G; }
a	{ return NOTETOKEN_A; }
b	{ return NOTETOKEN_B; }

r	{ return RESTTOKEN; }

\.+		{ yylval.i = strlen(yytext); return LENGTHDOT; }
[1-9][0-9]*	{
			/* XXX error handling */
			/* things later presume this is > 0,
			 * so do not change this to allow zero without
			 * thinking the implications */
			yylval.i = strtonum(yytext, 1, INT_MAX, NULL);
			return LENGTHNUMBER;
		}
'+		{ yylval.i = strlen(yytext); return OCTAVEUP; }
,+		{ yylval.i = strlen(yytext); return OCTAVEDOWN; }

~ 		{ return JOINEXPR; }

:5		{ return CHORDTOKEN_MAJ;      }
:m		{ return CHORDTOKEN_MIN;      }
:m5		{ return CHORDTOKEN_MIN;      }
:aug		{ return CHORDTOKEN_AUG;      }
:dim		{ return CHORDTOKEN_DIM;      }
:7		{ return CHORDTOKEN_7;        }
:maj7		{ return CHORDTOKEN_MAJ7;     }
:maj		{ return CHORDTOKEN_MAJ7;     }
:m7		{ return CHORDTOKEN_MIN7;     }
:dim7		{ return CHORDTOKEN_DIM7;     }
:aug7		{ return CHORDTOKEN_AUG7;     }
:m7\.5-		{ return CHORDTOKEN_DIM5MIN7; }
:m7\+		{ return CHORDTOKEN_MIN5MAJ7; }
:6		{ return CHORDTOKEN_MAJ6;     }
:m6		{ return CHORDTOKEN_MIN6;     }
:9		{ return CHORDTOKEN_9;        }
:maj9		{ return CHORDTOKEN_MAJ9;     }
:m9		{ return CHORDTOKEN_MIN9;     }
:11		{ return CHORDTOKEN_11;       }
:maj11		{ return CHORDTOKEN_MAJ11;    }
:m11		{ return CHORDTOKEN_MIN11;    }
:13		{ return CHORDTOKEN_13;       }
:13\.11		{ return CHORDTOKEN_13_11;    }
:maj13\.11	{ return CHORDTOKEN_MAJ13_11; }
:m13\.11	{ return CHORDTOKEN_MIN13_11; }
:sus2		{ return CHORDTOKEN_SUS2;     }
:sus4		{ return CHORDTOKEN_SUS4;     }
:1\.5		{ return CHORDTOKEN_5;        }
:1\.5\.8	{ return CHORDTOKEN_5_8;      }
:		{ return CHORDTOKEN_NONE;     }

\{		{ return SEQUENCE_START; }
\}		{ return SEQUENCE_END;   }

\<\<		{ return SIMULTENCE_START; }
\>\>		{ return SIMULTENCE_END;   }

\<		{ return RELSIMULTENCE_START; }
\>		{ return RELSIMULTENCE_END;   }

::		{ return TRACK_OPERATOR; }

[ \t\n]+	{}
%%

/* XXX I do not know why I need this */
int
yywrap(void)
{
	return 1;
}
