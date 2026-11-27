#pragma once

#include <algorithm>

namespace MenuScale
{
	inline float Calculate(int nScreenW, int nScreenH)
	{
		if (nScreenW <= 0 || nScreenH <= 0)
			return 1.0f;
		return std::max(0.01f, std::min(nScreenW / 1600.0f, nScreenH / 900.0f));
	}
}
