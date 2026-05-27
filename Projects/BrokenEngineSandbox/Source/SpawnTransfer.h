#pragma once

#include "Frame/StatusChange.h"

namespace game
{

struct Frame;

void SpawnTransfer(Frame& rFrame, StatusChangeType eType, const TransferData& rData, engine::alignment_t playerAlignment);

} // namespace game
