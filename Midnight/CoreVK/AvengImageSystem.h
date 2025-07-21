#pragma once
#include "EngineDevice.h"
#include "AMD/vk_mem_alloc.h"
#include "aveng_buffer.h"

#include <iostream>
#include <unordered_map>
#include <vector>
#include <memory>

/**
	All of the helper functions that submit commands so far have been set up to execute synchronously
	by waiting for the queue to become idle. For practical applications it is recommended to combine
	these operations in a single command buffer and execute them asynchronously for higher throughput,
	especially the transitions and copy in the createTextureImage function. Try to experiment with this
	by creating a setupCommandBuffer that the helper functions record commands into, and add a
	flushSetupCommands to execute the commands that have been recorded so far
*/
namespace aveng {

	class ImageSystem {

	public:

		ImageSystem(EngineDevice& device);
		ImageSystem(EngineDevice& device, const std::vector<std::string>& texturePaths);
		~ImageSystem();

		// Not copyable or movable
		ImageSystem(const ImageSystem&) = delete;
		ImageSystem& operator=(const ImageSystem&) = delete;
		ImageSystem(ImageSystem&&) = delete;
		ImageSystem& operator=(ImageSystem&&) = delete;

		void createTextureImage(const char* filepath, size_t i);
		VkImageView createImageView(VkImage image, VkFormat format, uint32_t mipLevels);
		void createTextureImageView(VkImage image, size_t i);
		void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
		void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
		void createTextureSampler();
		void createImageDescriptors(std::vector<VkImageView> views);

		VkDescriptorImageInfo getImageInfoAtIndex(int index)    { return imageInfosArray[index]; }
		std::vector<VkDescriptorImageInfo> descriptorInfoForAllImages(){ return imageInfosArray; }
		std::vector<const char*> texture_paths;

		// Dynamic texture management
		void addTexture(const std::string& texturePath);
		size_t getTextureCount() const { return images.size(); }

	private:

		// mipLevels is one dependency keeping us from merging some of these functions with SwapChain's impelmentations of them
		void initializeWithPaths(const std::vector<std::string>& texturePaths);
		
		// Batched command submission optimization
		VkCommandBuffer setupCommandBuffer = VK_NULL_HANDLE;
		std::vector<std::unique_ptr<AvengBuffer>> stagingBuffers; // Keep staging buffers alive
		void beginSetupCommands();
		void flushSetupCommands();
		
		// Batch-friendly versions of helper functions
		void createTextureImageBatched(const char* filepath, size_t i);
		void transitionImageLayoutBatched(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
		void generateMipmapsBatched(VkImage _image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t _mipLevels);

		EngineDevice& engineDevice;
		VkSampler textureSampler;
		std::vector<VkImage> images;
		std::vector<uint32_t> mipLevels;
		std::vector<VkImageView> textureImageViews;
		std::vector<VmaAllocation> allImageAllocations;
		std::vector<VkDescriptorImageInfo> imageInfosArray;
		
		//std::unordered_map<std::string, Texture> textures;

	};

} 