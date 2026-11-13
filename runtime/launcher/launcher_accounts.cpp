// The player's online account files (launcher_accounts.h).
#include "launcher_accounts.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace launcher {
namespace fs = std::filesystem;
namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// "Kistopawa" -> "kistopawa"; anything but letters, digits, - and _ dropped.
std::string FilePart(const std::string& name) {
    std::string out;
    for (char c : Lower(name))
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') out.push_back(c);
    return out.empty() ? "account" : out;
}

fs::path FreeName(const fs::path& dir, const std::string& base) {
    std::error_code ec;
    fs::path p = dir / ("online_account_" + base + ".txt");
    for (int i = 2; fs::exists(p, ec) && i < 1000; ++i) p = dir / ("online_account_" + base + "_" + std::to_string(i) + ".txt");
    return p;
}

bool IsPasswordKey(const std::string& key) { return key == "password" || key == "pass" || key == "shadnet_password"; }

}  // namespace

std::vector<AccountEntry> ListAccounts(const fs::path& dir) {
    std::vector<AccountEntry> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string name = Lower(e.path().filename().string());
        if (name.rfind("online_account", 0) != 0 || e.path().extension() != ".txt") continue;
        AccountEntry a;
        a.file = e.path();
        a.active = name == "online_account.txt";
        a.data = ParseAccountFile(Slurp(e.path()));
        out.push_back(std::move(a));
    }
    std::sort(out.begin(), out.end(), [](const AccountEntry& a, const AccountEntry& b) {
        if (a.active != b.active) return a.active;
        return Lower(a.data.name) < Lower(b.data.name);
    });
    return out;
}

bool SetActiveAccount(const fs::path& dir, const fs::path& chosen, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    std::error_code ec;
    const fs::path active = dir / "online_account.txt";
    if (!fs::exists(chosen, ec)) return fail("that account file is gone");
    if (!fs::equivalent(chosen.parent_path(), dir, ec)) return fail("that file isn't in the account folder");
    if (fs::exists(active, ec) && fs::equivalent(active, chosen, ec)) return true;  // already active
    fs::path moved;
    if (fs::exists(active, ec)) {
        const AccountFile cur = ParseAccountFile(Slurp(active));
        moved = FreeName(dir, FilePart(cur.name.empty() ? "previous" : cur.name));
        fs::rename(active, moved, ec);
        if (ec) return fail("couldn't set the current account aside: " + ec.message());
    }
    fs::rename(chosen, active, ec);
    if (ec) {
        std::error_code ec2;
        if (!moved.empty()) fs::rename(moved, active, ec2);  // put it back
        return fail("couldn't make it the active account: " + ec.message());
    }
    return true;
}

bool ValidAccountPassword(const std::string& p) {
    if (p.size() < 8 || p.size() > 64) return false;
    if (std::isspace(static_cast<unsigned char>(p.front())) || std::isspace(static_cast<unsigned char>(p.back()))) return false;
    for (char c : p)
        if (c == '\n' || c == '\r' || c == '\0') return false;
    return true;
}

bool SetAccountPassword(const fs::path& file, const std::string& password, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    if (!ValidAccountPassword(password)) return fail("a password is 8-64 characters, without spaces at either end");
    std::error_code ec;
    if (!fs::exists(file, ec)) return fail("the account file is gone");
    const std::string old = Slurp(file);
    std::string out;
    bool replaced = false;
    std::istringstream in(old);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sep = line.find('=');
        if (sep == std::string::npos) sep = line.find(':');
        std::string key = sep == std::string::npos ? std::string() : Lower(line.substr(0, sep));
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());
        if (IsPasswordKey(key)) {
            out += key + "=" + password + "\n";
            replaced = true;
        } else {
            out += line + "\n";
        }
    }
    if (!replaced) out += "password=" + password + "\n";
    const fs::path tmp = file.string() + ".tmp", bak = file.string() + ".bak";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << out;
        if (!f) return fail("couldn't write " + tmp.string());
    }
    fs::copy_file(file, bak, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return fail("couldn't keep the old file as .bak");
    }
    fs::rename(tmp, file, ec);
    if (ec) return fail("couldn't replace the account file: " + ec.message());
    return true;
}

fs::path NewAccountFile(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    const fs::path active = dir / "online_account.txt";
    if (!fs::exists(active, ec)) return active;
    return FreeName(dir, FilePart(name));
}

bool WriteAccountFile(const fs::path& file, const AccountFile& a, std::string* error) {
    std::error_code ec;
    if (fs::exists(file, ec)) {
        if (error) *error = file.filename().string() + " exists already; not overwriting an account file";
        return false;
    }
    fs::create_directories(file.parent_path(), ec);
    std::ofstream f(file, std::ios::binary);
    f << "name=" << a.name << "\npassword=" << a.password << "\n";
    if (!a.server.empty()) f << "server=" << a.server << "\n";
    if (!a.email.empty()) f << "email=" << a.email << "\n";
    if (!f) {
        if (error) *error = "couldn't write " + file.string();
        return false;
    }
    return true;
}

std::string AccountEmail(const AccountFile& a) {
    if (!a.email.empty()) return a.email;
    return Lower(a.name) + "@lbppc.invalid";
}

}  // namespace launcher
