#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Cypress::CFB27
{
	inline constexpr std::ptrdiff_t PatternNotFound = -1;
	inline constexpr std::ptrdiff_t PatternAmbiguous = -2;

	std::ptrdiff_t FindUniquePattern(
		const std::uint8_t* data,
		std::size_t dataSize,
		const std::vector<int>& pattern);
}
