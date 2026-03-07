#pragma once

namespace engine
{

void HandleException(std::optional<const std::exception*> pException = std::nullopt);
void ReadDxDiag();

} // namespace engine
