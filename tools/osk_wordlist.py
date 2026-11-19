"""Builds data/osk/words_en.txt, the on-screen keyboard's English word list (GLOSSARY.md: word list).

    python -I tools/osk_wordlist.py --frequency en_50k.txt --scowl scowl-2020.12.07 \
        [--out data/osk/words_en.txt] [--size 30000] [--report]

Inputs (downloaded by hand, never committed; checked against the pinned versions):
  --frequency  hermitdave/FrequencyWords content/2018/en/en_50k.txt at 525f9b560de45753a5ea01069454e72e9aa541c6
               (word counts from OpenSubtitles 2018; content CC BY-SA 4.0, see that commit's README)
               https://raw.githubusercontent.com/hermitdave/FrequencyWords/525f9b560de45753a5ea01069454e72e9aa541c6/content/2018/en/en_50k.txt
  --scowl      SCOWL 2020.12.07, unpacked (Kevin Atkinson; MIT-like, see its Copyright file)
               https://downloads.sourceforge.net/project/wordlist/SCOWL/2020.12.07/scowl-2020.12.07.tar.gz
               final/ gives the capitals of names

What it does to the counts (the decisions behind it are in the commits that added and changed this tool):
  - The list is all lower case and splits contractions ("don" + "'t"): the fragments go, and the common
    contractions come back with a count taken from their fragment (CONTRACTIONS below).
  - Capitals: a word SCOWL knows only with a capital (Monday, London) gets it; "I" and its contractions too.
  - Nothing is left out for being rude: the list is the subtitles' words as they come.
  - LittleBigPlanet's own words are added (LBP_NAMES, LBP_TOOLS, LBP_WORDS).
  - Only words in the keyboard's sense are kept (letters, ' or - between letters), the most common first.

--report prints what to look at before committing a new list: the capitals of some words that are also
names, and the top six suggestions for each letter.
"""
import argparse
import hashlib
import pathlib
import re
import sys

# The pinned inputs; a mismatch only warns (the output would differ from the committed list).
FREQUENCY_SHA256 = "5351ff405b1126ef555791dd4d9798a48e3e9a501a9fc481a9da957752cfb458"
SCOWL_VERSION = "2020.12.07"

WORD = re.compile(r"^[^\W\d_]+(?:['-][^\W\d_]+)*$")

# Pieces of contractions in the frequency list, never words of their own here.
FRAGMENTS = {"don", "didn", "isn", "doesn", "wasn", "couldn", "wouldn", "shouldn", "haven", "hasn", "aren",
             "weren", "ain", "hadn", "mustn", "needn", "ll", "re", "ve"}

# contraction: (fragment whose count it takes, share of that count). The n't stems are the contraction
# (the stem never stands alone); the endings are shared, so each contraction gets an estimated share.
CONTRACTIONS = {
    "don't": ("don", 1.0), "didn't": ("didn", 1.0), "isn't": ("isn", 1.0), "doesn't": ("doesn", 1.0),
    "wasn't": ("wasn", 1.0), "couldn't": ("couldn", 1.0), "wouldn't": ("wouldn", 1.0),
    "shouldn't": ("shouldn", 1.0), "haven't": ("haven", 1.0), "hasn't": ("hasn", 1.0), "aren't": ("aren", 1.0),
    "weren't": ("weren", 1.0), "ain't": ("ain", 1.0), "hadn't": ("hadn", 1.0),
    # 't left over once the stems above are taken is mostly these two
    "can't": ("'t-rest", 0.7), "won't": ("'t-rest", 0.3),
    "I'm": ("'m", 1.0),
    "you're": ("'re", 0.45), "we're": ("'re", 0.3), "they're": ("'re", 0.25),
    "it's": ("'s", 0.3), "that's": ("'s", 0.2), "what's": ("'s", 0.1), "he's": ("'s", 0.09), "there's": ("'s", 0.07),
    "she's": ("'s", 0.06), "let's": ("'s", 0.05), "here's": ("'s", 0.03), "who's": ("'s", 0.02),
    "where's": ("'s", 0.015),
    "I'll": ("'ll", 0.4), "you'll": ("'ll", 0.2), "we'll": ("'ll", 0.15), "it'll": ("'ll", 0.07),
    "he'll": ("'ll", 0.06), "they'll": ("'ll", 0.05), "she'll": ("'ll", 0.04), "that'll": ("'ll", 0.03),
    "I've": ("'ve", 0.45), "you've": ("'ve", 0.2), "we've": ("'ve", 0.15), "they've": ("'ve", 0.1),
    "would've": ("'ve", 0.04), "could've": ("'ve", 0.03), "should've": ("'ve", 0.03),
    "I'd": ("'d", 0.45), "you'd": ("'d", 0.2), "he'd": ("'d", 0.1), "we'd": ("'d", 0.08), "she'd": ("'d", 0.06),
    "they'd": ("'d", 0.06), "it'd": ("'d", 0.03),
}

# Names from the game, always written with a capital, ranked like a common word (the 1000th), each a
# little below the one before it.
LBP_NAMES = ["Sackboy", "Sackgirl", "Sackbot", "Sackbots", "Oddsock", "Newton", "Bunkum", "Ziggurat", "Manglewood",
             "LittleBigPlanet", "LBP", "Popit"]
# The game's tools and collectables, also with a capital but ranked like the 10,000th word, so they come
# up once a few letters are typed rather than pushing out everyday words ("ve" -> very, vehicle...).
LBP_TOOLS = ["Velociporter", "Pumpinator", "Grabinator", "Creatinator", "Paintinator", "Controlinator",
             "Collectabell", "Collectabells"]
