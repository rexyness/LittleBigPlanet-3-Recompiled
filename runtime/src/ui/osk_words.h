// Word suggestions for the on-screen keyboard (GLOSSARY.md: suggestion, word list, learned
// word): the word list shipped with the port, the words the player has used, and how the two
// rank. No drawing and no files: ui/osk_overlay.cpp reads and writes the text, and osktest
// drives this directly.
//
// The rules, as decided for the feature:
// - A word is letters (accented ones too), with ' or - allowed between letters ("don't",
//   "co-op"), at least two letters long. Numbers, symbols and addresses are never learned.
// - Suggestions complete the part of a word just before the cursor, from its first letter,
//   only while the cursor is at the end of the word.
// - Learned words come first, most used first; then the word list, most common first. A word
//   that isn't in the word list is suggested only from its second use (a typo doesn't stick).
// - A suggestion follows the capitals typed: "sack" -> "sackboy", "Sack" -> "Sackboy",
//   "SACK" -> "SACKBOY"; a word always written with a capital ("Sackboy", "I'm") keeps it.
// - Learning counts the words of the confirmed text that weren't already in the text the
//   keyboard opened with. Counts never fade; at most 10,000 words are kept, the least used
//   going first.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ui::osk {

// Letters for word purposes: Latin (with accents), Greek and Cyrillic.
bool IsLetter(char16_t c);
char16_t ToLower(char16_t c);
char16_t ToUpper(char16_t c);
std::u16string Lower(std::u16string_view s);
std::u16string Upper(std::u16string_view s);

// The words of a text, as written (see the rules above).
std::vector<std::u16string> WordsIn(std::u16string_view text);

// The part of a word just before `cursor` that a suggestion would complete, or empty: the
// cursor must be at the end of the word (not followed by a letter, ' or -), and the word must
// not be glued to a digit or a symbol like @ or _ (a name tag, "lvl2"). `start` gets where it
// begins.
std::u16string_view TypedPart(std::u16string_view text, size_t cursor, size_t* start = nullptr);

// `form` with the capitals of `typed` (the rules above).
std::u16string FollowCase(std::u16string_view form, std::u16string_view typed);

class Words {
public:
    static constexpr size_t kMaxLearned = 10000;
    static constexpr size_t kMaxSuggestions = 6;

    // The word list: UTF-8 "word<TAB>count" lines; '#' starts a comment line. A word listed
    // twice keeps its first line. Replaces any list loaded before.
    void LoadList(std::string_view utf8);
    // The learned words: UTF-8 "count<TAB>word" lines, as SaveLearned writes them. The player
    // may edit the file, so blank, malformed and comment lines are skipped, a word listed twice
    // adds up, and CRLF is fine. Replaces any learned words loaded before.
    void LoadLearned(std::string_view utf8);
    std::string SaveLearned() const;

    // Up to `max` suggestions for `typed` (a TypedPart), best first, with its capitals; none
    // longer than `room` UTF-16 units and none identical to `typed`.
    std::vector<std::u16string> Suggest(std::u16string_view typed, size_t room, size_t max = kMaxSuggestions) const;
    // Counts the words of `confirmed` that weren't in `opening` (a word there twice covers two
    // uses). True if anything was learned.
    bool Learn(std::u16string_view opening, std::u16string_view confirmed);

    size_t listSize() const { return list_.size(); }
    size_t learnedSize() const { return learned_.size(); }
    uint32_t LearnedCount(std::u16string_view word) const;  // 0 if not learned

private:
    struct ListWord {
        std::u16string key;   // lower case
        std::u16string form;  // as it's written ("I'm", "Monday")
        uint64_t count = 0;
    };
    struct Learned {
        std::u16string form;  // the list's spelling for a list word, else as last typed
        uint32_t count = 0;
    };
    const ListWord* FindListed(const std::u16string& key) const;
    void Trim();

    std::vector<ListWord> list_;  // sorted by key, for a prefix's range
    std::unordered_map<std::u16string, size_t> listIndex_;
    std::unordered_map<std::u16string, Learned> learned_;  // by key
};

}  // namespace ui::osk
