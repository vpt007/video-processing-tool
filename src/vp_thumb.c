/* vp_thumb.c — thumbnail extraction helpers for the editor.
 *
 * Provides a small utility to grab the currently displayed frame from the
 * playback engine as an RGB24 buffer, which the UI can use to build a cover
 * thumbnail. Included by the unity build after vp_engine.c so the VPEngine
 * API is available.
 */
#include "vp_engine.h"
#include <stdlib.h>

/* Copy the engine's current frame into a caller-owned VPFrameRGB (pixels are
 * malloc'd; free with vp_engine_frame_free). Returns 1 on success, 0 if no
 * frame is available yet. */
int vp_thumb_capture(VPEngine *eng, VPFrameRGB *out)
{
	if (!eng || !out)
		return 0;
	return vp_engine_snapshot(eng, out) ? 1 : 0;
}

/* Convenience: capture the current frame and immediately free it, returning
 * only whether a frame was available. Useful for "is there a frame yet?"
 * checks without keeping a copy around. */
int vp_thumb_has_frame(VPEngine *eng)
{
	if (!eng)
		return 0;
	VPFrameRGB f;
	if (!vp_engine_snapshot(eng, &f))
		return 0;
	vp_engine_frame_free(&f);
	return 1;
}