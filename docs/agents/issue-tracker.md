# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues on **`rexyness/LittleBigPlanet-3-Recompiled`** (the fork). Use the `gh` CLI for all operations.

**Always pass `-R rexyness/LittleBigPlanet-3-Recompiled`.** In the main clone, `origin` is PurpleGitty's upstream and `fork` is ours, and `gh` has no default repo set, so a bare `gh issue ...` can land on the wrong repo. The commands below include it.

**The repo is public.** Issues, comments and pushed files are visible to anyone. Never put sign-ins (`user\config\online_account.txt`, passwords, tokens), real players' IP addresses, or game files and dumps (shader dumps, save data, long eboot disassembly) into an issue or a commit. Short addresses and excerpts in prose are fine.

## Conventions

- **Create an issue**: `gh issue create -R rexyness/LittleBigPlanet-3-Recompiled --title "..." --body "..."`. Use `--body-file` (or a heredoc) for multi-line bodies.
- **Read an issue**: `gh issue view <number> -R rexyness/LittleBigPlanet-3-Recompiled --json number,title,body,labels,comments`.
- **List issues**: `gh issue list -R rexyness/LittleBigPlanet-3-Recompiled --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Make an issue a sub-issue of a parent**: `gh issue create -R rexyness/LittleBigPlanet-3-Recompiled --parent <parent> ...`, or `gh issue edit <parent> -R rexyness/LittleBigPlanet-3-Recompiled --add-sub-issue <child>` afterwards.
- **Comment on an issue**: `gh issue comment <number> -R rexyness/LittleBigPlanet-3-Recompiled --body "..."`
- **Apply / remove labels**: `gh issue edit <number> -R rexyness/LittleBigPlanet-3-Recompiled --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> -R rexyness/LittleBigPlanet-3-Recompiled --comment "..."`
- **Pull requests**, when one is wanted: open them on the fork, `gh pr create -R rexyness/LittleBigPlanet-3-Recompiled --base main`, never against PurpleGitty's upstream unless the user asks. The user keeps the fork to `main`: delete a branch once it is merged.

## The old local tracker (archive)

Until 2026-11-19 tickets lived as markdown in a separate private repo (`lbp3-pc`). Its `.scratch/`, `docs/research/`, `docs/adr/` and `GLOSSARY.md` were moved into this repo at the same paths.

- `.scratch/<effort>/map.md` and `.scratch/<effort>/issues/NN-<slug>.md` are **history**. Tickets resolved before the move stay there as the record. Tickets that were still open were moved to GitHub issues: their file starts with a `> **Moved to GitHub:** ... (#N)` line and a `Status: moved to #N` line. Work on the issue, not the file.
- Don't create new ticket files under `.scratch/`. New tickets are GitHub issues.
- `.scratch/` still holds live working material: specs, notes, test rigs and tools (for example `.scratch/netcode/tools/rig3/`, `.scratch/lbp3-port/tools/`). Edit those in place.
- Research findings go to `docs/research/<name>.md` in this repo, with a `Findings:` link in the ticket's resolution comment.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> -R rexyness/LittleBigPlanet-3-Recompiled --comments` and `gh pr diff <number> -R rexyness/LittleBigPlanet-3-Recompiled` for the diff.
- **List external PRs for triage**: `gh api --paginate 'repos/rexyness/LittleBigPlanet-3-Recompiled/pulls?state=open' --jq '.[] | select(.author_association | IN("OWNER","MEMBER","COLLABORATOR") | not) | {number, title, author: .user.login, author_association, labels: [.labels[].name]}'`.
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`, each with `-R rexyness/LittleBigPlanet-3-Recompiled`.

GitHub shares one number space across issues and PRs, so a bare `#42` may be either: resolve with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue on `rexyness/LittleBigPlanet-3-Recompiled`.

## When a skill says "fetch the relevant ticket"

Read it as in **Read an issue** above. A path under `.scratch/` is an archived ticket: read the file, and if it carries a `Moved to GitHub` line, read that issue instead.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a single issue with **child** issues as tickets.

- **Map**: a single issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create -R rexyness/LittleBigPlanet-3-Recompiled --label wayfinder:map`. The online co-op netcode map is the one moved from `.scratch/netcode/map.md`; the LBP3 native port map (`.scratch/lbp3-port/map.md`) was already done and stays archived.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (see **Make an issue a sub-issue of a parent**). Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's **native issue dependencies**. Add an edge with `gh issue edit <child> -R rexyness/LittleBigPlanet-3-Recompiled --add-blocked-by <blocker>` (or `--blocked-by <n>,<n>` on `gh issue create`). GitHub reports `issue_dependencies_summary.blocked_by` (open blockers only, the live gate). A ticket is unblocked when every blocker is closed. Blockers that were resolved before the move are listed in the ticket body as links to their archived files.
- **Frontier query**: list the map's open children (`gh api repos/rexyness/LittleBigPlanet-3-Recompiled/issues/<map>/sub_issues`), drop any with an open blocker (`issue_dependencies_summary.blocked_by > 0`) or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> -R rexyness/LittleBigPlanet-3-Recompiled --add-assignee @me`, the session's first write.
- **Resolve**: `gh issue comment <n> -R rexyness/LittleBigPlanet-3-Recompiled --body "<answer>"`, then `gh issue close <n> -R rexyness/LittleBigPlanet-3-Recompiled`, then append a context pointer (gist + link) to the map issue's Decisions-so-far (`gh issue edit <map> --body-file ...`).
