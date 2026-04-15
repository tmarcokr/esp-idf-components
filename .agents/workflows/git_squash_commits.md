# 🚀 Workflow: Squash Commits (Interactive Rebase)

Use this workflow to clean up your commit history (e.g., combining multiple "WIP" or "wp" commits into a single cohesive functional commit) before submitting a Pull Request to `main` (or `master`).

## Requirements
- This workflow leverages `oh-my-zsh` with the `git` plugin enabled.

## Steps to Follow

1. **Start Interactive Rebase**:
   - Run the aliased command in your terminal targeting the base branch (in this case, `master` or `main`):
     ```bash
     grbi main
     ```
   - *Note:* `grbi main` is the oh-my-zsh alias for `git rebase -i main` (Interactive Rebase).

2. **Edit the Rebase File**:
   - Your default terminal text editor (e.g., `nano` or `vim`) will open showing a list of your recent branch commits in chronological order.
   - Leave the word `pick` (or `p`) next to the **very first commit at the top** (this is the base commit you want to keep).
   - Change the word `pick` to `fixup` (or `f`) for all subsequent commits below it. `fixup` squashes the commit into the one above it and automatically discards its individual commit message (perfect for hiding "wp" logs).

   *Example snippet:*
   ```text
   pick 9e617f5 feat: implement polyphonic audio engine and AudioSample
   f e90b731 wp
   f 26efb3a wp
   f 216e696 FEATURE: Optimize polyphonic mixer levels
   ```

3. **Save and Close**:
   - Save the file and close the editor (if using `vim`, type `:wq` and press `Enter`).
   - Git will automatically process the rebase, squashing the marked commits into the top one, maintaining a single, clean timeline point.

4. **Force Push**:
   - Because you have rewritten your local commit history, a standard push will fail. You must force push your new unified history to remote:
     ```bash
     gpf!
     ```
   - *Note:* `gpf!` is the oh-my-zsh alias for `git push --force-with-lease` (safer than a raw force push).

## Result
Your remote repository branch is now updated with a clean, linear history containing a single, highly descriptive commit. You are now ready to open the Pull Request.
