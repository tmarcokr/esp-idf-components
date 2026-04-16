---
description: Add or update the ESP-IDF template (.agents, .vscode, and GEMINI.md) from the upstream repository.
---

# 🚀 Workflow: Template Sync / Update

Use this workflow to add or update the `esp-idf-template` inside another project.

## Important Constraints
- **Do not use `git checkout`** for bringing files from the template (e.g., `git checkout template/main -- .agents`), as it will forcefully overwrite local changes without warning.
- **Use `git merge`** to safely bring in changes from the template. This ensures Git uses its merge engine to combine changes and alert about conflicts, preserving local customizations.
- Focus updates specifically on the `.agents/`, `.vscode/` directories, and the root `GEMINI.md`.

## Process Steps

1. **Information Gathering**:
   - Check if the `template` remote already exists using `git remote -v`.
   - If not, add the template repository as a remote:
     ```bash
     git remote add template https://github.com/tmarcokr/esp-idf-template.git
     ```

2. **Fetching Updates**:
   - Fetch the latest changes from the template repository:
     ```bash
     git fetch template
     ```

3. **Merging Changes (Safe approach for specific folders)**:
   - To merge changes safely while only affecting `.agents/`, `.vscode/`, and `GEMINI.md`, execute a merge without committing automatically:
     ```bash
     git merge template/main --no-commit --no-ff --allow-unrelated-histories
     ```
   - *Note:* If the merge process results in conflicts inside the target files or folders, they must be resolved manually or using standard Git conflict resolution tools.

4. **Filtering and Atomic Cleanup**:
   - Because a merge of unrelated histories downloads the entire template tree, you must isolate the desired changes and purge the rest:
     ```bash
     # 1. Unstage everything (leaves the tree "dirty" from the merge)
     git reset HEAD
     
     # 2. Add ONLY the target directories and the context file
     git add .agents/ .vscode/ GEMINI.md
     
     # 3. Restore tracked local files that were modified and are not wanted (e.g., README.md)
     git restore .
     
     # 4. Remove all new template files and directories that are now untracked
     git clean -fd
     ```
   - *Warning:* Ensure the workspace was clean before starting, as `git clean -fd` will remove any other untracked files you might have had.

5. **Finalizing**:
   - Review the staged changes using `git status` and `git diff --staged`.
   - If `GEMINI.md` resulted in a conflict during merge, ensure you have combined the template's foundational rules with your project-specific context.
   - If everything looks correct, commit the merge:
     ```bash
     git commit -m "chore: sync .agents, .vscode and GEMINI.md from esp-idf-template"
     ```
