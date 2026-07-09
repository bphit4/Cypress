#include <CFB27/PatternScanner.h>

namespace Cypress::CFB27
{
	std::ptrdiff_t FindUniquePattern(
		const std::uint8_t* data,
		const std::size_t dataSize,
		const std::vector<int>& pattern)
	{
		if (!data || pattern.empty() || pattern.size() > dataSize)
			return PatternNotFound;

		std::ptrdiff_t match = PatternNotFound;
		for (std::size_t offset = 0; offset <= dataSize - pattern.size(); ++offset)
		{
			bool matches = true;
			for (std::size_t index = 0; index < pattern.size(); ++index)
			{
				if (pattern[index] >= 0 && data[offset + index] != pattern[index])
				{
					matches = false;
					break;
				}
			}
			if (!matches)
				continue;
			if (match != PatternNotFound)
				return PatternAmbiguous;
			match = static_cast<std::ptrdiff_t>(offset);
		}
		return match;
	}
}
