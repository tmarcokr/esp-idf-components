---
description: Audit process to validate the technical quality of the code.
---

# 🛡️ Workflow: Technical Verification

This flow should be executed every time a significant implementation is completed to ensure code quality.

## Process Steps

1. **Invoke the Auditor**:
   - Request: *"Execute a technical audit on the [Name] component"*.
   - The agent will activate the `Quality_Auditor` skill and verify against the rules in `.agents/rules/01-project-instructions.md`.

2. **Verification Checklist**:
   - **RAII**: Any manual `new` or `delete`? (If yes -> REJECTED).
   - **Error Handling**: Are `esp_err_t` returns being ignored? (If yes -> REJECTED).
   - **Style**: Are logs in English? Are there redundant inline comments? (If yes -> REJECTED).
   - **Modularity**: Is hardware logic cleanly separated from business logic?

3. **Iterative Correction**:
   - If the Auditor detects failures, they must be corrected immediately before proceeding.

4. **Final Certification**:
   - Only when the code passes all tests will it be marked as ready to be flashed.
