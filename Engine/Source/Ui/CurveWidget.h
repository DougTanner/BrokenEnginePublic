#pragma once

#if defined(BT_CLIENT)

#include "CurveData.h"

namespace engine
{

// Photoshop-style curve editor. Returns true while the user is actively
// interacting (dragging, adding, or deleting a point) on this frame.
bool CurveWidget(std::string_view label, CurveData& rCurve);

} // namespace engine

#endif // BT_CLIENT
