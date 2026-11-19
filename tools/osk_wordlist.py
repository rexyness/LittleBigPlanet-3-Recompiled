"""Builds data/osk/words_en.txt, the on-screen keyboard's English word list (GLOSSARY.md: word list).

    python -I tools/osk_wordlist.py --frequency en_50k.txt --scowl scowl-2020.12.07 --blocklist en \
        [--out data/osk/words_en.txt] [--size 30000] [--report]
    python -I tools/osk_wordlist.py --hash WORD      the hash to add a word to EXTRA_BLOCK_SHA256

Inputs (downloaded by hand, never committed; their SHA-256 is checked):
  --frequency  hermitdave/FrequencyWords content/2018/en/en_50k.txt at 525f9b560de45753a5ea01069454e72e9aa541c6
               (word counts from OpenSubtitles 2018; content CC BY-SA 4.0, see that commit's README)
               https://raw.githubusercontent.com/hermitdave/FrequencyWords/525f9b560de45753a5ea01069454e72e9aa541c6/content/2018/en/en_50k.txt
  --scowl      SCOWL 2020.12.07, unpacked (Kevin Atkinson; MIT-like, see its Copyright file)
               https://downloads.sourceforge.net/project/wordlist/SCOWL/2020.12.07/scowl-2020.12.07.tar.gz
               final/ gives the capitals of names; misc/ (offensive.1, offensive.2, profane.1) words to leave out
  --blocklist  LDNOOBW List-of-Dirty-Naughty-Obscene-and-Otherwise-Bad-Words "en" at 4638b970cb8d9d82789564fcba1f4a1eb508ff1a
               (CC BY 4.0), more words to leave out
               https://raw.githubusercontent.com/LDNOOBW/List-of-Dirty-Naughty-Obscene-and-Otherwise-Bad-Words/4638b970cb8d9d82789564fcba1f4a1eb508ff1a/en

What it does to the counts (the decisions behind it are in the commit that added this tool):
  - The list is all lower case and splits contractions ("don" + "'t"): the fragments go, and the common
    contractions come back with a count taken from their fragment (CONTRACTIONS below).
  - Capitals: a word SCOWL knows only with a capital (Monday, London) gets it; "I" and its contractions too.
  - Swear words and slurs are left out: SCOWL's offensive and profane lists (but for a few harmless
    words in profane.3), LDNOOBW's single words with -s/-es (but for some ordinary words it lists for
    another sense), anything containing the strongest swear words, and EXTRA_BLOCK_SHA256 below.
  - LittleBigPlanet's own words are added (LBP_NAMES, LBP_WORDS).
  - Only words in the keyboard's sense are kept (letters, ' or - between letters), the most common first.

--report prints what to look at before committing a new list: blocked words found as parts of kept words,
the capitals of some words that are also names, and the top six suggestions for each letter.
"""
import argparse
import hashlib
import pathlib
import re
import sys

# The pinned inputs; a mismatch only warns (the output would differ from the committed list).
FREQUENCY_SHA256 = "5351ff405b1126ef555791dd4d9798a48e3e9a501a9fc481a9da957752cfb458"
BLOCKLIST_SHA256 = "af851ecef1d5f212caba17339b12ac39cc2fef7d78c74876f67237644fcee8bd"
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


# The port's own additions are kept as hashes (the first 16 hex digits of the SHA-256 of the lower-case
# word), so this public file doesn't spell out slurs; `--hash WORD` prints one to add.
# The six strongest swear words, left out wherever they appear in a word ("...-up", "...hole"):
# (length, hash) of each.
STRONG_SHA256 = {
    (4, "566f532d486c9477"),
    (4, "6ac3c336e4094835"),
    (4, "85fc17f7069acd39"),
    (4, "c2c3b68b48832afd"),
    (5, "9ae315a94e428a7e"),
    (5, "d75a838dc758ba17"),
}
# 47 slurs, insults and mild swear words that neither list has (and their plurals and other forms),
# found by reviewing a built list.
EXTRA_BLOCK_SHA256 = {
    "06a1d8bf7fc210de", "0dcc4d3364433181", "158869a97379229b", "1802d081455e60cd",
    "1dc96d865058d53a", "24dce6a4da3fb576", "2b3240970c71346b", "3314b0999a53aafe",
    "43b45d0f0863bdd8", "45cd3e1bb472d8e2", "5cb6ba4a9d88885c", "619bc82e0d200e4a",
    "6e7727239c684e8b", "70a4ce3720ccf427", "70aafb4266950c0d", "7540bd4356952df4",
    "819837a1df89760e", "82159dd02870c13e", "867d78976655050c", "886d51e97ad7931d",
    "92a9bf818e7e0002", "a40ccba874d28799", "a821c62e8104f851", "affc875700667e07",
    "b04a8a4006358447", "b601d5e9e6c9804e", "b9a365c4d2675d5f", "bbf8bb7863176009",
    "bd331fb1d24298f5", "bdb1cc258d6976aa", "c1cabb6f6e431f9c", "ca05283dee355647",
    "cc02032349c833ac", "ccff74c2d57f45d2", "d11215a13d66dbd3", "db10a7906121f4ea",
    "dc52ab1c9ebf4574", "dd14a884ef303d84", "de44de95f1339e73", "de569a7f6852ba73",
    "e9143a29033d9145", "f0288d013f92d92e", "f1a5fb56076acc84", "f9d0d9b18ae9033a",
    "fa38a8d3e5b92744", "fb96c27642b4bd11", "ff454a8739cfe724",
}


