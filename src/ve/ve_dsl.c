/* ve_dsl.c — parse the editing DSL into a VeScript (input/output + VeOp[]).
 *
 * Grammar: one statement per line. Tokens are whitespace-separated; use
 * "double quotes" for paths/values with spaces. '#' starts a comment.
 * Verb names are case-insensitive. Examples:
 *
 *      in   "clip.mp4"
 *      trim 00:00:10  1:05.5      # keyframe-accurate, lossless when possible
 *      rotate 90                  # lossless (display matrix)
 *      mute                       # lossless (drop audio)
 *      crop 1280 720 100 60       # forces video re-encode
 *      volume 150%                # audio re-encode only
 *      out  "out.mp4"
 */
#include "ve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXTOK 32
#define TOKLEN VE_PATH_MAX

/* ── small helpers ────────────────────────────────────────────────────────── */

static int ci_eq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

/* split one line into tokens; respects "quotes"; stops at '#'. returns count */
static int tokenize(const char* line, char tok[MAXTOK][TOKLEN]) {
    int n = 0;
    const char* p = line;
    while (*p && n < MAXTOK) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p || *p == '#') break;
        int len = 0;
        if (*p == '"') {                       /* quoted */
            p++;
            while (*p && *p != '"' && len < TOKLEN - 1) tok[n][len++] = *p++;
            if (*p == '"') p++;
        } else {
            while (*p && !isspace((unsigned char)*p) && *p != '#' && len < TOKLEN - 1)
                tok[n][len++] = *p++;
        }
        tok[n][len] = 0;
        n++;
    }
    return n;
}

/* parse [HH:]MM:SS[.ms] or SS[.ms] -> seconds, or -1 on error */
static double parse_time(const char* s) {
    double parts[3] = {0,0,0};
    int np = 0;
    char buf[64]; size_t bl = 0;
    for (const char* p = s; ; p++) {
        if (*p == ':' || *p == 0) {
            buf[bl] = 0;
            if (bl == 0 || np >= 3) return -1;
            char* end; double v = strtod(buf, &end);
            if (*end) return -1;
            parts[np++] = v; bl = 0;
            if (*p == 0) break;
        } else {
            if (bl >= sizeof(buf) - 1) return -1;
            if (!isdigit((unsigned char)*p) && *p != '.') return -1;
            buf[bl++] = *p;
        }
    }
    if (np == 1) return parts[0];
    if (np == 2) return parts[0]*60 + parts[1];
    return parts[0]*3600 + parts[1]*60 + parts[2];
}

/* parse a number, allowing a trailing '%' (returns fraction) */
static int parse_num(const char* s, double* out) {
    char* end; double v = strtod(s, &end);
    if (end == s) return 0;
    if (*end == '%') { v /= 100.0; end++; }
    if (*end) return 0;
    *out = v;
    return 1;
}

static int parse_int(const char* s, int* out) {
    char* end; long v = strtol(s, &end, 10);
    if (end == s || *end) return 0;
    *out = (int)v; return 1;
}

/* ── op list growth ───────────────────────────────────────────────────────── */

static VeOp* push_op(VeScript* s, VeOpKind k, int line) {
    if (s->n_ops >= s->cap_ops) {
        int nc = s->cap_ops ? s->cap_ops * 2 : 16;
        VeOp* na = realloc(s->ops, (size_t)nc * sizeof(VeOp));
        if (!na) return NULL;
        s->ops = na; s->cap_ops = nc;
    }
    VeOp* op = &s->ops[s->n_ops++];
    memset(op, 0, sizeof(*op));
    op->kind = k; op->line = line;
    return op;
}

#define FAIL(fmt, ...) do { snprintf(err, errlen, "line %d: " fmt, line, ##__VA_ARGS__); \
                            ve_script_free(out); return false; } while (0)
#define NEED(k)        do { if (nt < (k)) FAIL("'%s' needs %d argument(s)", verb, (k)-1); } while (0)

