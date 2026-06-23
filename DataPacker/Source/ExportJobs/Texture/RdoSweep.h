#pragma once

// BC7 RDO knob-sweep diagnostics. Three CLI modes (--rdo-sweep / --rdo-sweep-full /
// --rdo-sweep-validate, dispatched from Main.cpp's runOnce) profile the Texture encoder's
// lambda / lookback / uber knobs and emit a Pareto frontier — used only when retuning the
// constants atop Texture.cpp. Each takes exactly one image/intermediate path. Lives beside
// Texture.{h,cpp} because the sweep reaches into Texture::sEncodeMutex and Texture::EncodeWithRdo.
int RunRdoSweep(const std::filesystem::path& rPath);
int RunRdoSweepFull(const std::filesystem::path& rPath);
int RunRdoSweepValidate(const std::filesystem::path& rPath);
