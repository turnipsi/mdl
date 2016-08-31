%{
/* $Id: lex.l,v 1.30 2016/08/31 16:58:23 je Exp $ */

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
static int	new_drumsym(enum drumsym);
static int	new_notesym(enum notesym);
static int	new_texttoken(int);
static void	update_textloc(struct textloc *);

%}

%option nounput
%option noyywrap

%x funcargs
%x funcname
%x quoted

%%
\\			{
				update_textloc(NULL);
				BEGIN(funcname);
			}

<funcname>[[:alpha:]]+	{
				update_textloc(&yylval.string.textloc);
				yylval.string.expr = strdup(yytext);
				if (yylval.string.expr == NULL) {
					yyerror("strdup failed for %s",
					    yytext);
					yyterminate();
				}
				BEGIN(funcargs);
				return FUNCNAME_TOKEN;
			}

<funcname>[ \t]+	{ update_textloc(NULL); }
<funcname>\\\n		{ update_textloc(NULL); }
<funcname>\n|;		{ update_textloc(NULL); BEGIN(INITIAL); }

<funcargs>[[:alnum:]]+	{
				update_textloc(&yylval.string.textloc);
				yylval.string.expr = strdup(yytext);
				if (yylval.string.expr == NULL) {
					yyerror("strdup failed for %s",
					    yytext);
					yyterminate();
				}
				return FUNCARG_TOKEN;
			}

<funcargs>[ \t]+	{ update_textloc(NULL); }
<funcargs>\\\n		{ update_textloc(NULL); }
<funcargs>\n|;		{ update_textloc(NULL); BEGIN(INITIAL); }

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
		return NOTEMODTOKEN_ES;
	}
(is)+	{
		update_textloc(&yylval.i.textloc);
		yylval.i.expr = strlen(yytext) / 2;
		return NOTEMODTOKEN_IS;
	}

c	{ return new_notesym(NOTE_C); }
d	{ return new_notesym(NOTE_D); }
e	{ return new_notesym(NOTE_E); }
f	{ return new_notesym(NOTE_F); }
g	{ return new_notesym(NOTE_G); }
a	{ return new_notesym(NOTE_A); }
b	{ return new_notesym(NOTE_B); }

acousticbassdrum|bda	{ return new_drumsym(DRUM_BDA);   }
bassdrum|bd		{ return new_drumsym(DRUM_BD);    }
hisidestick|ssh		{ return new_drumsym(DRUM_SSH);   }
sidestick|ss		{ return new_drumsym(DRUM_SS);    }
losidestick|ssl		{ return new_drumsym(DRUM_SSL);   }
acousticsnare|sna	{ return new_drumsym(DRUM_SNA);   }
snare|sn		{ return new_drumsym(DRUM_SN);    }
handclap|hc		{ return new_drumsym(DRUM_HC);    }
electricsnare|sne	{ return new_drumsym(DRUM_SNE);   }
lowfloortom|tomfl	{ return new_drumsym(DRUM_TOMFL); }
closedhihat|hhc		{ return new_drumsym(DRUM_HHC);   }
hihat|hh		{ return new_drumsym(DRUM_HH);    }
highfloortom|tomfh	{ return new_drumsym(DRUM_TOMFH); }
pedalhihat|hhp		{ return new_drumsym(DRUM_HHP);   }
lowtom|toml		{ return new_drumsym(DRUM_TOML);  }
openhihat|hho		{ return new_drumsym(DRUM_HHO);   }
halfopenhihat|hhho	{ return new_drumsym(DRUM_HHHO);  }
lowmidtom|tomml		{ return new_drumsym(DRUM_TOMML); }
himidtom|tommh		{ return new_drumsym(DRUM_TOMMH); }
crashcymbala|cymca	{ return new_drumsym(DRUM_CYMCA); }
crashcymbal|cymc	{ return new_drumsym(DRUM_CYMC);  }
hightom|tomh		{ return new_drumsym(DRUM_TOMH);  }
ridecymbala|cymra	{ return new_drumsym(DRUM_CYMRA); }
ridecymbal|cymr		{ return new_drumsym(DRUM_CYMR);  }
chinesecymbal|cymch	{ return new_drumsym(DRUM_CYMCH); }
ridebell|rb		{ return new_drumsym(DRUM_RB);    }
tambourine|tamb		{ return new_drumsym(DRUM_TAMB);  }
splashcymbal|cyms	{ return new_drumsym(DRUM_CYMS);  }
cowbell|cb		{ return new_drumsym(DRUM_CB);    }
crashcymbalb|cymcb	{ return new_drumsym(DRUM_CYMCB); }
vibraslap|vibs		{ return new_drumsym(DRUM_VIBS);  }
ridecymbalb|cymrb	{ return new_drumsym(DRUM_CYMRB); }
mutehibongo|bohm	{ return new_drumsym(DRUM_BOHM);  }
hibongo|boh		{ return new_drumsym(DRUM_BOH);   }
openhibongo|boho	{ return new_drumsym(DRUM_BOHO);  }
mutelobongo|bolm	{ return new_drumsym(DRUM_BOLM);  }
lobongo|bol		{ return new_drumsym(DRUM_BOL);   }
openlobongo|bolo	{ return new_drumsym(DRUM_BOLO);  }
mutehiconga|cghm	{ return new_drumsym(DRUM_CGHM);  }
muteloconga|cglm	{ return new_drumsym(DRUM_CGLM);  }
openhiconga|cgho	{ return new_drumsym(DRUM_CGHO);  }
hiconga|cgh		{ return new_drumsym(DRUM_CGH);   }
openloconga|cglo	{ return new_drumsym(DRUM_CGLO);  }
loconga|cgl		{ return new_drumsym(DRUM_CGL);   }
hitimbale|timh		{ return new_drumsym(DRUM_TIMH);  }
lotimbale|timl		{ return new_drumsym(DRUM_TIML);  }
hiagogo|agh		{ return new_drumsym(DRUM_AGH);   }
loagogo|agl		{ return new_drumsym(DRUM_AGL);   }
cabasa|cab		{ return new_drumsym(DRUM_CAB);   }
maracas|mar		{ return new_drumsym(DRUM_MAR);   }
shortwhistle|whs	{ return new_drumsym(DRUM_WHS);   }
longwhistle|whl		{ return new_drumsym(DRUM_WHL);   }
shortguiro|guis		{ return new_drumsym(DRUM_GUIS);  }
longguiro|guil		{ return new_drumsym(DRUM_GUIL);  }
guiro|gui		{ return new_drumsym(DRUM_GUI);   }
claves|cl		{ return new_drumsym(DRUM_CL);    }
hiwoodblock|wbh		{ return new_drumsym(DRUM_WBH);   }
lowoodblock|wbl		{ return new_drumsym(DRUM_WBL);   }
mutecuica|cuim		{ return new_drumsym(DRUM_CUIM);  }
opencuica|cuio		{ return new_drumsym(DRUM_CUIO);  }
mutetriangle|trim	{ return new_drumsym(DRUM_TRIM);  }
triangle|tri		{ return new_drumsym(DRUM_TRI);   }
opentriangle|trio	{ return new_drumsym(DRUM_TRIO);  }

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
new_drumsym(enum drumsym drumsym)
{
	update_textloc(&yylval.drumsym.textloc);
	yylval.drumsym.expr = drumsym;
	return DRUMTOKEN;
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
