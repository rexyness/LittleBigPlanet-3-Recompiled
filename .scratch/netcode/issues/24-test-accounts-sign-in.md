# Can the three test accounts sign in and find each other?

Type: task
Status: resolved
Claimed by: work-through session 2, 2026-11-16
Blocked by: none
Map: [Online co-op netcode](../map.md)

## Question

[What does a third player do to a session?](18-third-player.md) couldn't run: the three instances came up signed out, and Kistopawa3 has no friends on the online server. Both are the user's to fix: the accounts and their password are theirs, and a session's search for the password was refused by the permission rules on 2026-11-16.

A checklist for the human:

- **Give each instance its sign-in.** Put `online_account.txt` (lines `name=...` and `password=...`) into `D:\Recomps\lbp3-wt\coop\A\user\config`, `B\user\config` and `C\user\config`, for Kistopawa, Kistopawa2 and Kistopawa3. `D:\Recomps\lbp3-wt\coop` is a worktree of the runtime repository, but `user/` is in its `.gitignore`, so the files are not picked up by git. Or say that sessions may read the password from where it is kept, and where that is.
- **Make Kistopawa3 a friend of Kistopawa** (request and accept) . Leaving it not a friend of Kistopawa2 keeps the unfriended pair available for the run, though that may not cut the pair (see the corrections in the third-player ticket). `tools/shadnet_friend.py` in the port's map does it once the password is at hand; so does the game's own friend screen.

Done when `tools/rig3/launch3.sh` prints "signed in as" for all three, and Kistopawa's friend list shows Kistopawa2 and Kistopawa3. The answer records where the sign-in lives, so later sessions don't look for it.

## Answer

Resolved 2026-11-16. The user handed the checklist back ("you do that"), so the session did it.

- **Where the sign-in lives:** `D:\Recomps\lbp3-recompiled\user\config\online_account_kistopawa.txt`, `online_account_kistopawa2.txt` and `online_account_kistopawa3.txt` (the user's own `online_account.txt` there is another account; leave it alone). Each was copied to its instance as `D:\Recomps\lbp3-wt\coop\A|B|C\user\config\online_account.txt`. `user/` is git-ignored in that worktree. Don't print these files; read them with the password line redacted.
- **Friends:** Kistopawa3 and Kistopawa are friends (request and accept through `tools/shadnet_friend.py` in the port's map). After the first three-player run, Kistopawa2 and Kistopawa3 were made friends too, because without that the two guests can't find each other (see [What does a third player do to a session?](18-third-player.md)). All three are now friends of each other. To get an unfriended pair back, a friend has to be removed by hand; the tool only adds.
- **Checked:** `tools/rig3/launch3.sh` prints "signed in as" for all three, with 2 friends each.
