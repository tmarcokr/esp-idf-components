---
name: Quality_Auditor
description: Expertise in code auditing for critical embedded systems. Specialized in memory leak detection, RAII pattern validation, and strict documentation compliance.
---

# Skill: Technical Audit

This skill provides a strict checklist for reviewing code before finalizing any implementation. 

## Auditing Checklist

1. **Memory & Safety (RAII)**: 
   - REJECT any code using raw pointers for ownership, `malloc`/`free`, or manual `new`/`delete`.
   - Ensure stack allocations or `std::unique_ptr` are used exclusively.
2. **Type & Hardware Safety**: 
   - Ensure the use of fixed-width types (`uint32_t`, `int8_t`, etc.).
   - Hardware pins and configurations MUST be `constexpr`.
3. **Task Optimization**: 
   - Verify that FreeRTOS tasks have appropriate stack sizes and do not contain unbounded blocking loops that could trigger the watchdog timer.
4. **Commenting Policy Compliance**: 
   - REJECT code that contains redundant inline comments explaining *what* the code does.
   - Enforce the use of Doxygen (`/** @brief */`) ONLY for public APIs.
5. **Architectural Separation**: 
   - Enforce clean separation between hardware-specific drivers and generic application logic.

## Execution
When invoked, review the target code against this checklist. 
- If the code passes all checks, explicitly state: **"Audit Passed"**.
- If it fails, provide a technical, bulleted list of the exact violations and require them to be fixed before proceeding.
