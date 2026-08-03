#include "IslandChainPlacement.h"

#include "Frame/IslandTerrain.h"
#include "Frame/Frame.h"

namespace engine
{

namespace
{

// Independent deterministic RNG streams, each seeded from the grid coord with a distinct odd 64-bit
// multiplier (SeedFromGridCoord mixes both x and y bits into the high half). Splitting crc / position /
// rotation / anchor / curve lets one knob be retuned without reshuffling the others — the anchor and macro
// chain heading stay stable in particular. Draw order within a stream is load-bearing.
inline constexpr uint64_t kCrcPickSeedMultiplier = 0xD1B54A32D192ED03ull;
inline constexpr uint64_t kPositionSeedMultiplier = 0xCBF29CE484222325ull;
inline constexpr uint64_t kRotationSeedMultiplier = 0x9E3779B97F4A7C15ull;
inline constexpr uint64_t kAnchorSeedMultiplier = 0xFF51AFD7ED558CCDull;
inline constexpr uint64_t kCurveSeedMultiplier = 0xC4CEB9FE1A85EC53ull;

// Anchor: large island centered at 1/3 of the cell from the SW (min) corner.
inline constexpr float kfAnchorFraction = 1.0f / 3.0f;
inline constexpr float kfAnchorJitterMeters = 25.0f;
inline constexpr float kfAnchorRotationJitter = 0.2f;
inline constexpr float kfCellEdgeMarginMeters = 10.0f;

// Contact growth. Every non-anchor island is placed just-touching an existing eligible island, leaving a
// small visual gap (very close, not perfectly touching). The big-island chain — Huge anchor, then 2 Large,
// then 3 Medium — grows along a curve (the heading turns hard each step so the chain reads as a clear arc),
// each link touching the chain tip, stopping as soon as a link no longer fits inside the frame. Smalls then
// ring each big island (touching a big island only, never another small), with a few extra trailing off
// the end of the curve.
inline constexpr float kfTouchGapMeters = 5.0f;
inline constexpr float kfChainHeadingStartJitter = 0.4f;
inline constexpr float kfChainHeadingTurn = 0.7f;
inline constexpr float kfChainHeadingTurnJitter = 0.25f;
inline constexpr float kfTouchDirJitter = 0.7f;
inline constexpr int64_t kiTouchAttempts = 32;

// Surround: ring every big island (Huge/Large/Medium) with smalls that touch ONLY a big island — never
// another small, so the ring can't grow into unbounded small-to-small chains. Per-island slot count scales
// with the island's perimeter; a slot that would overlap a neighbour is left empty. No fixed small total —
// it is however many fit around the big islands.
inline constexpr float kfSurroundSpacingMeters = 110.0f;
inline constexpr int64_t kiMinSurroundSlots = 6;
inline constexpr float kfSurroundDirJitter = 0.20f;

inline constexpr float kfTailDirJitter = 0.6f;

// A long island (footprint aspect >= this) aligns its long axis to the attach direction with small
// jitter; squarer islands get a full random rotation. The 2x1 / 3x1 Large strips align to the curve.
inline constexpr float kfLongAspectThreshold = 2.0f;
inline constexpr float kfTangentRotationJitter = 0.15f;

// Reserve size for the per-candidate world-vertex scratch.
inline constexpr int64_t kiHullScratchReserve = 256;

enum class Role
{
	kHuge,
	kLarge,
	kMedium,
	kSmall,
};

// Per-cell generation state threaded through the placement helpers.
struct CellContext
{
	float fCenterWorldX = 0.0f;
	float fCenterWorldY = 0.0f;
	float fHalfW = 0.0f;
	float fHalfH = 0.0f;

	common::RandomEngine crcRandom;
	common::RandomEngine positionRandom;
	common::RandomEngine rotationRandom;
	common::RandomEngine anchorRandom;
	common::RandomEngine curveRandom;

	std::vector<IslandPlacement>* pOut = nullptr;

