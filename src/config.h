/* config.h — shared editor configuration types and globals.
 *
 * Included by main.c (the unity-build entry point) and by ve_export.c so the
 * export layer can be analyzed/compiled independently. Holds the common
 * VideoConfig struct, the buffer-size constant, and the app-wide error
 * message globals that the export worker reports through.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "cimgui.h"

#define JH_BUFFER_MAX (1 << 10)

/* App window geometry / title (used by main.c). */
#define WIDTH     100
#define HEIGHT    100
#define APP_TITLE "Video Processing Tool"

/* The full set of video/audio edit settings the user can change. */
typedef struct {
	ImVec2 aspect_ratio;
	ImVec2 scale;
	int rotate;
	int scale_volume;
	bool flip_h;
	bool flip_v;
	bool mute;
	bool sterio_to_mono;
	char vf[JH_BUFFER_MAX];
	char af[JH_BUFFER_MAX];
} VideoConfig;

/* App-wide error dialog state (defined in main.c). */
extern const char *error_message;
extern const char *error_title;

#endif /* CONFIG_H */