#pragma once
#include <cstdint>

#include "CoreVK/Resources/MTexture.h"
#include "Core/Imaging/ITextureSource.h"

/* Primary Vulkan/Backend resource orchestrator */

namespace aveng {

	struct VkRenderData;
	class EngineDevice;

	/* Owned by Midnight */
	struct MidnightTextureSystem {

		explicit MidnightTextureSystem(EngineDevice& _engineDevice, VkRenderData _renderData);

		TextureHandle createTexture(ITextureSource& source, TextureCreateRequest& req, uint32_t next_descriptor_index, const int frameIndex);
		const TextureSlot* getSlot(TextureHandle handle) const;
		TextureSlot* getSlot(TextureHandle handle);

		bool uploadToGPU(TextureSlot& slot, ITextureSource& source, unsigned char* pixelBlob);
		void getOrCreateSampler(const MSamplerInfo& desc, TextureSlot& slot);

		void updateBindlessDescriptor(uint32_t bindlessIndex, const TextureSlot& slot, const int frameIndex);

		EngineDevice& engineDevice_;
		VkRenderData& renderData_;
		std::vector<TextureSlot> m_records;
		uint32_t m_nextHandle = 1;
		uint32_t m_nextBindlessSlot = 0;

	};

}