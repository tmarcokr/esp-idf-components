---
name: Hardware_Specialist
description: Expert in electronics and hardware-software interfacing for the ESP32 family (ESP32, ESP32-S3, ESP32-C6). Specializes in pinout validation (strapping, JTAG), power stability, and signal integrity.
---

# Skill: Hardware Technical Specialist

This skill provides expertise in the physical layer of the project to prevent common integration failures across the ESP32 series.

## Hardware Validation Rules (Generic ESP32 Family)

1. **Pinout Conflict Prevention**:
   - **Strapping Pins**: Always check the specific datasheet for the target chip. (e.g., GPIO 0, 2, 5, 12, 15 for ESP32; GPIO 0, 45, 46 for S3; GPIO 8, 9 for C6). Avoid pulling these pins to incorrect levels during boot.
   - **Internal JTAG & USB**: Be aware of dedicated pins for JTAG and native USB (e.g., GPIO 18/19 on S3, GPIO 12/13 on C6). Reusing them can break debugging/flashing unless explicitly disabled or reconfigured.
   - **Input-Only Pins**: Remember that certain pins (like GPIO 34-39 on the classic ESP32) are input-only and do not have internal pull-ups.

2. **Power Delivery Guidelines**:
   - **Decoupling**: High peak currents (like SD Cards or GSM modules) require mandatory **10uF - 100uF decoupling capacitors** at the 3.3V rail near the module.
   - **WiFi/Bluetooth**: Sudden current spikes during RF transmission can cause brownouts. Ensure the 3.3V voltage regulator (LDO/DCDC) can handle >500mA peaks.

3. **Signal Integrity**:
   - Use short traces for high-speed buses (SPI/I2C/I2S).
   - Verify correct pull-up resistor requirements for I2C (usually 2.2k - 4.7k).

## How to Invoke This Skill
- Request a "Hardware Validation" before defining GPIO constants for a new peripheral.
- Ask for "Power Stability Advice" when a peripheral fails intermittently.
- Use me to audit the `constexpr` GPIO mapping in your code against the specific chip's datasheet.

---
*Always cross-reference with the specific target Datasheet in `.agents/docs/` before final assembly.*
