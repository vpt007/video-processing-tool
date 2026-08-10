/* cimgui_unity.cpp — unity build for the cimgui / Dear ImGui stack.
 *
 * This single translation unit compiles the entire ImGui + cimgui + backend
 * code together (a "unity build"), which avoids the need for a separate build
 * step per .cpp file and lets the compiler inline across translation units.
 *
 * The macros below configure the build before any ImGui header is pulled in:
 *   • IMGUI_IMPL_OPENGL_LOADER_GL3W — use the GL3W loader for OpenGL symbols.
 *   • IMGUI_USER_CONFIG            — point at our custom imconfig overrides.
 *   • IMGUI_DISABLE_OBSOLETE_FUNCTIONS — drop deprecated ImGui APIs.
 *   • IMGUI_IMPL_API extern "C"    — export backend symbols with C linkage so
 *                                    the C code in the rest of the app can use
 *                                    them.
 *   • IMGUI_DEFINE_MATH_OPERATORS  — enable ImVec2/ImVec4 operator overloads.
 */
#define IMGUI_IMPL_OPENGL_LOADER_GL3W
#define IMGUI_USER_CONFIG "../cimconfig.h"
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1
#define IMGUI_IMPL_API extern "C"
#define IMGUI_DEFINE_MATH_OPERATORS

/* Core ImGui implementation (widgets, drawing, tables, demo). */
#include "imgui.cpp"
#include "imgui_draw.cpp"
#include "imgui_demo.cpp"
#include "imgui_widgets.cpp"
#include "imgui_tables.cpp"

/* Platform / renderer backends: OpenGL3 for drawing, GLFW for windowing. */
#include "backends/imgui_impl_opengl3.cpp"
#include "backends/imgui_impl_glfw.cpp"

/* The C API wrapper that lets the C code in this project drive ImGui. */
#include "cimgui.cpp"
