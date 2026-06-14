/* ve_ops.c — classification: does an operation force a re-encode? */
#include "ve.h"
#include <stdio.h>

void ve_copy(char* dst, unsigned long cap, const char* src) {
    if (cap) snprintf(dst, cap, "%s", src);
}

const char* ve_op_name(VeOpKind k) {
    switch (k) {
    case VE_ROTATE:           return "rotate";
    case VE_FLIP:             return "flip";
    case VE_MUTE:             return "mute";
    case VE_ADD_AUDIO:        return "add-audio";
    case VE_REMOVE_AUDIO:     return "remove-audio";
    case VE_VOLUME:           return "volume";
    case VE_MONO:             return "mono";
    case VE_SCALE:            return "scale";
    case VE_CROP:             return "crop";
    case VE_MERGE:            return "merge";
    case VE_TRIM:             return "trim";
    case VE_REVERSE_VIDEO:    return "reverse-video";
    case VE_REVERSE_AUDIO:    return "reverse-audio";
    case VE_ADD_THUMBNAIL:    return "add-thumbnail";
    case VE_REMOVE_THUMBNAIL: return "remove-thumbnail";
    case VE_ADD_SUBTITLE:     return "add-subtitle";
    case VE_REMOVE_SUBTITLE:  return "remove-subtitle";
    case VE_BRIGHTNESS:       return "brightness";
    case VE_CONTRAST:         return "contrast";
    case VE_HUE:              return "hue";
    case VE_SATURATION:       return "saturation";
    case VE_GAMMA:            return "gamma";
    case VE_GRAYSCALE:        return "grayscale";
    case VE_ASPECT:           return "aspect";
    case VE_FPS:              return "fps";
    case VE_SPEED:            return "speed";
    case VE_FADE:             return "fade";
    case VE_DENOISE:          return "denoise";
    case VE_SHARPEN:          return "sharpen";
    case VE_PAD:              return "pad";
    case VE_METADATA:         return "metadata";
    case VE_FORMAT:           return "format";
    default:                  return "?";
    }
}

VeOpClass ve_classify(const VeOp* op) {
    VeOpClass c = {0};
    switch (op->kind) {

    /* ---- pure container / metadata: always lossless ---- */
    case VE_MUTE:
    case VE_REMOVE_AUDIO:
    case VE_ADD_AUDIO:           /* extra stream muxed by copy */
    case VE_REMOVE_SUBTITLE:
    case VE_ADD_SUBTITLE:
    case VE_ADD_THUMBNAIL:
    case VE_REMOVE_THUMBNAIL:
    case VE_METADATA:
    case VE_FORMAT:
    case VE_ASPECT:              /* SAR is a metadata field */
        c.container_only = true;
        break;

    /* rotate: lossless via display matrix iff a multiple of 90° */
    case VE_ROTATE: {
        double d = op->u.rotate.deg;
        long q = (long)d;
        if ((double)q == d && (q % 90) == 0) c.container_only = true;
        else                                 c.needs_video_decode = true;
        break;
    }

    /* ---- structural: timeline edits, copy-friendly ---- */
    case VE_TRIM:                /* keyframe-aligned stream copy */
    case VE_MERGE:               /* concat by copy when params match */
        c.structural = true;
        c.container_only = true; /* attempted losslessly; executor may fall back */
        break;

    /* ---- force VIDEO transcode ---- */
    case VE_FLIP:
    case VE_SCALE:
    case VE_CROP:
    case VE_BRIGHTNESS:
    case VE_CONTRAST:
    case VE_HUE:
    case VE_SATURATION:
    case VE_GAMMA:
    case VE_GRAYSCALE:
    case VE_FPS:
    case VE_REVERSE_VIDEO:
    case VE_DENOISE:
    case VE_SHARPEN:
    case VE_PAD:
        c.needs_video_decode = true;
        break;

    /* ---- force AUDIO transcode only (video stays copy) ---- */
    case VE_VOLUME:
    case VE_MONO:
    case VE_REVERSE_AUDIO:
        c.needs_audio_decode = true;
        break;

    /* ---- affect both streams ---- */
    case VE_SPEED:               /* setpts (video) + atempo (audio) */
        c.needs_video_decode = true;
        c.needs_audio_decode = true;
        break;

    case VE_FADE:                /* target decides */
        if (op->u.fade.target == 'v' || op->u.fade.target == 'b') c.needs_video_decode = true;
        if (op->u.fade.target == 'a' || op->u.fade.target == 'b') c.needs_audio_decode = true;
        break;

    default:
        break;
    }
    return c;
}
