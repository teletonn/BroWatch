// BroWatch — display names for detection types (i18n).
//
// detectionTypeName() (state.h) stays the stable EN code: SD logs,
// the black box, serial diagnostics and the gibberish background feed
// keep speaking it, so logs stay ASCII-stable across languages. The
// names here are for pixels only -- adapted RU forms using common
// abbreviations, sized to fit the screens that show them.
#pragma once
#include "state.h"

namespace TypeNames {
// Short RU name for rows and counters (LOG, filter, ignore list,
// diary, desk card, toasts): one name per type, all caps.
const char* ru(DetectionType t);
// Shorter RU code for the bingo squares, mirroring shortName()'s
// EN abbreviations.
const char* cellRu(DetectionType t);
// Fuller RU name for the ALERT headline strip, mirroring
// targetLabel()'s EN "FLOCK CAM" style.
const char* headlineRu(DetectionType t);
// EN code or RU name by Settings::lang() (0 EN, 1 RU). Screen code
// calls this instead of detectionTypeName().
const char* display(DetectionType t);
}  // namespace TypeNames