	// Accepted world hulls: storage holds the rotated world verts (pointer-stable — reserved up front,
	// never reallocated), views are the ConvexHull2D the SAT test reads. scratch is reused per candidate.
	std::vector<std::vector<XMFLOAT2>> placedHullStorage;
	std::vector<common::ConvexHull2D> placedHullViews;
	std::vector<XMFLOAT2> scratch;
};

// Signed jitter in [-fRadius, +fRadius).
float SignedJitter(float fRadius, common::RandomEngine& rRandom)
{
	return common::Random(2.0f * fRadius, rRandom) - fRadius;
}

// The CRC bucket for a role, falling back through related buckets to mIslandCrcsSorted (asserted
// non-empty), so small asset sets still place something.
const std::vector<common::crc_t>& PickBucket(Role eRole)
{
	const IslandTerrain& rTerrain = *gpIslandTerrain;
	switch (eRole)
	{
		case Role::kHuge:
			if (!rTerrain.mHugeCrcs.empty()) { return rTerrain.mHugeCrcs; }
			if (!rTerrain.mLargeCrcs.empty()) { return rTerrain.mLargeCrcs; }
			if (!rTerrain.mMediumCrcs.empty()) { return rTerrain.mMediumCrcs; }
			return rTerrain.mIslandCrcsSorted;
		case Role::kLarge:
			if (!rTerrain.mLargeCrcs.empty()) { return rTerrain.mLargeCrcs; }
			if (!rTerrain.mMediumCrcs.empty()) { return rTerrain.mMediumCrcs; }
			if (!rTerrain.mHugeCrcs.empty()) { return rTerrain.mHugeCrcs; }
			return rTerrain.mIslandCrcsSorted;
		case Role::kMedium:
			if (!rTerrain.mMediumCrcs.empty()) { return rTerrain.mMediumCrcs; }
			if (!rTerrain.mLargeCrcs.empty()) { return rTerrain.mLargeCrcs; }
			if (!rTerrain.mSmallCrcs.empty()) { return rTerrain.mSmallCrcs; }
			return rTerrain.mIslandCrcsSorted;
		case Role::kSmall:
			if (!rTerrain.mSmallCrcs.empty()) { return rTerrain.mSmallCrcs; }
			if (!rTerrain.mMediumCrcs.empty()) { return rTerrain.mMediumCrcs; }
			return rTerrain.mIslandCrcsSorted;
	}
	return rTerrain.mIslandCrcsSorted;
}

// Random template CRC for a role. common::Random(N) is inclusive on N — pass size - 1.
common::crc_t PickCrc(Role eRole, common::RandomEngine& rCrcRandom)
{
	const std::vector<common::crc_t>& rBucket = PickBucket(eRole);
	return rBucket.at(common::Random(static_cast<uint32_t>(rBucket.size() - 1u), rCrcRandom));
}

// Orientation for a candidate. A long island points its LONGEST edge along fAttachWorld (the chain-curve
// direction) so consecutive links run nose-to-tail down the curve, plus small jitter; a squarer island
// gets a full random rotation. The extra quarter-turn (vs. naively pointing the major footprint axis at
// fAttachWorld) corrects an observed 90-degree offset between the major footprint axis and the rendered
// long edge — without it the elongated 2x1 / 3x1 strips sit across the curve instead of along it.
float OrientForTangent(common::crc_t crc, float fAttachWorld, common::RandomEngine& rRotationRandom)
{
	const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(crc);
	float fX = rTemplate.mfQuadFootprintX;
	float fY = rTemplate.mfQuadFootprintY;
	float fLong = std::max(fX, fY);
	float fShort = std::min(fX, fY);
	if (fShort <= 0.0f || fLong / fShort < kfLongAspectThreshold)
	{
		return common::Random(2.0f * DirectX::XM_PI, rRotationRandom);
	}

	float fBase = (fX >= fY) ? (fAttachWorld + 0.5f * DirectX::XM_PI) : fAttachWorld;
	return fBase + SignedJitter(kfTangentRotationJitter, rRotationRandom);
}

// The template's CCW island-local valid-area hull, or a synthesized 4-corner footprint rectangle (written
// into rRectStorage) when the hull is degenerate, so every island is testable. Sets rCount (int32_t to
// match the ConvexHull2D / BuildWorldHull geometry API).
const XMFLOAT2* LocalHull(const IslandTemplate& rTemplate, XMFLOAT2 (&rRectStorage)[4], int32_t& rCount)
{
	const XMFLOAT2* pLocalHull = rTemplate.mpf2ValidAreaVertices;
	int32_t iCount = rTemplate.miValidAreaVertexCount;
	if (pLocalHull == nullptr || iCount < 3)
	{
		float fHx = 0.5f * rTemplate.mfQuadFootprintX;
		float fHy = 0.5f * rTemplate.mfQuadFootprintY;
		rRectStorage[0] = {-fHx, -fHy};
		rRectStorage[1] = { fHx, -fHy};
		rRectStorage[2] = { fHx,  fHy};
		rRectStorage[3] = {-fHx,  fHy};
		pLocalHull = rRectStorage;
		iCount = 4;
	}
	rCount = iCount;
	return pLocalHull;
}

// Commit a candidate world hull as a new placement: copy its verts into pointer-stable storage, push the
// matching view and the IslandPlacement, and return the new placed index.
int64_t CommitPlacement(CellContext& rContext, common::crc_t crc, XMFLOAT2 f2World, float fRotation, const common::ConvexHull2D& rCandidate)
{
	// placedHullStorage MUST NOT reallocate — placedHullViews hold raw pointers into it (below). It is
	// reserved to kiMaxIslandsPerCell; a future constant change that lets placement exceed the reserve
	// would dangle every view and corrupt the SAT overlap test → non-deterministic packing → CRC desync.
	ASSERT(rContext.placedHullStorage.size() < rContext.placedHullStorage.capacity());
	rContext.placedHullStorage.emplace_back(rContext.scratch.begin(), rContext.scratch.end());
	common::ConvexHull2D view = rCandidate;
	view.pVertices = rContext.placedHullStorage.back().data();
	rContext.placedHullViews.push_back(view);
	rContext.pOut->push_back({.islandCrc = crc, .f2WorldPos = f2World, .fRotation = fRotation});
	return static_cast<int64_t>(rContext.placedHullViews.size()) - 1;
}

// Place the large anchor at the clamped target (output index 0). The center is clamped so the rotation-
// expanded footprint rectangle stays inside the cell; returns false only if the island cannot fit at all.
// Returns the accepted cell-local position via rAcceptedLocalOut.
bool PlaceAnchor(CellContext& rContext, common::crc_t crc, float fTargetLocalX, float fTargetLocalY, float fRotation, XMFLOAT2& rAcceptedLocalOut)
{
	const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(crc);
	XMFLOAT2 rectHull[4] {};
	int32_t iLocalCount = 0;
	const XMFLOAT2* pLocalHull = LocalHull(rTemplate, rectHull, iLocalCount);

	common::SinCos rotation = common::DeterministicSinCos(fRotation);
	float fAbsCos = std::abs(rotation.fCos);
	float fAbsSin = std::abs(rotation.fSin);
	float fRotatedHalfW = 0.5f * (rTemplate.mfQuadFootprintX * fAbsCos + rTemplate.mfQuadFootprintY * fAbsSin);
	float fRotatedHalfH = 0.5f * (rTemplate.mfQuadFootprintX * fAbsSin + rTemplate.mfQuadFootprintY * fAbsCos);
	float fLimitX = rContext.fHalfW - fRotatedHalfW - kfCellEdgeMarginMeters;
	float fLimitY = rContext.fHalfH - fRotatedHalfH - kfCellEdgeMarginMeters;
	if (fLimitX < 0.0f || fLimitY < 0.0f)
	{
		return false;
	}

	float fLocalX = std::clamp(fTargetLocalX, -fLimitX, fLimitX);
	float fLocalY = std::clamp(fTargetLocalY, -fLimitY, fLimitY);
	XMFLOAT2 f2World {rContext.fCenterWorldX + fLocalX, rContext.fCenterWorldY + fLocalY};

	rContext.scratch.resize(static_cast<size_t>(iLocalCount));
	common::ConvexHull2D candidate = common::BuildWorldHull(pLocalHull, iLocalCount, f2World, rotation.fCos, rotation.fSin, rContext.scratch.data());
	CommitPlacement(rContext, crc, f2World, fRotation, candidate);
	rAcceptedLocalOut = {fLocalX, fLocalY};
	return true;
}

// One contact-placement attempt: position `crc` just-touching the host island (placed index iHost) along
// fDir, leaving kfTouchGapMeters between hulls, oriented fRotation. Rejects if the resulting pose leaves
// the cell or its hull overlaps any already-placed hull. On success commits the placement and returns the
// new placed index via rPlacedIndexOut.
bool TryTouchPlace(CellContext& rContext, common::crc_t crc, int64_t iHost, float fDir, float fRotation, int64_t& rPlacedIndexOut)
{
	const IslandTemplate& rTemplate = gpIslandTerrain->mIslands.at(crc);
	XMFLOAT2 rectHull[4] {};
	int32_t iLocalCount = 0;
	const XMFLOAT2* pLocalHull = LocalHull(rTemplate, rectHull, iLocalCount);

	common::SinCos direction = common::DeterministicSinCos(fDir);
	float fDirX = direction.fCos;
	float fDirY = direction.fSin;

	// Candidate's near-edge projection onto fDir, relative to its own center (rotation applied).
	common::SinCos rotation = common::DeterministicSinCos(fRotation);
	float fCos = rotation.fCos;
	float fSin = rotation.fSin;
	float fCandMin = std::numeric_limits<float>::max();
	for (int32_t i = 0; i < iLocalCount; ++i)
	{
		float fRx = pLocalHull[i].x * fCos - pLocalHull[i].y * fSin;
		float fRy = pLocalHull[i].x * fSin + pLocalHull[i].y * fCos;
		fCandMin = std::min(fCandMin, fRx * fDirX + fRy * fDirY);
	}

	// Host's far-edge projection onto fDir.
	const common::ConvexHull2D& rHost = rContext.placedHullViews.at(static_cast<size_t>(iHost));
	float fHostMax = std::numeric_limits<float>::lowest();
	for (int32_t i = 0; i < rHost.iVertexCount; ++i)
	{
		fHostMax = std::max(fHostMax, rHost.pVertices[i].x * fDirX + rHost.pVertices[i].y * fDirY);
	}

	// Slide the candidate center along fDir so its near edge sits kfTouchGapMeters beyond the host's far
	// edge — the two hulls just clear each other along fDir (separating axis), leaving the small gap.
	XMFLOAT2 f2Host = rContext.pOut->at(static_cast<size_t>(iHost)).f2WorldPos;
	float fS = fHostMax + kfTouchGapMeters - fCandMin - (f2Host.x * fDirX + f2Host.y * fDirY);
	XMFLOAT2 f2World {f2Host.x + fS * fDirX, f2Host.y + fS * fDirY};

	rContext.scratch.resize(static_cast<size_t>(iLocalCount));
	common::ConvexHull2D candidate = common::BuildWorldHull(pLocalHull, iLocalCount, f2World, fCos, fSin, rContext.scratch.data());

	// Keep the whole cluster inside the cell (the touch pose is not clamped — that would break contact).
	float fLimitX = rContext.fHalfW - kfCellEdgeMarginMeters;
	float fLimitY = rContext.fHalfH - kfCellEdgeMarginMeters;
	if (candidate.f2AabbMin.x < rContext.fCenterWorldX - fLimitX || candidate.f2AabbMax.x > rContext.fCenterWorldX + fLimitX
	 || candidate.f2AabbMin.y < rContext.fCenterWorldY - fLimitY || candidate.f2AabbMax.y > rContext.fCenterWorldY + fLimitY)
	{
		return false;
	}

	for (const common::ConvexHull2D& rPlaced : rContext.placedHullViews)
	{
		if (common::ConvexHullsOverlap(candidate, rPlaced))
		{
			return false;
		}
	}

	rPlacedIndexOut = CommitPlacement(rContext, crc, f2World, fRotation, candidate);
	return true;
}

} // anonymous namespace

void GenerateIslandChain(GridCoord coord, std::vector<IslandPlacement>& rOut)
{
	rOut.clear();

	const float fCellW = game::Frame::kfCellWidth;
	const float fCellH = game::Frame::kfCellHeight;
	const float fCellOriginX = game::Frame::kfBaseAreaMinX + static_cast<float>(coord.x) * fCellW;
	const float fCellMaxY = game::Frame::kfBaseAreaMaxY + static_cast<float>(coord.y) * fCellH;

	CellContext context;
	context.fCenterWorldX = fCellOriginX + 0.5f * fCellW;
	context.fCenterWorldY = fCellMaxY - 0.5f * fCellH;
	context.fHalfW = 0.5f * fCellW;
	context.fHalfH = 0.5f * fCellH;
	context.crcRandom = common::RandomEngine(game::SeedFromGridCoord(coord, kCrcPickSeedMultiplier));
	context.positionRandom = common::RandomEngine(game::SeedFromGridCoord(coord, kPositionSeedMultiplier));
	context.rotationRandom = common::RandomEngine(game::SeedFromGridCoord(coord, kRotationSeedMultiplier));
	context.anchorRandom = common::RandomEngine(game::SeedFromGridCoord(coord, kAnchorSeedMultiplier));
	context.curveRandom = common::RandomEngine(game::SeedFromGridCoord(coord, kCurveSeedMultiplier));
	context.pOut = &rOut;
	context.placedHullStorage.reserve(static_cast<size_t>(kiMaxIslandsPerCell));
	context.placedHullViews.reserve(static_cast<size_t>(kiMaxIslandsPerCell));
	context.scratch.reserve(static_cast<size_t>(kiHullScratchReserve));
	rOut.reserve(static_cast<size_t>(kiMaxIslandsPerCell));

	// 1. ANCHOR (output index 0) — Huge island in the SW third, always placed.
	common::crc_t anchorCrc = PickCrc(Role::kHuge, context.crcRandom);
	float fAnchorTargetX = -context.fHalfW + kfAnchorFraction * fCellW + SignedJitter(kfAnchorJitterMeters, context.anchorRandom);
	float fAnchorTargetY = -context.fHalfH + kfAnchorFraction * fCellH + SignedJitter(kfAnchorJitterMeters, context.anchorRandom);
	float fAnchorRotation = SignedJitter(kfAnchorRotationJitter, context.anchorRandom);
	XMFLOAT2 f2AnchorLocal {};
	bool bAnchorPlaced = PlaceAnchor(context, anchorCrc, fAnchorTargetX, fAnchorTargetY, fAnchorRotation, f2AnchorLocal);
	// Output index 0 must be the anchor (SpaceshipsNavigation / PlayersNavigation index islands.at(0)).
	// PlaceAnchor only fails if the island can't fit the cell at all — impossible for current assets at the
	// current cell size, but assert so a future oversized island fails fast instead of silently shifting index 0.
	ASSERT(bAnchorPlaced);

	// 2. BIG-ISLAND CHAIN — 2 Large then 3 Medium, contact-linked along a hard-turning curve from the Huge
	// anchor (each link touches the chain tip — the anchor or the previous link). Stop as soon as a link
	// cannot fit inside the frame, so the chain extends as far down the curve as the cell allows.
	const Role kChainRoles[] = {Role::kLarge, Role::kLarge, Role::kMedium, Role::kMedium, Role::kMedium};
	float fHeading = 0.25f * DirectX::XM_PI + SignedJitter(kfChainHeadingStartJitter, context.anchorRandom);
	int64_t iTipIndex = 0;
	for (Role eRole : kChainRoles)
	{
		common::crc_t crc = PickCrc(eRole, context.crcRandom);
		bool bPlaced = false;
		for (int64_t iAttempt = 0; iAttempt < kiTouchAttempts; ++iAttempt)
		{
			float fDir = fHeading + (iAttempt == 0 ? 0.0f : SignedJitter(kfTouchDirJitter, context.positionRandom));
			float fRotation = OrientForTangent(crc, fDir, context.rotationRandom);
			int64_t iPlaced = 0;
			if (TryTouchPlace(context, crc, iTipIndex, fDir, fRotation, iPlaced))
			{
				iTipIndex = iPlaced;
				bPlaced = true;
				break;
			}
		}
		if (!bPlaced)
		{
			break;
		}
		fHeading += kfChainHeadingTurn + SignedJitter(kfChainHeadingTurnJitter, context.curveRandom);
	}

	// 3. SURROUND — ring every big island (the anchor + chain links, indices [0, iBigIslandCount)) with
	// smalls that touch ONLY that big island, never another small. Evenly-spaced angular slots (count scales
	// with the island's perimeter, + jitter); a slot whose pose overlaps a neighbour is simply left empty.
	int64_t iBigIslandCount = static_cast<int64_t>(context.placedHullViews.size());
	for (int64_t iBig = 0; iBig < iBigIslandCount; ++iBig)
	{
		const IslandTemplate& rBig = gpIslandTerrain->mIslands.at(context.pOut->at(static_cast<size_t>(iBig)).islandCrc);
		float fPerimeter = 2.0f * (rBig.mfQuadFootprintX + rBig.mfQuadFootprintY);
		int64_t iSlots = std::clamp(static_cast<int64_t>(fPerimeter / kfSurroundSpacingMeters), kiMinSurroundSlots, kiMaxSurroundSlots);
		for (int64_t iSlot = 0; iSlot < iSlots; ++iSlot)
		{
			float fDir = (2.0f * DirectX::XM_PI * static_cast<float>(iSlot)) / static_cast<float>(iSlots) + SignedJitter(kfSurroundDirJitter, context.positionRandom);
			common::crc_t crc = PickCrc(Role::kSmall, context.crcRandom);
			float fRotation = common::Random(2.0f * DirectX::XM_PI, context.rotationRandom);
			int64_t iPlaced = 0;
			TryTouchPlace(context, crc, iBig, fDir, fRotation, iPlaced);
		}
	}

	// 4. TAIL — a few extra smalls trailing off the end of the curve, attached to the chain tip (a big
	// island), biased toward the forward heading so the chain peters out into islets.
	for (int64_t iTail = 0; iTail < kiTailSmallCount; ++iTail)
	{
		float fDir = fHeading + SignedJitter(kfTailDirJitter, context.positionRandom);
		common::crc_t crc = PickCrc(Role::kSmall, context.crcRandom);
		float fRotation = common::Random(2.0f * DirectX::XM_PI, context.rotationRandom);
		int64_t iPlaced = 0;
		TryTouchPlace(context, crc, iTipIndex, fDir, fRotation, iPlaced);
	}

	ASSERT(!rOut.empty());
}

} // namespace engine
