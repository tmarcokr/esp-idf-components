---
description: Create a new Git branch following the project nomenclature.
---

# 🚀 Workflow: Branch Creation

Use this workflow whenever you start a new task.

## Naming Rules
- Mandatory prefix: **`feature/`**
- Use only lowercase and underscores.
- Keep descriptions short (1-3 words).

## Process Steps

1. **Information Gathering**:
   - Determine the core goal of the task (e.g., "Implementing I2C Sensor").

2. **Branch Formatting**:
   - Generate the branch name: `feature/<short_description>`
   - Example: `feature/i2c_sensor`

3. **Execution**:
   - Checkout to a new branch from `main`:
     ```bash
     gcb feature/<your_description>
     ```
   - *Note:* `gcb` is the oh-my-zsh alias for `git checkout -b` (Create and switch to a new branch).
