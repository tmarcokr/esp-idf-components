---
description: Create a clean commit following the project's atomic commit policy and quality standards.
---

# 🚀 Workflow: Creating a Commit

Use this workflow to record your progress after completing a specific sub-task or feature.

## Steps to Follow

1. **Pre-Commit Validation**:
   - Before committing, you MUST ensure the code is functional and does not break the project.
   - Run the VS Code task **"Build ESP-IDF"** or execute the command equivalent:
     ```bash
     source ~/esp/esp-idf/export.sh && idf.py build
     ```
   - *Note:* If the project includes unit tests, they should also be executed and passed.

2. **Stage Changes**:
   - Select the files to be included in the commit:
     ```bash
     ga <file_path>
     ```
   - *Note:* `ga` is the oh-my-zsh alias for `git add`.

3. **Format & Commit**:
   - Write a simple, descriptive commit message in a single sentence explaining the work done:
     ```bash
     gc -m "feat: <simple_description>"
     ```
   - *Note:* `gc -m` is the oh-my-zsh alias for `git commit -m`. Use conventional prefixes (feat, fix, docs, chore, etc.).

4. **Verify State**:
   - Check the status to ensure everything is correctly recorded:
     ```bash
     gst
     ```
   - *Note:* `gst` is the oh-my-zsh alias for `git status`.
