#pragma once
#include <functional>
#include <type_traits>
#include "CoreVK/VkRenderData.h"

namespace aveng {

	// from: https://stackoverflow.com/a/57595105
	template <typename T, typename... Rest>
	void hashCombine(std::size_t& seed, const T& v, const Rest&... rest) {
		seed ^= std::hash<T>{}(v)+0x9e3779b9 + (seed << 6) + (seed >> 2);
		(hashCombine(seed, rest), ...);
	};

	// Use this in dev to determine what can safely utilize low-lvl memory copy/update functions like memcpy, std::copy, etc.
	template <typename T>
	bool isTrivialToCopy(T thing)
	{
		static_assert(std::is_trivially_copyable_v<glm::vec4>, "glm::vec4 must be trivially copyable");
		static_assert(std::is_trivially_copyable_v<NodeTransformData>, "NodeTransformData must be trivially copyable");
		static_assert(alignof(NodeTransformData) % alignof(glm::vec4) == 0, "Alignment must be vec4-friendly");
		static_assert(sizeof(NodeTransformData) == 3 * sizeof(glm::vec4), "Expect 48 bytes (std430-friendly)");
		return true;
	}



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