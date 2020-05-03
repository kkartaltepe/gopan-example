A toy example of rendering font, but with all the parts actually needed.

Most examples provide a trivial call to the shaper but no font fallback. Or how
to choose fonts with fallbacks but no shaping. Almost all ignore fribidi (the
easiest step by far). Or dont even provide an example for rendering shaped
glyphs.

This example is based on pango since it has the most accessible code to review
and produces excellent output, if you need to render text you should use it.

We need some unicode information so there is a godawful golang script to build
some tables and enums from the UCD datafiles also provided. You can reference
glib's unicode tables and codegen scripts for a more typical approach to
building these (used in pango).

First we build a maximally covering set of fonts ordered by their close-ness to
the provided pattern (Fontconfig does a great job of this usually). Then we
break the input text into runs based on unicode properties and fonts available
(another often forgotten step this example is intended to showcase).  With runs
of consistent font and shaping properties we can get useful output from the
shaper. Finally we use the shaped glyphs and fonts and render (cairo can
rasterize the paths all on its own so it's extra easy to use).

The the code in this repository is provided under the GPLv3 or MIT, whichever
you prefer. Though its dependencies may be GPL.
