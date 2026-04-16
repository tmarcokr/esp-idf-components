---
description: Generate a Pull Request following the project standard.
---

# 🚀 Workflow: Submission of Pull Request

Use this workflow when you have completed a feature, bug fix, or documentation update.

## Steps to Follow

1. **Information Gathering**:
   - Summarize the work done.
   - Identify the type of change (FEATURE, BUG, or DOC).

2. **Template Generation**:
   - Write the PR title using the mandatory prefix.
   - Fill the **Description**, **Implementation**, and **Obs** sections strictly in **English** using H1 headers (`# `).

3. **History Cleanup (Rebase & Squash)**:
   - Ensure your branch is updated with the latest code from `main` following the **`.agents/workflows/git_rebase_main.md`** workflow.
   - Squash your commits into a single cohesive functional commit following the **`.agents/workflows/git_squash_commits.md`** workflow.

4. **Auditor Review**:
   - Request a quality check from the **Quality Auditor Skill**.
   - Ensure RAII, C++20 standards, and commenting policies are met.

5. **Final Output**:
   - Present the formatted Markdown code (Title and Body) to the user for confirmation.

6. **Publish & Create PR**:
   - Push your finalized branch (using force push since history was rewritten):
     ```bash
     gpf!
     ```
   - *Note:* `gpf!` is the oh-my-zsh alias for `git push --force-with-lease`.
   - Create the Pull Request using the GitHub CLI:
     ```bash
     gh pr create --title "<TITLE>" --body "<MARKDOWN_CONTENT>"
     ```
   - **Important**: You MUST show the Title and Body to the user and ask for confirmation before executing `gh pr create`.
   - **Fallback**: If `gh` fails, provide the Markdown and manual URL.

---

## 📝 Reference Example (Generic Template)

```markdown
# Description
The initialization for the I2C sensor was procedural and prone to resource leaks on failure. This update encapsulates the driver in a C++ class to ensure safe cleanup via RAII.

# Implementation
- I2C Sensor Driver:
  - Created `Hardware::Sensors::GenericI2C` component.
  - Implemented RAII logic in constructor/destructor to manage GPIO and I2C bus life cycles.
- Integration:
  - Migrated legacy `main.cpp` calls to the new class interface.

# Obs
- Verified against the target MCU Datasheet for I2C pull-up requirements.
- Technical Audit: Passed (No raw pointers, Error checks included, Doxygen compliant).
```
