Mdl is a music description language with a MIDI sequencer.  It is
in early stages of development and is probably not useful for you
at the moment.  It is written primarily on OpenBSD, but should also
work on other BSDs and Linux (but may not be quite there yet).

Syntax mimicks the Lilypond syntax but is not compatible.
[Lilypond](http://lilypond.org/web/) is a nice project and is an
inspiration but it has somewhat of a different focus than Mdl.
Lilypond is primarily designed for music typesetting, whereas Mdl
does not try to do that at all.

Mdl project goals include:

  - implement a programming language for describing music
    in a compact way
  - provide functionality needed by composers as they are doing
    their work, for example techniques of algorithmic composition
  - implement a MIDI sequencer that should allow changing the music
    expression it is playing, adapting seamlessly to new music
    expression
  - integration with some popular text editors (vim, emacs, ...) for
    real-time syntax highlighting of music expressions as they are
    playing
  - fast interpretation of music expressions - one should not need to
    wait a second to test changes in music (no "compile to midifile"
    or any such step)
  - programming new expressions should be convenient - there should
    not be much difference between music expressions and "programming
    code"
  - be secure, playing music files should be not be more of a risk than
    playing mp3s or such (try to code safely, and use pledge and
    other tricks on OpenBSD)
  - use C for core functionality, but provide extensibility in the
    language itself with Tcl or some other such scripting language
    (extension language must provide a "safe"-mode to evaluate
    expressions)
  - provide a library that can be linked to by other programs
  - use OpenBSD default license or public domain
  - be portable across *BSDs and Linux, possibly others
  - be an old-school unix program, do not follow latest trends
  - overthrow the music business! ;-)

What Mdl is not or does not do:

  - is not a synth, it does not produce audio
  - is not a music typesetter, it does not produce sheet music
    (use lilypond for that)
  - does not provide a GUI-interface
