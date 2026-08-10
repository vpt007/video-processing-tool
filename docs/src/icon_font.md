# icon_font.h - Icon Glyph Strings

> **File:** [`src/icon_font.h`](../../src/icon_font.h)
> **Package:** `app`
> **Purpose:** UTF-8 icon glyph strings for the icomoon icon font

## Overview

[`icon_font.h`](../../src/icon_font.h) defines macros that expand to the UTF-8 encoding of icon glyphs from the icomoon font (`asset/fonts/icomoon.ttf`, embedded as a byte array in [`icon_moon.h`](../../src/icon_moon.h)). The codepoints come from [`asset/selection.json`](../../asset/selection.json). The icon font is pushed with `igPushFont(icon_font, ...)` before these strings are drawn.

## Icon Macros

| Macro | Codepoint | Description |
|-------|-----------|-------------|
| `i_pause` | U+EA28 | Pause |
| `i_play3` | U+EA2E | Play |
| `i_fire` | U+E9BB | Fire |
| `i_folder_open` | U+E942 | Open folder |
| `i_info` | U+EA1E | Info |
| `i_music` | U+E923 | Music |
| `i_vpu_icon_thumbnail` | U+E90B | Thumbnail |
| `i_vpu_icon_crop` | U+E901 | Crop |
| `i_vpu_icon_trim` | U+E90C | Trim |
| `i_vpu_icon_add_subs` | U+E900 | Add subtitles |
| `i_vpu_icon_delete_sub` | U+E902 | Delete subtitle |
| `i_vpu_icon_rotate_90` | U+E905 | Rotate 90° |
| `i_vpu_icon_rotate_180` | U+E906 | Rotate 180° |
| `i_vpu_icon_rotate_270` | U+E907 | Rotate 270° |
| `i_vpu_icon_rotate_custom` | U+E908 | Custom rotate |
| `i_vpu_icon_fliph` | U+E903 | Flip horizontal |
| `i_vpu_icon_flipv` | U+E904 | Flip vertical |
| `i_vpu_icon_volume_50_up` | U+E910 | Volume +50% |
| `i_vpu_icon_volume_25_Up` | U+E90E | Volume +25% |
| `i_vpu_icon_volume_50_down` | U+E90F | Volume -50% |
| `i_vpu_icon_volume_25_down` | U+E90D | Volume -25% |
| `i_vpu_icon_volume_mute` | U+E911 | Mute |
| `i_vpu_icon_stero2mono` | U+E90A | Stereo to mono |
| `i_vpu_icon_settings` | U+E909 | Settings |
| `i_VPU_Icon_Vector_Select_Frame_as_Thumbnail` | U+EAFD | Select frame as thumbnail |
| `i_VPU_Icon_Vector_Remove_Thumbnail_2` | U+EAFE | Remove thumbnail |

## Usage

```c
igPushFont(icon_font, icon_size);
if (igButton(i_vpu_icon_crop, btn_size))
    crop_enabled = !crop_enabled;
igPopFont();
```

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [logger.h](logger.md) | [src/](index.md) | [util.c](util.md) |