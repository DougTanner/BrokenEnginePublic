// 32-bit GLSL analogue of common::RandomEngine — same xorshift family + splitmix-style seed init,
// but width and shift triple necessarily differ from the 64-bit C++ version (Common/Random.h).
// 32-bit was chosen because GLSL uint is universally supported; uint64 needs GL_ARB_gpu_shader_int64.
struct RandomEngine
{
	uint uiState;
};

uint RandomNext(inout RandomEngine r)
{
	uint x = r.uiState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	r.uiState = x;
	return x;
}

float Random01(inout RandomEngine r)
{
	return float(RandomNext(r)) * (1.0 / 4294967296.0);
}

// splitmix32 — mirrors splitmix64 init in Common/Random.cpp:6-16.
RandomEngine SeedRandomEngine(uint uiSeed)
{
	uint x = uiSeed + 0x9e3779b9u;
	x = (x ^ (x >> 16)) * 0x7feb352du;
	x = (x ^ (x >> 15)) * 0x846ca68bu;
	x ^= x >> 16;
	if (x == 0u) x = 1u;
	RandomEngine r;
	r.uiState = x;
	return r;
}
