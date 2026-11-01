<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Git guide

This guide collects the practical Git knowledge needed to contribute to OAI in
one place: how to set up commit signing, how to manage and synchronize a
feature branch, how to handle submodules, how to recover from common mistakes,
and how to avoid resolving the same merge conflicts over and over. It is a
how-to companion to the contribution *requirements*, which are
defined in [CONTRIBUTING.md](../CONTRIBUTING.md) (CLA, DCO, verified commits)
and [code-style-contrib.md](./code-style-contrib.md) (workflow, commit, and
review policy).

[[_TOC_]]

## Setting up commit signing

Every commit in a pull request must pass two independent CI checks, described
in [CONTRIBUTING.md](../CONTRIBUTING.md#commit-guidelines):

1. **[Developer Certificate of Origin (DCO)](https://developercertificate.org/)**:
   the commit message carries a `Signed-off-by:` trailer.
2. **[Verified commits](https://docs.github.com/en/authentication/managing-commit-signature-verification/about-commit-signature-verification)**:
   the commit is cryptographically signed with an SSH or GPG key.

These are two different mechanisms: the sign-off is a line of text you add with
`git commit -s`, the signature is created automatically by Git once signing is
configured. You need both.

### Quick setup (SSH signing)

```bash
# 1. Generate a key pair (skip if you already have one)
ssh-keygen -t ed25519 -C "<your email>"

# 2. Configure Git to sign every commit with it
git config --global user.name "<Your Name>"
git config --global user.email "<your email>"
git config --global gpg.format ssh
git config --global user.signingkey ~/.ssh/id_ed25519.pub
git config --global commit.gpgsign true
```

> **NOTE:**
> `--global` writes to `~/.gitconfig` and applies to every repository on the
> machine. When working on a shared server (or with different identities in
> different clones), drop `--global` to store the same settings in the current
> repository's `.git/config` only.

Then print the public key with `cat ~/.ssh/id_ed25519.pub` and paste it into
your GitHub account under *Settings → SSH and GPG keys → New SSH key*, choosing
the key type **Signing Key**.

> **NOTE:**
> Adding an SSH key for repository access does not automatically enable commit
> signing. The key must also be added under GitHub's Signing Keys settings.

For commits to show as *Verified* on GitHub:

- your `git config user.email` must match an email of your GitHub account,
- that email must be [verified in your GitHub account](https://docs.github.com/en/account-and-profile/how-tos/email-preferences/verifying-your-email-address),
- and it must be the email address used for the CLA (see
  [CONTRIBUTING.md](../CONTRIBUTING.md)).

If you prefer GPG over SSH, set `gpg.format` to `openpgp` and `user.signingkey`
to your GPG key ID instead; see the [GitHub documentation on signing
commits](https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-commits)
for the full walkthrough of both methods.

### Signing off your commits (DCO)

The `Signed-off-by:` trailer is added with the `-s`/`--signoff` flag:

```bash
git commit -s                     # new commit
git commit --amend -s --no-edit   # add the trailer to the last commit
```

It must read `Signed-off-by: Full Name <email-for-cla>`. See the
[commit trailers section](./code-style-contrib.md#use-of-git-commit-trailers)
of the contribution guidelines for this and other trailers.

### Verifying signed commits

You can verify that commits are properly signed locally using:

```bash
git log --show-signature
```

GitHub should also display a *Verified* badge next to signed commits once the
signing key has been correctly configured in your account.

For SSH commit signing, local Git verification may require an
`allowed_signers` file. This is only used for local verification in Git and is
not required by GitHub. If you see errors such as:

```text
No principal matched
Can't check signature
error: gpg.ssh.allowedSignersFile needs to be configured
```

create the file, add your signing identity, and enable it in your Git config:

```bash
mkdir -p ~/.config/git
echo "user@example.com ssh-ed25519 AAAACexamplekeystringhere" > ~/.config/git/allowed_signers
git config gpg.ssh.allowedSignersFile ~/.config/git/allowed_signers
```

> **NOTE:**
> This is only for local Git signature verification and does not affect GitHub,
> or remote repository behavior.

## Managing your own branch

The general development branch, and the target of every contribution, is
`develop`; see [GET_SOURCES.md](./GET_SOURCES.md) for the branch and tag model
(weekly `YYYY.wXX` tags, `vX.Y` releases). The rules for what a branch should
look like — linear history, small self-contained logical commits, commit
messages that explain *why* — are policy and live in
[code-style-contrib.md](./code-style-contrib.md#workflow).

Before starting to work, please make sure to branch off the latest `develop`
branch. Make commits as appropriate.

```bash
git fetch origin
git checkout develop
git checkout -b my-new-feature # name as appropriate
git add -p                     # add changes for change set 1, use `-p` to review what to include
git commit -s                  # in the editor, describe your changes
git add -p                     # add changes for change set 2
git commit -s                  # in the editor, describe your changes
```

Recent Git versions also offer `git switch` as a clearer alternative to
`git checkout` for branch operations: `git switch develop` changes branch,
`git switch -c my-new-feature` creates one.

Commit messages should take multiple lines; after the initial title, a blank
line should follow. Read the `DISCUSSION` section in `man git commit` for more
information. For documentation-only commits, prefix the title with `docs:`
(see [doc_best_practices.md](./doc_best_practices.md)).

Code must be formatted with clang-format; an optional pre-commit hook can
check this automatically at every commit — see
[clang-format.md](./clang-format.md) for its installation and how to combine
it with `git add -p`/`git stash -p`.

If your development takes longer, make sure to synchronize regularly with
`origin/develop` using `git rebase`:

```bash
git fetch origin
git rebase -i origin/develop
```

If you do logical changes, you should not have to resolve the same conflicts
over and over again. If the same conflicts do keep reappearing, e.g., when
maintaining a long-lived fork, consider enabling
[`git rerere`](#reusing-conflict-resolutions-with-git-rerere). Note that if
you jumped over multiple develop tags, you can also rebase in intermediate
steps, in case you fear the differences might be too big.

```bash
git rebase -i 2023.w38
git rebase -i 2023.w41
git rebase -i develop
```

Once you rebased, push the changes to the remote:

```bash
git push origin my-new-feature --force-with-lease # force with lease lets you only overwrite what you also have locally in origin/my-new-feature
```

### Fixing up earlier commits

The [workflow policy](./code-style-contrib.md#workflow) asks for a history
without "clean up" commits: when review or testing reveals a problem in an
earlier commit of your branch, fold the fix into that commit instead of
appending a `Fix bug` commit on top. Git automates this with fixup commits and
`--autosquash`:

```bash
git add -p                                 # stage the fix
git commit --fixup=<commit>                # creates a commit titled "fixup! <original title>"
git rebase -i --autosquash origin/develop  # moves it after <commit> and squashes the two
```

During the `--autosquash` rebase, Git pre-arranges the todo list so each
`fixup!` commit is squashed into the commit it references; you normally just
accept it. The result is the same clean history as if the fix had been part of
the original commit.

A handy variant is `git commit --fixup=amend:<commit>`, which folds in the fix
and also rewrites the commit message: during the `--autosquash` rebase the
editor opens pre-filled with the original message, ready to be edited into the
new one.

## Working with submodules

Parts of the tree are Git submodules. After cloning, and after every branch
switch or pull, make sure they match the superproject:

```bash
git submodule update --init --recursive
```

A recurring review problem is the *unintended submodule pointer update*: a
submodule whose checked-out commit differs from what the superproject records
shows up in `git status` as `modified: <path> (new commits)`, and a broad
`git add .`, `git add -A`, or `git commit -a` silently records the new pointer
in your commit. To avoid it:

- review `git status` before committing and stage files explicitly (e.g. with
  `git add -p`) rather than adding everything;
- if a pointer change was staged by accident, unstage it with
  `git restore --staged <path>` and realign the submodule with
  `git submodule update --init <path>`.

Only commit a submodule pointer change when updating that submodule is the
purpose of the commit, and say so in the commit message.

## Recovering from mistakes

To unstage a file that was added by accident (the changes stay in your working
tree), or to throw away local changes to a file:

```bash
git restore --staged <file>   # unstage; keeps the modifications
git restore <file>            # discard unstaged modifications - cannot be undone
```

`git reset` moves the current branch to another commit and differs in what it
does to your files:

```bash
git reset --soft HEAD~1   # undo the last commit, keep its changes staged (e.g. to re-split it)
git reset --hard <commit> # make branch, index and working tree identical to <commit>
```

> **Warning:** `git reset --hard` discards all uncommitted changes; there is no
> way to recover them.

Committed work is much harder to lose than it seems: `git reflog` records every
position of `HEAD` (commits, rebases, resets, checkouts) for a retention period
of at least 30 days, even for commits no branch points to anymore. If a rebase
or reset went wrong, find the last good state and reset back to it:

```bash
git reflog                    # e.g.: e75076172 HEAD@{5}: commit: doc: add git rerere guide
git reset --hard 'HEAD@{5}'   # return the branch to that state
```

## Reusing conflict resolutions with git rerere

The `develop` branch is updated roughly once a week. Feature branches that live
for more than a few days therefore have to be re-synced with `develop`
repeatedly, and the same merge conflicts tend to reappear at every sync - often
in the same scheduler, PHY, or RRC files that several contributors touch at
once. Resolving the identical conflict by hand every week is error-prone and
wastes time.

Git ships a built-in feature for exactly this situation: `rerere`, short for
**reuse recorded resolution**. Once enabled, Git remembers how you resolved a
given conflict and replays that resolution automatically the next time the same
conflict appears.

This section explains how to enable and use it. It is a local developer
convenience: nothing about it changes the repository, the history you push, or
the contribution workflow.

### What it does

When a conflict occurs, `rerere` records the conflicted hunk (the *preimage*).
After you resolve it, `rerere` records your resolution (the *postimage*), keyed
by a hash of the preimage. The next time a conflict with the same preimage shows
up - in a later rebase, a later merge, or even another branch - Git reapplies
your recorded resolution instead of presenting the conflict again.

The data lives in `.git/rr-cache/` inside your local clone. It is never part of
any commit and is never pushed.

### Enabling it

Enable it once, globally, so it applies to every repository on your machine:

```bash
git config --global rerere.enabled true
git config --global rerere.autoupdate true
```

`rerere.autoupdate` stages a replayed resolution automatically. Without it, the
resolution is still written into your working tree, but you have to `git add`
the file yourself.

### Typical flow

The first time you hit a conflict after enabling `rerere`, resolve it exactly as
you always have:

```bash
# during a rebase or a merge that conflicts
git status                 # rerere reports which paths it is recording
# edit the conflicted files, remove the markers
git add <resolved-files>
git rebase --continue      # or: git commit, for a merge
```

That resolution is now recorded. The next time the same conflict appears, Git
resolves it for you. With `autoupdate` on, the file is already staged and you can
go straight to:

```bash
git rebase --continue      # or git commit
```

Always review the replayed result before continuing - see *Caveats* below.

### Inspecting and undoing recorded resolutions

```bash
git rerere status          # paths with a recorded preimage in the current operation
git rerere diff            # the resolution rerere is applying
git rerere forget <path>   # discard a recorded resolution (e.g. a wrong one)
```

`git rerere forget` is the escape hatch when you recorded a bad resolution: it
drops the cached entry for that path so the next conflict is presented fresh.

### Seeding from existing history

If your branch already contains **merge commits** whose conflicts you resolved
before enabling `rerere`, you can backfill the cache so those resolutions are
available immediately. Git ships a helper for this in `contrib/`:

```bash
sh /path/to/git/contrib/rerere-train.sh origin/develop..HEAD
```

It replays the merge commits in the given range, reconstructs each conflict, and
records the resolution found in the merge commit.

> **Note:** this only works for resolutions captured in merge commits. A purely
> linear (rebased) history has no merge commits to learn from, so there is
> nothing to backfill - `rerere` will simply start recording from your next
> conflict onward.

### Sharing the cache (optional)

The cache is local. If you work across several machines, or want a team to share
resolutions for the same recurring conflicts, copy the directory:

```bash
rsync -a ~/work/oai-A/.git/rr-cache/ ~/work/oai-B/.git/rr-cache/
```

There is no built-in push/pull for the cache; treat it as an ordinary directory
to sync.

### Caveats and good practice

- `rerere` matches on the **exact** conflicting text. If `develop` changed the
  lines surrounding your change, the preimage differs and the conflict is
  presented as new. This is expected - the resolution is still recorded for the
  next identical occurrence.
- A replayed resolution is only as correct as the original. When the code around
  a conflict has evolved, an old resolution can apply cleanly yet be wrong.
  **Review every replayed resolution and build/test before continuing.**
- `rerere` reduces repeated manual work; it does not change which branch
  strategy you use. It helps both when rebasing onto `develop` and when merging
  `develop` into a feature branch. Remember that branches intended for
  contribution must have a linear history without merge commits (see the
  [workflow policy](./code-style-contrib.md#workflow)); a fork can of course
  carry merge commits if that is convenient for its development.

## See also

- [CONTRIBUTING.md](../CONTRIBUTING.md) - CLA, DCO, and licensing requirements.
- [code-style-contrib.md](./code-style-contrib.md) - workflow, commit, and
  review policy, including commit trailers.
- [GET_SOURCES.md](./GET_SOURCES.md) - branches, tags, and how to obtain the
  sources.
- [clang-format.md](./clang-format.md) - code formatting and its Git
  integration (pre-commit hook).
- The [Git Book](https://git-scm.com/book/en/v2) and the
  [`git rerere` manual](https://git-scm.com/docs/git-rerere)
