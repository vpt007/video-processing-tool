/* nob.c — build recipe for the video processing tool (uses nob.h).
 *
 * Builds the app as a unity build:
 *   1. src/cimgui_unity.cpp  -> build/cimgui_unity.o   (C++17: ImGui + cimgui + backends)
 *   2. src/main.c            -> build/main.o           (C11: the whole app unity build)
 *   3. link                  -> bin/video-processing-tool
 *
 * Run with:  cc -o nob nob.c && ./nob
 */
#define NOB_IMPLEMENTATION
#include "src/vendor/nob.h"

#define BUILD_DIR "build"
#define BIN_DIR   "bin"
#define BIN_NAME  "vpt"

/* Context for the directory-walk rebuild check. */
typedef struct {
	const char *out;
	bool needs;
} RebuildCtx;

/* Walk callback: mark needs=true if any regular file is newer than `out`. */
static bool rebuild_walk(Nob_Walk_Entry entry)
{
	RebuildCtx *ctx = (RebuildCtx *)entry.data;
	if (entry.type == NOB_FILE_REGULAR &&
	    nob_needs_rebuild1(ctx->out, entry.path))
		ctx->needs = true;
	return true;
}

/* True if `out` is older than any regular file under `dir` (recursively). */
static bool needs_rebuild_dir(const char *out, const char *dir)
{
	RebuildCtx ctx = {out, false};
	nob_walk_dir(dir, rebuild_walk, .data = &ctx);
	return ctx.needs;
}

/* Compile a single source file into an object file (skips if up to date).
   Rebuilds if the source OR any file under `deps_dir` (headers, etc.) is
   newer than the object, so header changes always trigger a recompile. */
static bool compile(const char *src, const char *out, const char *compiler,
                    const char *std, const char **inc, size_t ninc,
                    const char **defs, size_t ndefs, const char *deps_dir)
{
	if (!nob_needs_rebuild(out, (const char *[]){src}, 1) &&
	    !needs_rebuild_dir(out, deps_dir))
		return true;
	Nob_Cmd cmd = {0};
	nob_cmd_append(&cmd, compiler, std, "-c", src);
	nob_cc_output(&cmd, out);
	for (size_t i = 0; i < ninc; i++)
		nob_cmd_append(&cmd, "-I", inc[i]);
	for (size_t i = 0; i < ndefs; i++)
		nob_cmd_append(&cmd, "-D", defs[i]);
	if (!nob_cmd_run_sync(cmd)) {
		nob_cmd_free(cmd);
		return false;
	}
	nob_cmd_free(cmd);
	return true;
}

int main(int argc, char **argv)
{
	NOB_GO_REBUILD_URSELF(argc, argv);

	if (!nob_mkdir_if_not_exists(BUILD_DIR))
		return 1;
	if (!nob_mkdir_if_not_exists(BIN_DIR))
		return 1;

	/* Include paths for the C++ unity build (cimgui + imgui + backends). */
	const char *inc_cpp[] = {
	    "src/vendor/cimgui",
	    "src/vendor/cimgui/imgui",
	    "src/vendor/cimgui/imgui/backends",
	};
	/* Include paths for the C unity build (app sources + vendored libs +
	   the repo root so "app_icon.h" resolves). */
	const char *inc_c[] = {
	    "src",
	    "src/vendor",
	    "src/vendor/cimgui",
	    "src/vendor/cimgui/imgui",
	    "src/vendor/cimgui/imgui/backends",
	    ".",
	};

	/* Feature-test macros must be set before ANY system header is pulled in.
	   os.c defines them itself, but in the unity build main.c includes many
	   headers before os.c, so they must also come from the command line. */
	const char *defs_c[] = {
	    "_POSIX_C_SOURCE=200809L",
	    "_DEFAULT_SOURCE=1",
	};

	const char *cpp_obj = BUILD_DIR "/cimgui_unity.o";
	const char *c_obj   = BUILD_DIR "/main.o";
	const char *bin     = BIN_DIR "/" BIN_NAME;

	if (!compile("src/cimgui_unity.cpp", cpp_obj, "g++", "-std=c++17",
		     inc_cpp, NOB_ARRAY_LEN(inc_cpp), NULL, 0,
		     "src/vendor/cimgui"))
		return 1;
	if (!compile("src/main.c", c_obj, "gcc", "-std=c11", inc_c,
		     NOB_ARRAY_LEN(inc_c), defs_c, NOB_ARRAY_LEN(defs_c),
		     "src"))
		return 1;

	/* Link the two objects into the final binary. */
	if (nob_needs_rebuild(bin, (const char *[]){cpp_obj, c_obj}, 2)) {
		Nob_Cmd cmd = {0};
		nob_cmd_append(&cmd, "g++", cpp_obj, c_obj, "-o", bin);
		/* FFmpeg / libav */
		nob_cmd_append(&cmd, "-lavformat", "-lavcodec", "-lavfilter",
			       "-lavutil", "-lswscale", "-lswresample");
		/* GLFW + OpenGL + system */
		nob_cmd_append(&cmd, "-lglfw", "-lGL", "-lpthread", "-ldl",
			       "-lm");
		if (!nob_cmd_run_sync(cmd)) {
			nob_cmd_free(cmd);
			return 1;
		}
		nob_cmd_free(cmd);
	}

	nob_log(NOB_INFO, "Built %s", bin);
	return 0;
}