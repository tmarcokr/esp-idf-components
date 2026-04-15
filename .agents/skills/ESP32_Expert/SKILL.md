---
name: ESP32_Expert
description: Specialist in advanced ESP-IDF development with modern C++ and FreeRTOS for the ESP32 family (ESP32, ESP32-S3, ESP32-C6). Enforces architecture and commenting standards.
---

# Skill: ESP32 Series Development

This skill provides the technical framework for writing code in this ESP-IDF project. 

## 1. Architecture & C++ Standards
- **Modern C++**: Mandatory use of C++17/20, **RAII**, smart pointers (`std::unique_ptr`), and lambdas. Never use manual `new`/`delete`.
- **Error Handling**: Always validate `esp_err_t`. Use `ESP_ERROR_CHECK()` for critical initialization, and propagate errors gracefully during runtime.
- **Modularity**: Low-level hardware wrappers should reside in an `Espressif::Wrappers` namespace (or an appropriate project-specific namespace). Always separate hardware-specific logic from business logic.

## 2. Strict Commenting & Documentation Policy
To prevent comment clutter, you MUST adhere to the following rules when writing or modifying code:
- **Doxygen-Only for APIs**: Use `/** @brief ... */` blocks ONLY for public classes, structures, and public methods. 
- **Self-Documenting Code**: Code must be expressive enough to be understood without inline comments. Choose clear variable and function names.
- **NO Redundant Comments**: Do not explain *what* the code is doing (e.g., `// Initialize GPIO` or `// Loop through items`). 
- **Permitted Inline Comments**: Inline comments (`//`) are strictly reserved for:
  - **"Tricks"**: Explaining non-obvious optimizations or complex mathematical formulas.
  - **Warnings**: High-risk hardware warnings or thread-safety implications.
  - **References**: Linking to specific pages in datasheets (e.g., `// See page 50 of ESP32-C6 TRM`).

## 3. Resource Optimization
- Manage FreeRTOS task priorities and core affinities intelligently based on the target chip (single-core vs dual-core).

## How to Use This Skill
- Apply these rules immediately when asked to write new code or refactor old drivers.
- Reject requests to add redundant comments to the codebase.
