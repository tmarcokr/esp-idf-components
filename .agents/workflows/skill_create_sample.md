---
description: Create a new modular feature (ISample) if the project uses the Sample architecture.
---

# 🚀 Workflow: Creating a New Sample (Optional Architecture)

Use this workflow when the current project utilizes the `ISample` modular architecture and you need to add a new feature.

1. **Component Definition (If needed)**:
   - Create a folder in `components/` if a new hardware wrapper is required.
   - Implement the wrapper in C++ within an appropriate namespace (e.g., `Espressif::Wrappers`).

2. **Sample Implementation**:
   - Create the `.hpp` and `.cpp` files in `main/samples/<new_sample>/`.
   - Inherit from the project's interface (e.g., `Espressif::App::ISample`).
   - Implement `setup()` (hardware initialization) and `run()` (loop/task logic).

3. **Main Integration**:
   - Include the header in `main/main.cpp`.
   - Instantiate the object (use `static` or dynamic allocation as per project rules).
   - Call `setup()`.
   - Launch the FreeRTOS task with `xTaskCreate`, passing the instance.

4. **Verification**:
   - Run `idf.py build` to ensure compilation succeeds.
