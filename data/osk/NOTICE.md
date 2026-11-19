# The on-screen keyboard's word list

`words_en.txt` is the English word list behind the on-screen keyboard's word suggestions: one word and a
count per line, the most common first. The port reads it at the first keyboard of a run.

## Licence

`words_en.txt` is licensed under the **Creative Commons Attribution-ShareAlike 4.0 International** licence
(CC BY-SA 4.0), https://creativecommons.org/licenses/by-sa/4.0/. That licence covers this file only; the rest
of the port is free software under the GNU GPL (see `licenses\` in a package, `LICENSE` in the source).

## Where it comes from

It is adapted from **FrequencyWords** by Hermit Dave, https://github.com/hermitdave/FrequencyWords, the file
`content/2018/en/en_50k.txt` at commit `525f9b560de45753a5ea01069454e72e9aa541c6`, whose content is licensed
CC BY-SA 4.0. Its counts come from the **OpenSubtitles 2018** corpus: subtitles from
http://www.opensubtitles.org/, prepared by OPUS (P. Lison and J. Tiedemann, 2016, *OpenSubtitles2016:
Extracting Large Parallel Corpora from Movie and TV Subtitles*, LREC 2016), http://opus.nlpl.eu/.

## What was changed

`tools/osk_wordlist.py` in the port's source builds it from those files:

- The pieces of split contractions ("don", "'t", "'s") were removed, and the common contractions ("don't",
  "it's", "I'm") added back with counts estimated from those pieces.
- Names were given their capitals ("Monday", "London", "I") using SCOWL (below).
- Swear words and slurs were left out, using SCOWL's lists of taboo words, the LDNOOBW list (below) and a
  short list of the port's own.
- Words from LittleBigPlanet were added ("Sackboy", "Oddsock", "Popit").
- Only the 30,000 most common words were kept.

## Lists used to make it

**SCOWL** (Spell Checker Oriented Word Lists) 2020.12.07, http://wordlist.aspell.net/, was used for the capitals
and the taboo words. Its notice:

> Copyright 2000-2018 by Kevin Atkinson
>
> Permission to use, copy, modify, distribute and sell these word lists, the associated scripts, the output
> created from the scripts, and its documentation for any purpose is hereby granted without fee, provided
> that the above copyright notice appears in all copies and that both that copyright notice and this
> permission notice appear in supporting documentation. Kevin Atkinson makes no representations about the
> suitability of this array for any purpose. It is provided "as is" without express or implied warranty.

**LDNOOBW**, the List of Dirty, Naughty, Obscene, and Otherwise Bad Words,
https://github.com/LDNOOBW/List-of-Dirty-Naughty-Obscene-and-Otherwise-Bad-Words (CC BY 4.0), was used only
to leave words out; none of its text is in `words_en.txt`.