# Create-mode words in lower case, added if missing, ranked like the 5000th word.
LBP_WORDS = ["checkpoint", "checkpoints", "scoreboard", "microchip", "microchips", "sequencer", "sequencers",
             "emitter", "emitters", "toggle", "swoop", "pod"]


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def read_frequency(path):
    counts = {}
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) != 2 or not parts[1].isdigit():
            continue
        word = parts[0].lower()
        counts[word] = counts.get(word, 0) + int(parts[1])
    return counts


def read_scowl(root):
    """Lower-case words SCOWL has, and the capitalised form of names it only has with a capital.

    Size 95 (its most obscure words) lists lower-case forms of almost anything ("monday", "london"),
    so only sizes up to 80 decide that a word is written in lower case."""
    final = pathlib.Path(root) / "final"
    lower, capital = set(), {}
    for f in sorted(final.iterdir(), key=lambda p: (p.suffix, p.name)):  # smaller sizes first
        kind = f.name.split(".")[0].split("-")[-1]
        size = int(f.suffix[1:]) if f.suffix[1:].isdigit() else 0
        if kind in ("words", "abbreviations", "contractions") and size > 80:
            continue
        for w in f.read_text(encoding="latin-1").split():
            if "'" in w and kind != "contractions":
                continue  # possessives
            if kind in ("words", "abbreviations", "contractions"):
                if w == w.lower():
                    lower.add(w)
                else:
                    capital.setdefault(w.lower(), w)
            elif kind in ("upper", "names"):
                capital.setdefault(w.lower(), w)
    return lower, capital


def build(args):
    if sha256(args.frequency) != FREQUENCY_SHA256:
        print(f"warning: {args.frequency} isn't the pinned en_50k.txt (sha256 differs)", file=sys.stderr)
    version = pathlib.Path(args.scowl, "VERSION")
    if not version.exists() or SCOWL_VERSION not in version.read_text(encoding="latin-1"):
        print(f"warning: {args.scowl} isn't SCOWL {SCOWL_VERSION}", file=sys.stderr)
    counts = read_frequency(args.frequency)
    lower, capital = read_scowl(args.scowl)

    # The fragments' counts feed the contractions; the n't stems' share of 't is taken out first.
    frag = {k: counts.get(k, 0) for k in ("'s", "'t", "'m", "'re", "'ll", "'ve", "'d")}
    frag.update({k: counts.get(k, 0) for k in FRAGMENTS})
    stems = sum(counts.get(s, 0) for s, share in CONTRACTIONS.values() if s in FRAGMENTS and share == 1.0)
    frag["'t-rest"] = max(0, frag["'t"] - stems)

    words = {}  # lower -> [form, count]
    for w, c in counts.items():
        if w in FRAGMENTS or w.startswith("'") or not WORD.match(w):
            continue
        if len(w) == 1 and w not in ("a", "i"):
            continue
        form = "I" if w == "i" else (capital[w] if w not in lower and w in capital else w)
        words[w] = [form, c]
    for form, (source, share) in CONTRACTIONS.items():
        c = int(frag.get(source, 0) * share)
        if c > 0:
            words[form.lower()] = [form, max(c, words.get(form.lower(), [None, 0])[1])]

    ranked = sorted(words.values(), key=lambda fc: (-fc[1], fc[0].lower()))
    at1000, at5000, at10000 = ranked[999][1], ranked[4999][1], ranked[9999][1]
    for names, at in ((LBP_NAMES, at1000), (LBP_TOOLS, at10000)):
        for i, name in enumerate(names):  # in the order listed: Sackboy before Sackbot
            old = words.get(name.lower(), [name, 0])
            words[name.lower()] = [name, max(old[1], at - i)]
    for w in LBP_WORDS:
        if w not in words:
            words[w] = [w, at5000]
        elif words[w][0] != w and w not in (n.lower() for n in LBP_NAMES):
            words[w][0] = w

    return sorted(words.values(), key=lambda fc: (-fc[1], fc[0].lower()))[: args.size]


def write(ranked, out):
    out = pathlib.Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# The on-screen keyboard's English word list: word<TAB>count, the most common first.",
        "# CC BY-SA 4.0. Adapted from hermitdave/FrequencyWords (OpenSubtitles 2018 counts); see NOTICE.md here.",
        "# Built by tools/osk_wordlist.py; don't edit by hand.",
    ]
    lines += [f"{form}\t{count}" for form, count in ranked]
    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {out}: {len(ranked)} words")


def report(ranked):
    forms = [f for f, _ in ranked]
    print("\n== capitals of words that are also names:")
    by = {f.lower(): f for f in forms}
    for w in ["may", "will", "mark", "bill", "rose", "march", "china", "turkey", "monday", "london", "john", "i",
              "i'm", "newton", "sackboy", "toggle", "god", "english", "ok", "tv"]:
        print(f"  {w}: {by.get(w, '(not in list)')}")
    print("\n== the top six for each letter:")
    for ch in "abcdefghijklmnopqrstuvwxyz":
        print(f"  {ch}: {', '.join([f for f in forms if f.lower().startswith(ch)][:6])}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--frequency", required=True)
    p.add_argument("--scowl", required=True)
    p.add_argument("--out", default="data/osk/words_en.txt")
    p.add_argument("--size", type=int, default=30000)
    p.add_argument("--report", action="store_true")
    args = p.parse_args()
    ranked = build(args)
    write(ranked, args.out)
    if args.report:
        report(ranked)


if __name__ == "__main__":
    main()
