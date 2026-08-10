/* icon_font.h — icon glyph strings for the icomoon icon font.
 *
 * The app renders toolbar/button icons by drawing glyphs from
 * asset/fonts/icomoon.ttf (embedded as a byte array in icon_moon.h). Each
 * macro below expands to the UTF-8 encoding of the glyph's private-use-area
 * codepoint, taken from asset/selection.json. The icon font is pushed with
 * igPushFont(icon_font, ...) before these strings are drawn.
 */
#ifndef ICON_FONT_H
#define ICON_FONT_H

/* Transport / generic icons (icomoon set). */
#define i_pause                  "\xEE\xA8\xA8" /* pause        U+EA28 */
#define i_play3                  "\xEE\xA8\xAE" /* play3        U+EA2E */
#define i_fire                   "\xEE\xA6\xBB" /* fire         U+E9BB */
#define i_folder_open            "\xEE\xA5\x82" /* folder-open  U+E942 */
#define i_info                   "\xEE\xA8\x9E" /* info         U+EA1E */
#define i_music                  "\xEE\xA4\xA3" /* music        U+E923 */

/* Custom VPU toolbar icons (from asset/vpu_icon_*.svg). */
#define i_vpu_icon_thumbnail     "\xEE\xA4\x8B" /* U+E90B */
#define i_vpu_icon_crop          "\xEE\xA4\x81" /* U+E901 */
#define i_vpu_icon_trim          "\xEE\xA4\x8C" /* U+E90C */
#define i_vpu_icon_add_subs      "\xEE\xA4\x80" /* U+E900 */
#define i_vpu_icon_delete_sub    "\xEE\xA4\x82" /* U+E902 */
#define i_vpu_icon_rotate_90     "\xEE\xA4\x85" /* U+E905 */
#define i_vpu_icon_rotate_180    "\xEE\xA4\x86" /* U+E906 */
#define i_vpu_icon_rotate_270    "\xEE\xA4\x87" /* U+E907 */
#define i_vpu_icon_rotate_custom "\xEE\xA4\x88" /* U+E908 */
#define i_vpu_icon_fliph         "\xEE\xA4\x83" /* U+E903 */
#define i_vpu_icon_flipv         "\xEE\xA4\x84" /* U+E904 */
#define i_vpu_icon_volume_50_up  "\xEE\xA4\x90" /* U+E910 */
#define i_vpu_icon_volume_25_Up  "\xEE\xA4\x8E" /* U+E90E */
#define i_vpu_icon_volume_50_down "\xEE\xA4\x8F" /* U+E90F */
#define i_vpu_icon_volume_25_down "\xEE\xA4\x8D" /* U+E90D */
#define i_vpu_icon_volume_mute   "\xEE\xA4\x91" /* U+E911 */
#define i_vpu_icon_stero2mono    "\xEE\xA4\x8A" /* U+E90A */
#define i_vpu_icon_settings      "\xEE\xA4\x89" /* U+E909 */

/* Thumbnail toolbar icons (from asset/selection.json). */
#define i_VPU_Icon_Vector_Select_Frame_as_Thumbnail "\xEE\xAB\xBD" /* U+EAFD */
#define i_VPU_Icon_Vector_Remove_Thumbnail_2        "\xEE\xAB\xBE" /* U+EAFE */

#endif /* ICON_FONT_H */
