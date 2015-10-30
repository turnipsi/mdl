%{
/* $Id: lex.l,v 1.3 2015/10/30 21:07:19 je Exp $ */

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

#include "y.tab.h"   
%}

%%
c	{ return NOTE_C;   }
cis	{ return NOTE_CIS; }
des	{ return NOTE_DES; }
d	{ return NOTE_D;   }
dis	{ return NOTE_DIS; }
es	{ return NOTE_ES;  }
e	{ return NOTE_E;   }
f	{ return NOTE_F;   }
fis	{ return NOTE_FIS; }
ges	{ return NOTE_GES; }
g	{ return NOTE_G;   }
gis	{ return NOTE_GIS; }
aes	{ return NOTE_AES; }
a	{ return NOTE_A;   }
ais	{ return NOTE_AIS; }
bes	{ return NOTE_BES; }
b	{ return NOTE_B;   }

[ \t\n]+	/* eat up whitespace */
%%
