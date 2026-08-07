# Engine/Data/Shaders/Shadow - Top-Down Shadow Compute Pipelines

## Overview

Two independent compute chains post-process top-down (orthographic, world-XY-mapped) shadow data for the RTS camera. The terrain chain ray-marches sun occlusion, runs horizontal and vertical Gaussian blur, temporally resolves the final result, then refreshes distinct history storage. The object chain blurs its top-down object-coverage raster separately. Terrain and water consume both final textures; models consume only terrain shadow.

## Terrain Shadow Contract

- Inverse coverage is load-bearing: terrain shadow stores `1 - shadow` (1.0 = fully lit), so clamp-to-border-white sampling outside texture coverage degrades to no shadow rather than an artifact.
- Full-texture work: every terrain pass processes its whole texture. Group counts are resolved at record time from the texture extent, and each invocation rejects the tile-rounding tail against `imageSize` of the image it writes.
- Record-once dispatching: the five terrain passes—source, horizontal blur, vertical blur, temporal resolve, and history copy—remain in the recorded Global command buffer. Every extent change re-records that buffer, so the recorded group counts stay current.
- Consumer validity: terrain-shadow consumers read the final texture directly through the border-white sampler, which degrades to no shadow outside coverage.
- Temporal validity: temporal reprojection samples history only when the reprojected coordinate stays inside `[0, 1]`; disocclusions and reset history use current output. A separate history-copy pass transfers the result between distinct storage images after temporal resolve, avoiding same-image read/write aliasing.
- Sun direction is loop parameters, not branches: morning versus evening flips the march increment, start offset, and width-scale sign. The ray-march is 1-D along texture rows because the sun azimuth is east/west; the wider, sunward-extended elevation raster admits off-screen occluders.
- Shadow shaping: per-occluder shadow is feathered by sun-versus-terrain angle and distance falloff, then max-accumulated. An elevation height-fade band limits low-beach noise at dawn and dusk, while the moon multiplier fades terrain shadow with the lighting envelope.
- Terrain and object blur differ: terrain radius is a compile-time constant, while object blur receives its radius and sigma from runtime settings. Object horizontal pairing requires matching logical tap spacing and sampler grids; when its independently scaled input differs, it uses discrete output-grid taps. Both object inputs remain linear, clamp-to-edge; horizontal blur inverts coverage and vertical blur restores multiply-with-lighting form.