bool ve_parse(const char* text, VeScript* out, char* err, int errlen) {
    memset(out, 0, sizeof(*out));
    int line = 0;
    const char* cur = text;

    while (*cur) {
        /* read one line */
        char linebuf[2048]; int ll = 0; line++;
        while (*cur && *cur != '\n' && ll < (int)sizeof(linebuf) - 1) linebuf[ll++] = *cur++;
        linebuf[ll] = 0;
        if (*cur == '\n') cur++;

        char tok[MAXTOK][TOKLEN];
        int nt = tokenize(linebuf, tok);
        if (nt == 0) continue;
        const char* verb = tok[0];

        if (ci_eq(verb, "in") || ci_eq(verb, "input") || ci_eq(verb, "load")) {
            NEED(2); ve_copy(out->input, sizeof(out->input), tok[1]);
        } else if (ci_eq(verb, "out") || ci_eq(verb, "output") || ci_eq(verb, "export")) {
            NEED(2); ve_copy(out->output, sizeof(out->output), tok[1]);
        } else if (ci_eq(verb, "rotate")) {
            NEED(2); VeOp* o = push_op(out, VE_ROTATE, line);
            if (!parse_num(tok[1], &o->u.rotate.deg)) FAIL("bad angle '%s'", tok[1]);
            o->u.rotate.ccw = (nt >= 3 && ci_eq(tok[2], "ccw"));
        } else if (ci_eq(verb, "flip")) {
            NEED(2); VeOp* o = push_op(out, VE_FLIP, line);
            if (ci_eq(tok[1],"h")||ci_eq(tok[1],"horizontal")) o->u.flip.axis='h';
            else if (ci_eq(tok[1],"v")||ci_eq(tok[1],"vertical")) o->u.flip.axis='v';
            else FAIL("flip wants h or v");
        } else if (ci_eq(verb, "mute")) {
            push_op(out, VE_MUTE, line);
        } else if (ci_eq(verb, "add-audio") || ci_eq(verb, "addaudio")) {
            NEED(2); VeOp* o = push_op(out, VE_ADD_AUDIO, line);
            ve_copy(o->u.add_audio.path, sizeof(o->u.add_audio.path), tok[1]);
            ve_copy(o->u.add_audio.lang, sizeof(o->u.add_audio.lang), nt>=3?tok[2]:"und");
        } else if (ci_eq(verb, "remove-audio")) {
            NEED(2); VeOp* o = push_op(out, VE_REMOVE_AUDIO, line);
            if (!parse_int(tok[1], &o->u.remove_audio.index)) FAIL("bad index");
        } else if (ci_eq(verb, "volume")) {
            NEED(2); VeOp* o = push_op(out, VE_VOLUME, line);
            if (!parse_num(tok[1], &o->u.volume.factor)) FAIL("bad volume");
        } else if (ci_eq(verb, "mono")) {
            push_op(out, VE_MONO, line);
        } else if (ci_eq(verb, "scale") || ci_eq(verb, "resize")) {
            VeOp* o = push_op(out, VE_SCALE, line);
            if (nt >= 3) { if(!parse_int(tok[1],&o->u.scale.w)||!parse_int(tok[2],&o->u.scale.h)) FAIL("bad size"); }
            else if (nt == 2) {                 /* WxH form */
                if (sscanf(tok[1], "%dx%d", &o->u.scale.w, &o->u.scale.h) != 2) FAIL("bad size");
            } else FAIL("scale needs W H");
        } else if (ci_eq(verb, "crop")) {
            NEED(5); VeOp* o = push_op(out, VE_CROP, line);
            if(!parse_int(tok[1],&o->u.crop.w)||!parse_int(tok[2],&o->u.crop.h)||
               !parse_int(tok[3],&o->u.crop.x)||!parse_int(tok[4],&o->u.crop.y)) FAIL("bad crop");
        } else if (ci_eq(verb, "trim") || ci_eq(verb, "cut")) {
            NEED(2); VeOp* o = push_op(out, VE_TRIM, line);
            o->u.trim.start = parse_time(tok[1]);
            if (o->u.trim.start < 0) FAIL("bad start time");
            o->u.trim.end = (nt >= 3) ? parse_time(tok[2]) : -1;
            if (nt >= 3 && o->u.trim.end < 0) FAIL("bad end time");
        } else if (ci_eq(verb, "merge") || ci_eq(verb, "concat")) {
            NEED(2); VeOp* o = push_op(out, VE_MERGE, line);
            for (int i = 1; i < nt && o->u.merge.n < VE_MERGE_MAX; i++) {
                char* slot = o->u.merge.paths[o->u.merge.n++];
                ve_copy(slot, VE_PATH_MAX, tok[i]);
            }
        } else if (ci_eq(verb, "reverse")) {
            NEED(2); VeOpKind k = (ci_eq(tok[1],"audio")||ci_eq(tok[1],"a"))?VE_REVERSE_AUDIO:VE_REVERSE_VIDEO;
            push_op(out, k, line);
        } else if (ci_eq(verb, "thumbnail") || ci_eq(verb, "cover")) {
            NEED(2); VeOp* o = push_op(out, VE_ADD_THUMBNAIL, line);
            ve_copy(o->u.thumbnail.path, sizeof(o->u.thumbnail.path), tok[1]);
        } else if (ci_eq(verb, "remove-thumbnail")) {
            push_op(out, VE_REMOVE_THUMBNAIL, line);
        } else if (ci_eq(verb, "subtitle") || ci_eq(verb, "sub")) {
            NEED(2); VeOp* o = push_op(out, VE_ADD_SUBTITLE, line);
            ve_copy(o->u.subtitle.path, sizeof(o->u.subtitle.path), tok[1]);
            ve_copy(o->u.subtitle.lang, sizeof(o->u.subtitle.lang), nt>=3?tok[2]:"und");
        } else if (ci_eq(verb, "remove-subtitle")) {
            NEED(2); VeOp* o = push_op(out, VE_REMOVE_SUBTITLE, line);
            if (!parse_int(tok[1], &o->u.remove_sub.index)) FAIL("bad index");
        } else if (ci_eq(verb,"brightness")||ci_eq(verb,"contrast")||ci_eq(verb,"hue")||
                   ci_eq(verb,"saturation")||ci_eq(verb,"sat")||ci_eq(verb,"gamma")) {
            NEED(2);
            VeOpKind k = ci_eq(verb,"brightness")?VE_BRIGHTNESS:
                         ci_eq(verb,"contrast")?VE_CONTRAST:
                         ci_eq(verb,"hue")?VE_HUE:
                         ci_eq(verb,"gamma")?VE_GAMMA:VE_SATURATION;
            VeOp* o = push_op(out, k, line);
            if (!parse_num(tok[1], &o->u.adjust.value)) FAIL("bad value");
        } else if (ci_eq(verb, "grayscale") || ci_eq(verb, "gray")) {
            push_op(out, VE_GRAYSCALE, line);
        } else if (ci_eq(verb, "aspect")) {
            VeOp* o = push_op(out, VE_ASPECT, line);
            if (nt >= 3) { if(!parse_int(tok[1],&o->u.aspect.num)||!parse_int(tok[2],&o->u.aspect.den)) FAIL("bad aspect"); }
            else if (nt == 2) { if (sscanf(tok[1],"%d:%d",&o->u.aspect.num,&o->u.aspect.den)!=2) FAIL("bad aspect"); }
            else FAIL("aspect needs N D");
        } else if (ci_eq(verb, "fps")) {
            NEED(2); VeOp* o = push_op(out, VE_FPS, line);
            if (!parse_num(tok[1], &o->u.fps.value)) FAIL("bad fps");
        } else if (ci_eq(verb, "speed")) {
            NEED(2); VeOp* o = push_op(out, VE_SPEED, line);
            if (!parse_num(tok[1], &o->u.speed.factor) || o->u.speed.factor <= 0) FAIL("bad speed");
        } else if (ci_eq(verb, "fade")) {
            NEED(3); VeOp* o = push_op(out, VE_FADE, line);
            o->u.fade.dir = (ci_eq(tok[1],"out")||ci_eq(tok[1],"o")) ? 'o' : 'i';
            o->u.fade.dur = parse_time(tok[2]);
            if (o->u.fade.dur < 0) FAIL("bad fade duration");
            o->u.fade.target = (nt>=4 && (ci_eq(tok[3],"audio")||ci_eq(tok[3],"a")))?'a':
                               (nt>=4 && (ci_eq(tok[3],"both")||ci_eq(tok[3],"b")))?'b':'v';
        } else if (ci_eq(verb, "denoise")) {
            push_op(out, VE_DENOISE, line);
        } else if (ci_eq(verb, "sharpen")) {
            push_op(out, VE_SHARPEN, line);
        } else if (ci_eq(verb, "pad") || ci_eq(verb, "letterbox")) {
            NEED(3); VeOp* o = push_op(out, VE_PAD, line);
            if(!parse_int(tok[1],&o->u.pad.w)||!parse_int(tok[2],&o->u.pad.h)) FAIL("bad pad size");
        } else if (ci_eq(verb, "metadata") || ci_eq(verb, "meta")) {
            NEED(3); VeOp* o = push_op(out, VE_METADATA, line);
            ve_copy(o->u.meta.key, sizeof(o->u.meta.key), tok[1]);
            ve_copy(o->u.meta.val, sizeof(o->u.meta.val), tok[2]);
        } else if (ci_eq(verb, "format") || ci_eq(verb, "container")) {
            NEED(2); VeOp* o = push_op(out, VE_FORMAT, line);
            ve_copy(o->u.format.name, sizeof(o->u.format.name), tok[1]);
        } else {
            FAIL("unknown command '%s'", verb);
        }
    }
    return true;
}

void ve_script_free(VeScript* s) {
    if (!s) return;
    free(s->ops);
    s->ops = NULL; s->n_ops = s->cap_ops = 0;
}
