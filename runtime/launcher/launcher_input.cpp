// The controls editor's model of input.ini (launcher_input.h).
#include "launcher_input.h"

#include <fstream>
#include <set>
#include <sstream>

#include "input/action_table.h"

namespace launcher {
namespace fs = std::filesystem;
namespace {

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

const ControlRow* Find(const std::string& key) {
    for (const auto& r : ControlRows())
        if (r.key == key) return &r;
    return nullptr;
}

}  // namespace

const std::vector<ControlRow>& ControlRows() {
    static const std::vector<ControlRow> rows = [] {
        std::vector<ControlRow> v;
        for (const auto& a : input::actions::kActionRows) v.push_back({a.name, a.description, a.defaults, false});
        for (const auto& p : input::actions::kPadRows) v.push_back({p.name, p.label, p.defaults, true});
        return v;
    }();
    return rows;
}

std::vector<std::pair<std::string, std::string>> PadButtonChoices() {
    std::vector<std::pair<std::string, std::string>> v{{"none", "nothing (unmapped)"}};
    for (const auto& b : input::actions::kPadButtonNames) v.emplace_back(b.name, b.label);
    return v;
}

std::vector<std::string> SplitKeys(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream in(value);
    std::string k;
    while (std::getline(in, k, ',')) {
        k = Trim(k);
        if (!k.empty()) out.push_back(k);
    }
    return out;
}

InputConfig InputConfig::FromText(const std::string& text) {
    InputConfig c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        c.m_lines.push_back(line);
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(t.substr(0, eq));
        if (Find(key)) c.m_values[key] = Trim(t.substr(eq + 1));  // the last one wins, as in the runtime
    }
    return c;
}

InputConfig InputConfig::Load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return InputConfig{};
    std::ostringstream ss;
    ss << f.rdbuf();
    return FromText(ss.str());
}

std::string InputConfig::Get(const std::string& key) const {
    if (auto it = m_values.find(key); it != m_values.end()) return it->second;
    const ControlRow* r = Find(key);
    return r ? r->defaults : std::string();
}

void InputConfig::Set(const std::string& key, const std::string& value) {
    if (!Find(key) || Get(key) == value) return;
    m_values[key] = value;
    m_changed = true;
}

void InputConfig::Reset(const std::string& key) {
    const ControlRow* r = Find(key);
    if (r) Set(key, r->defaults);
}

void InputConfig::ResetAll() {
    for (const auto& r : ControlRows()) Reset(r.key);
}

std::map<std::string, std::vector<std::string>> InputConfig::Clashes() const {
    std::map<std::string, std::vector<std::string>> uses;
    for (const auto& r : ControlRows()) {
        if (r.pad) continue;
        for (const auto& k : SplitKeys(Get(r.key))) uses[k].push_back(r.key);
    }
    std::map<std::string, std::vector<std::string>> out;
    for (auto& [k, v] : uses)
        if (v.size() > 1) out[k] = v;
    return out;
}

std::string InputConfig::Text() const {
    std::string out;
    std::set<std::string> written;
    if (m_lines.empty()) {
        out = "# Keyboard controls. One action per line: action = key, key, ...\n"
              "# Key names are SDL's (A, Space, Left Shift, Return, Escape, Up, F1, Keypad 8, ...).\n"
              "# Delete this file to get the defaults back.\n\n";
    }
    for (const std::string& line : m_lines) {
        const std::string t = Trim(line);
        const size_t eq = t.find('=');
        if (!t.empty() && t[0] != '#' && eq != std::string::npos) {
            const std::string key = Trim(t.substr(0, eq));
            if (Find(key)) {
                if (written.insert(key).second) out += key + " = " + Get(key) + "\n";
                continue;  // a second line for the same key: dropped (the runtime used the last one)
            }
        }
        out += line + "\n";
    }
    bool padHeader = false;
    for (const auto& r : ControlRows()) {
        if (written.count(r.key)) continue;
        if (r.pad) {
            if (Get(r.key) == r.defaults) continue;  // untouched pad buttons stay unwritten
            if (!padHeader) {
                out += "\n# Pad buttons (pad_<button> = <pad button>, ...; written by the launcher)\n";
                padHeader = true;
            }
            out += r.key + " = " + Get(r.key) + "\n";
        } else {
            out += "# " + r.label + "\n" + r.key + " = " + Get(r.key) + "\n\n";
        }
    }
    return out;
}

bool InputConfig::Save(const fs::path& file, std::string* error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (fs::exists(file, ec)) fs::copy_file(file, file.string() + ".bak", fs::copy_options::overwrite_existing, ec);
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << Text();
        if (!f) {
            if (error) *error = "couldn't write " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        if (error) *error = "couldn't replace input.ini: " + ec.message();
        return false;
    }
    m_changed = false;
    *this = Load(file);
    return true;
}

}  // namespace launcher
