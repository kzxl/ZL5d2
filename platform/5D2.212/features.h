#include "all_features.h"

#define FEATURE_LV_BUTTON_PROTECT // works even without the Q menu present
#undef FEATURE_FLASH_TWEAKS // no built-in flash
#undef FEATURE_FLASH_NOFLASH
#define FEATURE_LV_FOCUS_BOX_SNAP_TO_X5_RAW

// ============================================================================
// STILL PHOTOGRAPHY OPTIMIZATION: Disable all video/movie features
// ============================================================================
#undef FEATURE_NITRATE
#undef FEATURE_MOVIE_LOGGING
#undef FEATURE_MOVIE_RESTART
#undef FEATURE_REC_NOTIFY
#undef FEATURE_REC_NOTIFY_BEEP
#undef FEATURE_MOVIE_REC_KEY
#undef FEATURE_FPS_OVERRIDE
#undef FEATURE_FPS_RAMPING
#undef FEATURE_HDR_VIDEO
#undef FEATURE_SHUTTER_LOCK
#undef FEATURE_GRADUAL_EXPOSURE
#undef FEATURE_IMAGE_EFFECTS
#undef FEATURE_VIGNETTING_CORRECTION
#undef FEATURE_SHUTTER_FINE_TUNING
#undef FEATURE_REC_INDICATOR
#undef FEATURE_FORCE_LIVEVIEW
#undef FEATURE_OVERRIDE_MOVIE_30_MIN_LIMIT
#undef FEATURE_MOVIE_RECORDING_50D
#undef FEATURE_CROP_MODE_HACK
