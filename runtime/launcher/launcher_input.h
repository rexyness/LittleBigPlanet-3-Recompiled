// The controls editor's model of user\config\input.ini (the runtime's keyboard actions and the
// optional pad_* lines; input/action_table.h). Lines it doesn't know (comments, unknown keys) are
// kept as they are; known keys get their new values in place; pad lines are only written when they
// differ from the defaults, so an untouched pad behaves exactly as before.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace launcher {

struct ControlRow {
    std::string key;          // "jump", "pad_cross"
    std::string label;        // for people
    std::string defaults;     // "Space", "south"
    bool pad = false;
};
// Every keyboard action, then every pad button, in input.ini's order.
const std::vector<ControlRow>& ControlRows();
// The pad button names (south, east...) with labels, for the pad picker; "none" first.
std::vector<std::pair<std::string, std::string>> PadButtonChoices();

class InputConfig {
public:
    static InputConfig FromText(const std::string& text);
    static InputConfig Load(const std::filesystem::path& file);  // missing: the defaults

    std::string Get(const std::string& key) const;  // the value in use (the file's, else the default)
    void Set(const std::string& key, const std::string& value);
    void Reset(const std::string& key);  // back to its default
    void ResetAll();
    bool Changed() const { return m_changed; }
    // Keys bound to more than one keyboard action ("Space" -> {"jump", "confirm"}).
    std::map<std::string, std::vector<std::string>> Clashes() const;

    std::string Text() const;
    // Writes the file; the previous one is kept as input.ini.bak.
    bool Save(const std::filesystem::path& file, std::string* error);

private:
    std::vector<std::string> m_lines;           // the file as it was
    std::map<std::string, std::string> m_values;  // known keys set in the file or here
    bool m_changed = false;
};

// "jump = Space, Up" -> {"Space", "Up"}.
std::vector<std::string> SplitKeys(const std::string& value);

}  // namespace launcher
