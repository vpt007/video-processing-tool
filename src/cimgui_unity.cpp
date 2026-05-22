#define IMGUI_IMPL_OPENGL_LOADER_GL3W
#define IMGUI_USER_CONFIG "../cimconfig.h"
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS 1
#define IMGUI_IMPL_API extern "C"
#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.cpp"
#include "imgui_draw.cpp"
#include "imgui_demo.cpp"
#include "imgui_widgets.cpp"
#include "imgui_tables.cpp"

#include "backends/imgui_impl_opengl3.cpp"
#include "backends/imgui_impl_glfw.cpp"

#include "cimgui.cpp"