def word_hash(word):
    return hashlib.sha256(word.lower().encode("utf-8")).hexdigest()[:16]
# SCOWL's milder profane.3 words are left out too, except these.
MILD_KEEP = {"dickens", "dicker", "dickers", "fart", "farted", "farting", "farts"}
# Ordinary words that LDNOOBW lists for another sense.
NOT_BLOCKED = {"suck", "sucks", "sucked", "sucking", "sucker", "suckers", "escort", "escorts", "escorted",
               "escorting", "snatch", "snatched", "snatching", "playboy", "hardcore", "domination", "skeeter",
               "scat", "booby", "spunk", "poof", "spices"}


def read_blocked(scowl_root, blocklist):
    """The roots (for the report) and every word left out. SCOWL's lists carry their own inflections;
    LDNOOBW's words get -s and -es."""
    misc = pathlib.Path(scowl_root) / "misc"
    roots, blocked = set(), set()
    for name in ("offensive.1", "offensive.2", "profane.1", "profane.3"):
        for w in (misc / name).read_text(encoding="latin-1").split():
            w = w.lower()
            if "'" in w or w in MILD_KEEP:
                continue
            roots.add(w)
            blocked.add(w)
    for line in pathlib.Path(blocklist).read_text(encoding="utf-8").splitlines():
        w = line.strip().lower()
        if w and " " not in w:
            roots.add(w)
            blocked.update({w, w + "s", w + "es"})
    return roots, blocked - NOT_BLOCKED


STRONG_BY_LENGTH = {}
for _n, _h in STRONG_SHA256:
    STRONG_BY_LENGTH.setdefault(_n, set()).add(_h)


def is_blocked(word, blocked):
    if word in blocked or word_hash(word) in EXTRA_BLOCK_SHA256:
        return True
    return any(word_hash(word[i:i + n]) in hashes
               for n, hashes in STRONG_BY_LENGTH.items() for i in range(len(word) - n + 1))


def build(args):
    if sha256(args.frequency) != FREQUENCY_SHA256:
        print(f"warning: {args.frequency} isn't the pinned en_50k.txt (sha256 differs)", file=sys.stderr)
    if sha256(args.blocklist) != BLOCKLIST_SHA256:
        print(f"warning: {args.blocklist} isn't the pinned LDNOOBW en (sha256 differs)", file=sys.stderr)
    version = pathlib.Path(args.scowl, "VERSION")
    if not version.exists() or SCOWL_VERSION not in version.read_text(encoding="latin-1"):
        print(f"warning: {args.scowl} isn't SCOWL {SCOWL_VERSION}", file=sys.stderr)
    counts = read_frequency(args.frequency)
    lower, capital = read_scowl(args.scowl)
    roots, blocked = read_blocked(args.scowl, args.blocklist)

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
        if is_blocked(w, blocked):
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

    ranked = sorted(words.values(), key=lambda fc: (-fc[1], fc[0].lower()))[: args.size]
    return ranked, roots, blocked


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


def report(ranked, roots):
    forms = [f for f, _ in ranked]
    print("\n== blocked roots found inside kept words (check none of these is offensive):")
    hits = {}
    for r in sorted(roots):
        if len(r) < 3:
            continue
        found = [f for f in forms if r in f.lower() and f.lower() != r]
        if found:
            hits[r] = found[:8]
    for r, found in hits.items():
        print(f"  {r}: {', '.join(found)}")
    print("\n== capitals of words that are also names:")
    by = {f.lower(): f for f in forms}
    for w in ["may", "will", "mark", "bill", "rose", "march", "china", "turkey", "monday", "london", "john", "i",
              "i'm", "newton", "sackboy", "toggle", "god", "english", "ok", "tv"]:
        print(f"  {w}: {by.get(w, '(not in list)')}")
    print("\n== the top six for each letter:")
    for ch in "abcdefghijklmnopqrstuvwxyz":
        print(f"  {ch}: {', '.join([f for f in forms if f.lower().startswith(ch)][:6])}")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--hash":
        print(word_hash(sys.argv[2]))  # for EXTRA_BLOCK_SHA256 / STRONG_SHA256
        return
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--frequency", required=True)
    p.add_argument("--scowl", required=True)
    p.add_argument("--blocklist", required=True)
    p.add_argument("--out", default="data/osk/words_en.txt")
    p.add_argument("--size", type=int, default=30000)
    p.add_argument("--report", action="store_true")
    args = p.parse_args()
    ranked, roots, _ = build(args)
    write(ranked, args.out)
    if args.report:
        report(ranked, roots)


if __name__ == "__main__":
    main()
