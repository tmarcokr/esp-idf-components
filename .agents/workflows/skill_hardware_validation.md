---
description: Hardware validation process before integrating new peripherals or assigning pins.
---

# 🔌 Workflow: Hardware Validation

Execute this workflow **BEFORE** assigning any GPIO pins, designing a custom PCB, or integrating a new hardware peripheral.

## Process Steps

1. **Information Gathering**:
   - Identify the target ESP32 chip (e.g., ESP32, ESP32-S3, ESP32-C6).
   - Identify the peripheral to be connected (e.g., I2C Sensor, SD Card, Motor Driver).

2. **Invoke the Specialist**:
   - Request: *"Perform a Hardware Validation for connecting [Peripheral] to [Target Chip]"*.
   - The agent will activate the `Hardware_Specialist` skill and consult the specific datasheet in `.agents/docs/`.

3. **Validation Checks**:
   - **Pin Conflicts**: Are the proposed pins safe? (Avoid strapping pins, internal JTAG, or input-only pins).
   - **Power Delivery**: Does the peripheral require high peak current (e.g., SD Card)? Are decoupling capacitors necessary?
   - **Signal Integrity**: Are pull-up resistors required (e.g., for I2C)?

4. **Result & Action**:
   - If the validation passes, proceed to define the `constexpr` GPIO pins in the code.
   - If conflicts or risks are identified, the agent will propose safe alternative pins or necessary hardware modifications (e.g., adding capacitors).
