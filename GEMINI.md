# Project Context & Bootstrapper

## ⚠️ MANDATORY INITIALIZATION
**Stop.** Before performing any analysis or action, you **MUST** execute `read_file` on:
`.agents/rules/01-project-instructions.md`

This is a foundational requirement of the ecosystem template. That file contains your mandatory identity, communication protocols (English-only technical output), and the logic for autonomous skill activation. Failure to load these rules will result in an incorrect execution state.

---

## 🏗️ Project-Specific Overrides
*Add local context below this line without modifying the mandate above.*

- **Target**: This project is a collection of reusable ESP32 components and examples.
- **Root Directory**: Do not run `idf.py` here; always use the `examples/` subdirectories.
