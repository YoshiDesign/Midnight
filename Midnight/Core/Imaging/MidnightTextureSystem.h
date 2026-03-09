#pragma once
#include <cstdint>

#include "CoreVK/Resources/MTexture.h"

/* Primary Vulkan/Backend resource orchestrator */

namespace aveng {

	struct VkRenderData;
	class EngineDevice;

	/* Owned by Midnight */
	struct MidnightTextureSystem {

		explicit MidnightTextureSystem(EngineDevice& _engineDevice, VkRenderData _renderData);

		TextureHandle createTexture(const TextureCreateRequest& req, const int frameIndex);
		const TextureSlot* getSlot(TextureHandle handle) const;
		TextureSlot* getSlot(TextureHandle handle);

		uint32_t allocateBindlessSlot();
		VkSampler getOrCreateSampler(const MSamplerInfo& desc);

		void uploadTexturePixels(const TextureCreateRequest& req, TextureSlot& rec);
		void updateBindlessDescriptor(uint32_t bindlessIndex, const TextureSlot& rec, const int frameIndex);

		EngineDevice& engineDevice_;
		VkRenderData& renderData_;
		std::vector<TextureSlot> m_records;
		uint32_t m_nextHandle = 1;
		uint32_t m_nextBindlessSlot = 0;

	};

}