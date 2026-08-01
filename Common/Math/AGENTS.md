# Common Math

Deterministic math, random streams, vector helpers, and 2D convex-hull operations shared by offline and runtime code.

## Deterministic Math

- Simulation or CRC-fed code uses `DeterministicSinCos`, `ExponentialDecay`, and related helpers instead of library transcendental functions. Respect each helper's documented input domain; clamp the result at the caller when the domain is wider.
- `ValidateVector<IS_POSITION>` checks finiteness and the repository W-lane invariant at collection boundaries. Build position, direction, velocity, normal, and offset vectors with the correct W from the start.
- DirectXMath arithmetic uses named functions, not overloaded operators. Preserve the SSE4-only and MXCSR rules owned by the Common hub.

## Random Streams

`RandomEngine` is the deterministic gameplay RNG (random-number generator). All gameplay and simulation randomness flows through it — never `std::random`. `<random>` compiles here because it is a PCH include, but its distributions and `random_device` produce implementation-defined values, so a single standard-library draw in simulation code breaks bit-determinism, desyncs the per-tick CRC, and shows up only as a rare replay divergence. Every draw advances the stream once; float draws are half-open and bounded integer draws include the upper bound. Seed independent streams explicitly. `TimeSeed()` is non-deterministic and must not seed shared simulation state unless that seed is distributed as deterministic input.

The state is never zero, and that is an enforced invariant rather than a convention: zero jams the xorshift step, so every later draw would be zero for the rest of the run, and identifiers built from those draws would collide with whatever absent-value sentinel they use. The state is private, and a value arriving from a save, replay, or network stream installs only through `SetSerializedState`, which treats zero as corrupt input and refuses the stream instead of repairing it — repairing would diverge from the recording. Reading a whole `RandomEngine` out of a stream is a deliberate compile error, so read the value as a `uint64_t` and pass it to that setter.

Adding or removing a draw changes every later result. Keep random draws and mode-transition draws outside throttled or client-only branches unless divergent consumption is intentional and isolated from shared state.

## Convex Hulls

`ConvexHull2D`, `BuildWorldHull`, and `ConvexHullsOverlap` provide deterministic island-placement geometry. Runtime SAT assumes convex, counter-clockwise hulls; producers must preserve those properties and validate them before serialization. Placement math that feeds CRC state uses deterministic trig.

## See Also

- `../../Documents/FloatingPointDeterminism.txt` - full floating-point contract
