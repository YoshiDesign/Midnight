#pragma once
#include <functional>

namespace aveng {

	// from: https://stackoverflow.com/a/57595105
	template <typename T, typename... Rest>
	void hashCombine(std::size_t& seed, const T& v, const Rest&... rest) {
		seed ^= std::hash<T>{}(v)+0x9e3779b9 + (seed << 6) + (seed >> 2);
		(hashCombine(seed, rest), ...);
	};



	//size_t pad_uniform_buffer_size(size_t originalSize, VkDeviceSize minUniformBufferOffsetAlignment)
	//{
	//	// Calculate required alignment based on minimum device offset alignment
	//	size_t minUboAlignment = minUniformBufferOffsetAlignment;
	//	size_t alignedSize = originalSize;
	//	if (minUboAlignment > 0) {
	//		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	//	}
	//	return alignedSize;
	//}

}