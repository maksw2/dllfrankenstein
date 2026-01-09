# DLLFrankenstein Enterprise™

**Advanced Dynamic Link Library Orchestration & Native Invocation Utility**

DLLFrankenstein is a lightweight, high-performance interoperability bridge designed to facilitate the execution of arbitrary exported functions from unmanaged Windows Dynamic Link Libraries (DLLs). It serves as a robust, feature-rich alternative to the legacy `rundll32.exe` utility, offering developers granular control over memory management, type signatures, and the execution stack.

## Key Features

* **Dynamic Function Resolution:** Invoke functions via standard naming conventions or low-level ordinals.
* **Granular Memory Control:** Direct access to heap allocation (`malloc`/`free`) and arbitrary memory address modification.
* **Type-Agnostic FFI:** Flexible Foreign Function Interface supporting a wide array of primitive types (`i32`, `u64`, `f32`, `voidptr`) and string encodings.
* **Scriptable Automation:** Support for batch processing via `.ffi` scripting for reproducible test scenarios.
* **Interactive REPL:** A Read-Eval-Print Loop environment for rapid prototyping and live debugging.

---

## ⚠️ Compliance & Safety Notice

**Intended Audience:** Systems Architects, Reverse Engineers, and QA Automation Specialists.

Please be advised that DLLFrankenstein provides **unrestricted access** to the process memory space.

* **Type Safety:** The runtime does not enforce strict type safety. It is the operator's responsibility to ensure signature alignment to prevent stack corruption.
* **Memory Integrity:** Direct memory manipulation may lead to application instability if pointers are not managed correctly.
* **Production Use:** While capable, this tool is primarily designed for development, debugging, and research environments.

---

## Installation & Deployment

### Prerequisites

* Microsoft Visual Studio Build Tools (MSVC)
* Windows 10/11 or Windows Server 2016+

### Build Pipeline

To compile the application from source, execute the standard build script located in the root directory:

```powershell
.\build.bat

```

For integration testing, the accompanying test module can be compiled via the MSVC CL compiler:

```powershell
cl /LD /Zi test.c /link /debug user32.lib gdi32.lib legacy_stdio_definitions.lib

```

---

## Usage Guide

The `invoke.exe` CLI accepts parameters for target libraries, return types, and arguments.

**Syntax:**

```bash
invoke.exe <dll_path> <return_type> <func_name>(<arg_type> <arg_value>, ...) [options]

```

### Interactive Mode (REPL)

To enter the interactive shell for persistent session management:

```bash
invoke.exe --interactive

```

### Scripted Execution

To execute a pre-defined workflow:

```bash
invoke.exe --script <path_to_workflow.ffi>

```

### Command Reference

| Command | Description |
| --- | --- |
| `/loaddll <path>` | Loads a library into the process address space. |
| `/set <addr> <type> <val>` | Writes a value to a specific memory address. |
| `/get <addr> <type>` | Reads a value from a specific memory address. |
| `/hex <addr> [count]` | Performs a hexadecimal dump for inspection. |
| `/struct { ... }` | Calculates struct offsets and packing alignment. |

---

## Use Case Scenarios

### 1. Win32 API Prototyping

Quickly validate API behavior without setting up a full C++ project scaffold.

```bash
invoke.exe user32.dll i32 MessageBoxA(voidptr 0, str "Operation Successful", str "System Alert", u32 0)

```

### 2. Legacy Application Hooking

Inject parameters into legacy applications that utilize non-standard entry points (e.g., specific game engines or proprietary business logic).

*(See `/examples/tf2_launcher_hook.ffi` for a detailed implementation strategy).*

### 3. Memory Lifecycle Management

Manually allocate buffers for testing pointer logic or stress-testing garbage collection routines.

```bash
> $buffer = msvcrt.dll voidptr malloc(u64 1024)
> /set $buffer str "Payload Injection"
> msvcrt.dll void free(voidptr $buffer)

```

---

## Frequently Asked Questions (FAQ)

**Q: How does this solution differentiate itself from `rundll32`?**
A: DLLFrankenstein offers a superset of functionality, including support for non-standard calling conventions, complex return types, and persistent memory states, which are not supported by the native Windows utility.

**Q: Is there support for pointer arithmetic?**
A: Yes. The system supports byte-level pointer arithmetic for precise memory traversal.

**Q: What is the support policy for application crashes (SEH Exceptions)?**
A: The application implements Structured Exception Handling (SEH) to prevent immediate termination during minor faults. However, significant memory access violations will necessitate a process restart. Please report reproducible issues via the issue tracker with full stack traces.

---

## Contribution Guidelines

We welcome contributions from the community. Please ensure all Pull Requests adhere to the following standards:

1. **Code Style:** Indent with 4 spaces.
2. **Testing:** All new features must be accompanied by relevant test cases.
3. **Workflow:** Fork the repository, create a feature branch, and submit a PR for review.

## Contact & Support

For enterprise inquiries or technical support, please contact the maintainer at `maksw@maksw.pl`.