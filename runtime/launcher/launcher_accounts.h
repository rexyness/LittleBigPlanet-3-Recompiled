// The player's online accounts on this PC: user\config\online_account*.txt. The game only ever
// reads online_account.txt (the active one); the others are kept beside it. Switching renames
// files (the active one to online_account_<name>.txt, the chosen one to online_account.txt), so no
// account file is ever overwritten or deleted. Passwords stay in these files and nowhere else.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "launcher_account.h"

namespace launcher {

struct AccountEntry {
    std::filesystem::path file;
    bool active = false;  // online_account.txt
    AccountFile data;
};
// Every online_account*.txt in the folder (not .bak or .tmp), the active one first, then by name.
std::vector<AccountEntry> ListAccounts(const std::filesystem::path& config_dir);

// Makes `chosen` (one of the folder's account files) the active one. The current active file is
// renamed to online_account_<its name>.txt first (with _2, _3... if that is taken).
bool SetActiveAccount(const std::filesystem::path& config_dir, const std::filesystem::path& chosen, std::string* error);

// Changes the saved password (every password line; one is added if there is none). The file's
// previous text is kept as <file>.bak. Only this PC's copy changes: the servers keep theirs.
bool SetAccountPassword(const std::filesystem::path& file, const std::string& password, std::string* error);

// Where a new account's file goes: online_account.txt if there is no active account yet, else
// online_account_<name>.txt (with _2, _3... if taken).
std::filesystem::path NewAccountFile(const std::filesystem::path& config_dir, const std::string& name);
// Writes a new account file ("name=", "password=", "server=", "email=" lines). Never overwrites.
bool WriteAccountFile(const std::filesystem::path& file, const AccountFile& a, std::string* error);

// The e-mail the server's website login wants: the file's email= line, else the placeholder the
// account tools register with (<name in lower case>@lbppc.invalid; the servers send no mail).
std::string AccountEmail(const AccountFile& a);
// A password this launcher accepts: 8-64 characters, no spaces at either end, no line breaks.
bool ValidAccountPassword(const std::string& password);

}  // namespace launcher
