# Goals for Our Project

* Minimal external dependencies
* Single executable binary
* Support for all major desktop platforms (Windows, Linux, and macOS)
* Lightweight architecture
* High performance and low resource usage
* Responsive user interface
* Small binary size
* Easy distribution and deployment
* Maintainable and scalable codebase
* Fast startup time
* Long-term portability and stability

# GUI Library Choices (Cross-Platform)

* Qt
* GTK
* Dear ImGui

---

# Why We Did Not Choose Qt

## Drawbacks of Qt

* Difficult to cross-compile consistently across platforms
* Large dependency footprint
* Increases binary size significantly
* Complex build system and tooling
* Licensing considerations for some use cases
* Slower iteration and deployment compared to lightweight solutions

Qt is a powerful framework, but it is better suited for large enterprise-style desktop applications where framework features outweigh binary size and dependency concerns.

---

# Why We Did Not Choose GTK

## Drawbacks of GTK

* Although cross-platform, it is heavily Linux-centric
* Native appearance feels inconsistent on Windows and macOS
* Cross-platform support is not as polished as Linux
* Packaging and distribution on non-Linux systems can be cumbersome
* Dependency management can become complicated

GTK works well in Linux environments, but it does not provide the level of portability and native integration we want for a lightweight desktop application.

---

# Why We Chose Dear ImGui

## Drawbacks of Dear ImGui

* Many higher-level components must be built manually
* Immediate-mode UI requires a different design approach compared to traditional retained-mode frameworks

## Advantages of Dear ImGui

* Easy to cross-compile
* Minimal dependencies beyond the selected rendering backend
* Multiple backend options available (OpenGL, Vulkan, DirectX, Metal, SDL, GLFW, etc.)
* Lightweight and fast
* Excellent runtime performance
* Easy state management
* Game engine–style architecture simplifies UI flow and rendering
* Full control over rendering and platform integration
* Easier debugging compared to heavyweight frameworks
* Faster iteration during development
* Suitable for tools, editors, debugging interfaces, and performance-oriented applications

Dear ImGui aligns well with our priorities: simplicity, portability, performance, and maintainability.

---

# Programming Language Choices

* C++
* Python
* C

---

# Why We Did Not Choose C++

## Drawbacks of C++

* Difficult to debug in large codebases
* Slow compilation times
* Extremely large and complex language surface
* Multiple ways to solve the same problem reduce consistency
* Heavy use of abstractions can make maintenance harder
* ABI compatibility issues across compilers and platforms
* Increased build system complexity

C++ offers powerful abstractions and performance, but its complexity can negatively impact maintainability, portability, and build reliability for our goals.

---

# Why We Did Not Choose Python

## Drawbacks of Python

* Interpreted language with slower runtime performance
* GIL (Global Interpreter Lock) limits true multithreading
* Harder to distribute as a standalone native application
* Higher memory usage compared to native languages
* Poor real-time performance
* Performance-critical libraries are often implemented in C/C++ underneath
* Dependency management and packaging can become problematic across platforms

Python has an excellent ecosystem and developer experience, but it does not fit our requirements for performance, portability, binary size, and standalone distribution.

---

# Why We Chose C

## Advantages of C

* Minimal and simple language design
* Stable and predictable ABI
* Supported on virtually every platform and compiler
* Extremely portable
* Fast compilation times
* Small runtime overhead
* Fine-grained control over memory and performance
* Easier to understand generated binaries and behavior
* Well suited for systems programming and performance-critical applications
* Long-term ecosystem stability

Many highly maintainable and successful software projects are written in C, including:

* Linux Kernel
* SQLite
* Python
* Redis
* Nginx
* Git
* FFmpeg
* PostgreSQL
* Lua

## Tradeoffs of C

* Manual memory management
* Fewer abstractions compared to higher-level languages
* Requires disciplined engineering practices

Despite these tradeoffs, C provides the simplicity, control, portability, and performance that best match our project goals.

---

# Video Encoding / Decoding

## FFmpeg

We chose FFmpeg for multimedia processing because:

* It is the industry standard for video and audio processing
* Supports virtually all major codecs and container formats
* Highly optimized and performance-oriented
* Cross-platform and battle-tested
* Large ecosystem and community support
* Flexible API for encoding, decoding, streaming, filtering, and transcoding

Although FFmpeg is large and complex internally, there is no realistic alternative that matches its maturity, compatibility, and performance.
