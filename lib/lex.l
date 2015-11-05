%{
/* $Id: lex.l,v 1.6 2015/11/05 20:24:51 je Exp $ */

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

#define yywrap() 1

#include "musicexpr.h"
#include "y.tab.h"   
%}

%%
c	{ return NOTETOKEN_C;   }
cis	{ return NOTETOKEN_CIS; }
des	{ return NOTETOKEN_DES; }
d	{ return NOTETOKEN_D;   }
dis	{ return NOTETOKEN_DIS; }
es	{ return NOTETOKEN_ES;  }
e	{ return NOTETOKEN_E;   }
f	{ return NOTETOKEN_F;   }
fis	{ return NOTETOKEN_FIS; }
ges	{ return NOTETOKEN_GES; }
g	{ return NOTETOKEN_G;   }
gis	{ return NOTETOKEN_GIS; }
aes	{ return NOTETOKEN_AES; }
a	{ return NOTETOKEN_A;   }
ais	{ return NOTETOKEN_AIS; }
bes	{ return NOTETOKEN_BES; }
b	{ return NOTETOKEN_B;   }

[ \t\n]+	{ return WHITESPACE; }
%%
