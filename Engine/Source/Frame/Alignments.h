#pragma once

namespace engine
{

// Simple alignment identifier (0 = invalid)
struct alignment_t
{
	uint32_t uiValue = 0;

	constexpr alignment_t() = default;
	constexpr explicit alignment_t(uint32_t uiValue) : uiValue(uiValue) {}

	constexpr bool IsValid() const { return uiValue != 0; }
	constexpr uint32_t Value() const { return uiValue; }

	constexpr bool operator==(const alignment_t&) const = default;
	constexpr auto operator<=>(const alignment_t&) const = default;

	void Write(std::ostream& rStream) const { common::Write(rStream, uiValue); }
	void Read(std::istream& rStream) { common::Read(rStream, uiValue); }
};

inline constexpr alignment_t kInvalidAlignment {};

namespace AlignmentFlags
{
	inline constexpr uint8_t kNone = 0x00;
	inline constexpr uint8_t kEnemies = 0x01;
	inline constexpr uint8_t kAllies = 0x02;
}

// Sparse map for alignment collision filtering
// Keys are two 32-bit alignment IDs concatenated (lower ID first)
// Values are alignment flags
struct Alignments
{
	std::unordered_map<uint64_t, uint8_t> alignmentPairs;

	void AddAlignment(alignment_t idA, alignment_t idB, uint8_t flags);
	void RemoveAlignment(alignment_t idA, alignment_t idB);
	bool CanCollide(alignment_t idA, alignment_t idB) const;

	bool operator==(const Alignments& rOther) const;
	common::crc_t Crc() const;
	void Write(std::ostream& rStream) const;
	void Read(std::istream& rStream);

private:

	static uint64_t MakeAlignmentKey(alignment_t idA, alignment_t idB);
};

} // namespace engine
