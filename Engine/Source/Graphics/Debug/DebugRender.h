#pragma once

#if defined(BT_CLIENT)

namespace engine
{

class DebugRender
{
public:

	static void Box(const XMFLOAT3A& f3Position, const XMFLOAT3A& f3Scale, const XMFLOAT4A& f4Color);
	static void Sphere(const XMFLOAT3A& f3Center, float fRadius, const XMFLOAT4A& f4Color);
	static void Circle(const XMFLOAT3A& f3Center, float fRadius, const XMFLOAT4A& f4Color);
	static void Line(const XMFLOAT3A& f3Start, const XMFLOAT3A& f3End, const XMFLOAT4A& f4Color);

	static void Toggle();

	static void BeginRender(int64_t iCommandBuffer);
	static void EndRender(int64_t iCommandBuffer);
};

} // namespace engine

#endif // BT_CLIENT
