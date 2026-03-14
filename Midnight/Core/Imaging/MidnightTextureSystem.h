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

		explicit MidnightTextureSystem(EngineDevice& _engineDevice, VkRenderData& _renderData);
		~MidnightTextureSystem() = default;
		
		void cleanup();
		TextureHandle createTexture(ITextureSource& source, TextureCreateRequest& req, size_t next_descriptor_index);
		const TextureSlot* getSlot(TextureHandle handle) const;
		TextureSlot* getSlot(TextureHandle handle);

		bool uploadToGPU(TextureSlot& slot, ITextureSource& source, unsigned char* pixelBlob);
		void getOrCreateSampler(const MSamplerInfo& desc, TextureSlot& slot);

		void updateBindlessDescriptor(uint32_t bindlessIndex, const TextureSlot& slot, const int frameIndex);

		EngineDevice& engineDevice_;
		VkRenderData& renderData_;
		std::vector<TextureSlot> m_records; // Complete slots parallel to descriptor indices
		uint32_t m_nextHandle = 1;
		// uint32_t m_nextBindlessSlot = 0;

	};

}