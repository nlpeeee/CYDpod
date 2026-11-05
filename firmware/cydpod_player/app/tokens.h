// ===========================================================================
// tokens.h -- the measurements the shared drawing helpers use
// ===========================================================================
// Sizes only. Colours are the theme's (theme.h) and the control shape is the
// style's (style.h). Page geometry lives in classic_layout.h.
#pragma once

// Spacing.
static const int SP_M  = 8;     // between related things
static const int SP_L  = 12;    // a text block's margin from its edge
static const int SP_XL = 16;    // between groups

// Corner radius of the one control shape (style.h).
static const int RADIUS_SOFT = 4;

// Icon nominal height, in px -- the `size` argument to ui_icon().
static const int ICON_MD = 18;  // a header toggle
