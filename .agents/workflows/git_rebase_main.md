---
description: Rebase the current branch with main to keep history clean and updated.
---

# 🚀 Workflow: Rebase with Main

Use this workflow to synchronize your feature branch with the latest changes from `main` without creating merge commits.

## Requirements
- This workflow leverages `oh-my-zsh` with the `git` plugin enabled.

## Steps to Follow

1. **Update Local Main**:
   - Switch to the `main` branch and pull the latest changes:
     ```bash
     gcm && gl
     ```
   - *Note:* `gcm` is the oh-my-zsh alias for `git checkout main`, and `gl` is for `git pull`.

2. **Rebase Feature Branch**:
   - Switch back to your feature branch and perform the rebase:
     ```bash
     gcb <your_branch>
     grb main
     ```
   - *Note:* `gcb` is the oh-my-zsh alias for `git checkout` (or `git checkout -b` if creating), and `grb main` is the alias for `git rebase main`.

3. **Resolve Conflicts (If Any)**:
   - If Git reports conflicts, **stop and ask the user** to resolve them manually in their editor.
   - After the user confirms resolution, stage the files and continue:
     ```bash
     git add .
     grbc
     ```
   - *Note:* `grbc` is the oh-my-zsh alias for `git rebase --continue`.

4. **Update Remote**:
   - Since history has diverged, you must force push:
     ```bash
     gpf!
     ```
   - *Note:* `gpf!` is the oh-my-zsh alias for `git push --force-with-lease`.

## Why Rebase?
- Maintains a **linear history** (no "Merge branch 'main' into..." commits).
- Simplifies the final squash before the Pull Request.
- Ensures you are testing your changes against the most recent code.
