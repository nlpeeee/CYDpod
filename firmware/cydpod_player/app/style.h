// ===========================================================================
// style.h -- how a control is shaped
// ===========================================================================
// The player has one look: the Classic Touch pill, a raised face with a
// hairline outline and a 1 px highlight along its top edge. It used to be one
// row of a table of seven that Setup cycled; the other six, and the setting
// that chose between them, were retired in CY-81. The numbers below are that
// row (radius RADIUS_SOFT, inset 3, border 1), unchanged.
#pragma once

#include "tokens.h"

static const int STYLE_RADIUS = RADIUS_SOFT;   // corner radius of faces, the confirm panel and toasts
static const int STYLE_INSET  = 3;             // a face inset from its touch rectangle
static const int STYLE_BORDER = 1;             // outline thickness
