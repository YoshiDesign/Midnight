#include "AvengImageSystem.h"
#include "../Core/aveng_model.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#include "../../vendor/stb/stb_image.h"

/*
* This class is not responsible for rendering. Here we are creating our VkImageSamplers and ImageViews
* Along with ImageDescriptorInfo, TextureSamplers and the like. This enables the Sampler in our frag shader
* 
* A word on ImageDescriptors: This class only creates the VkDescriptorImageInfo structs. You'll still need
* to manually write those descriptors using the AvengDescriptorSetWriter
* 
* From the Vulkan Tutorial:
	All of the helper functions that submit commands so far have been set up to execute synchronously 
	by waiting for the queue to become idle. For practical applications it is recommended to combine 
	these operations in a single command buffer and execute them asynchronously for higher throughput, 
	especially the transitions and copy in the createTextureImage function. Try to experiment with this 
	by creating a setupCommandBuffer that the helper functions record commands into, and add a 
	flushSetupCommands to execute the commands that have been recorded so far
*/

namespace aveng {

	ImageSystem::ImageSystem(EngineDevice& device) : engineDevice{ device }
	{
		// Default constructor creates empty ImageSystem
		// Textures must be provided via the vector constructor or addTexture()
		std::vector<std::string> emptyTextures;
		initializeWithPaths(emptyTextures);
	}

	ImageSystem::ImageSystem(EngineDevice& device, const std::vector<std::string>& texturePaths) 
		: engineDevice{ device }
	{
		initializeWithPaths(texturePaths);
	}

	void ImageSystem::initializeWithPaths(const std::vector<std::string>& texturePaths)
	{
		// Store texture paths
		texture_paths.clear();
		for (const auto& path : texturePaths) {
			std::cout << "Loading Texture: " << path << std::endl;
			texture_paths.push_back(path.c_str());
		}

		if (!texture_paths.empty()) {
			std::cout << "Using batched command submission for " << texture_paths.size() << " textures" << std::endl;
			
			// Begin batched command recording
			beginSetupCommands();
			
			// Create images and views using batched approach
			for (size_t i = 0; i < texture_paths.size(); i++)
			{
				createTextureImageBatched(texture_paths[i], i);
				createTextureImageView(images[i], i);
			}
			
			// Submit all commands at once
			flushSetupCommands();
			
			std::cout << "Batch texture loading complete!" << std::endl;
		}
		
		// Create sampler and descriptors
		createTextureSampler();
		createImageDescriptors(textureImageViews);
	}

	void ImageSystem::addTexture(const std::string& texturePath)
	{
		std::cout << "Adding dynamic texture: " << texturePath << std::endl;
		
		size_t index = images.size();
		texture_paths.push_back(texturePath.c_str());
		
		// Use batched approach for dynamic texture addition
		beginSetupCommands();
		createTextureImageBatched(texturePath.c_str(), index);
		createTextureImageView(images[index], index);
		flushSetupCommands();
		
		// Rebuild descriptor array with new texture count
		imageInfosArray.clear();
		createImageDescriptors(textureImageViews);
		
		std::cout << "Total textures now: " << getTextureCount() << std::endl;
		std::cout << "Note: Pipeline will need to be recreated with new texture count for runtime additions" << std::endl;
	}

	void ImageSystem::beginSetupCommands()
	{
		if (setupCommandBuffer != VK_NULL_HANDLE) {
			throw std::runtime_error("Setup command buffer already active!");
		}
		
		setupCommandBuffer = engineDevice.beginSingleTimeCommands();
		stagingBuffers.clear(); // Clear any previous staging buffers
		std::cout << "Started batched command recording" << std::endl;
	}

	void ImageSystem::flushSetupCommands()
	{
		if (setupCommandBuffer == VK_NULL_HANDLE) {
			throw std::runtime_error("No setup command buffer to flush!");
		}
		
		std::cout << "Submitting batched commands..." << std::endl;
		engineDevice.endSingleTimeCommands(setupCommandBuffer);
		setupCommandBuffer = VK_NULL_HANDLE;
		
		// Now it's safe to destroy staging buffers - commands have been executed
		stagingBuffers.clear();
		std::cout << "Batched commands completed" << std::endl;
	}

	void ImageSystem::createTextureImageBatched(const char* filepath, size_t i)
	{
		VkImage image;
		VmaAllocation allocation;

		// Load our image
		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = stbi_load(filepath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		VkDeviceSize imageSize = texWidth * texHeight * 4; // 4 - 1 for each channel rgba

		// Take the number of available mip lvls +1 for level 0
		uint32_t mipLevel = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
		mipLevels.push_back(mipLevel); // Store for later when we take the LCM

		if (!pixels || !mipLevel) 
		{
			std::cout << "TexWidth\t" << texWidth << std::endl;
			std::cout << "TexHeight\t" << texHeight << std::endl;
			std::cout << "MipLvl\t" << mipLevel << std::endl;
			std::cout << filepath << std::endl;
			throw std::runtime_error("Error: failed to load texture image!");
		}

		// Move the pixels into a staging buffer - store in member vector to keep alive
		auto stagingBuffer = std::make_unique<AvengBuffer>(
			engineDevice,
			imageSize,
			1,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			1, // minOffsetAlignment
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
		);

		// Staging allocation of pixel data
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(pixels, imageSize);
		stbi_image_free(pixels);

		// Image creation info (same as original)
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = static_cast<uint32_t>(texWidth);
		imageInfo.extent.height = static_cast<uint32_t>(texHeight);
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipLevel;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.flags = 0;

		// Create image with VMA
		engineDevice.createImageWithVMA(imageInfo, VMA_MEMORY_USAGE_GPU_ONLY, image, allocation);
		allImageAllocations.push_back(allocation);

		// BATCHED OPERATIONS - Record into setupCommandBuffer instead of separate submissions
		transitionImageLayoutBatched(image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel);
		
		// Record copy operation into setup command buffer
		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = {0, 0, 0};
		region.imageExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
		
		vkCmdCopyBufferToImage(setupCommandBuffer, stagingBuffer->getBuffer(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		
		// Store staging buffer to keep it alive until commands are executed
		stagingBuffers.push_back(std::move(stagingBuffer));
		
		generateMipmapsBatched(image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevel);

		// Store the image for later use
		images.push_back(image);
	}

	void ImageSystem::transitionImageLayoutBatched(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
	{
		// BATCHED VERSION - Record into setupCommandBuffer instead of creating new command buffer
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else {
			throw std::invalid_argument("Error: unsupported layout transition!");
		}

		// Record into setup command buffer
		vkCmdPipelineBarrier(
			setupCommandBuffer,
			sourceStage,
			destinationStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier
		);
	}

	void ImageSystem::generateMipmapsBatched(VkImage _image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t _mipLevels)
	{
		// Check if image format supports linear blitting
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(engineDevice.physicalDevice(), imageFormat, &formatProperties);

		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) 
		{
			// Continue without MipMapping - use batched transition
			std::cout << "This image does not support linear blitting" << std::endl;
			transitionImageLayoutBatched(_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, _mipLevels);
			return;
		}

		// BATCHED VERSION - Record into setupCommandBuffer instead of creating new command buffer
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = _image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < _mipLevels; i++) {
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(setupCommandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;

			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(setupCommandBuffer,
				_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(setupCommandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		// Final transition for the last mip level
		barrier.subresourceRange.baseMipLevel = _mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(setupCommandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);
	}

	ImageSystem::~ImageSystem() 
	{
		// Ensure all GPU operations are complete before cleanup
		vkDeviceWaitIdle(engineDevice.device());
		
		vkDestroySampler(engineDevice.device(), textureSampler, nullptr);
		std::cout << "Destroying ImageSystem:\t" << images.size() << std::endl;
		for (int i=0; i < images.size(); i++) 
		{
			std::cout << "Destroying image " << i << " : " << images[i] << std::endl;
			// Order matters - destroy image view first
			vkDestroyImageView(engineDevice.device(), textureImageViews[i], nullptr);
			// Use VMA to destroy image and free memory automatically
			vmaDestroyImage(engineDevice.allocator(), images[i], allImageAllocations[i]);
		}
		
	}

	/*
	 * Deprecated
	 */
	void ImageSystem::createTextureImage(const char* filepath, size_t i)
	{
		VkImage image;
		VmaAllocation allocation;

		// Load our image
		int texWidth, texHeight, texChannels;
		stbi_uc* pixels = stbi_load(filepath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		VkDeviceSize imageSize = texWidth * texHeight * 4;

		// Take the number of available mip lvls +1 for level 0
		uint32_t mipLevel = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
		mipLevels.push_back(mipLevel); // Store for later when we take the LCM

		if (!pixels || !mipLevel) 
		{
			std::cout << "TexWidth\t" << texWidth << std::endl;
			std::cout << "TexHeight\t" << texHeight << std::endl;
			std::cout << "MipLvl\t" << mipLevel << std::endl;
			std::cout << filepath << std::endl;
			throw std::runtime_error("Error: failed to load texture image!");
		}

		// Move the pixels into a staging buffer
		AvengBuffer stagingBuffer{
			engineDevice,
			imageSize,
			1,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO,
			1, // minOffsetAlignment
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
		};

		// Staging allocation of pixel data
		stagingBuffer.map();
		stagingBuffer.writeToBuffer(pixels, imageSize);

		// We no longer need the local pixel data
		stbi_image_free(pixels);

		// Image
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = static_cast<uint32_t>(texWidth);
		imageInfo.extent.height = static_cast<uint32_t>(texHeight);
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipLevel;
		imageInfo.arrayLayers = 1;
		imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;	// Being sure to utilize the same image format for the texels as the pixels in the buffer, or the copy op will fail
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;	// Specifying an implementation defined ordering of the data, instead of something like row major order
		// Note: If you want to access individual texels of the image data, you need to use VK_IMAGE_TILING_LINEAR instead, this is a suboptimal tiling format
		// as it does not allow the underlying hardware to map the image into memory by its own conventions.
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // No need to preserve texel data during the first transition to the image memory from the staging buffer
		// e.g. You're using an image as a transfer source
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;	// Destination for the buffer copy- Also a source (for mipmapping due to vkCmdBlitImage). VK_IMAGE_USAGE_SAMPLED_BIT means we'd like to be able to access the image from our shaders
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Exclusive to 1 queue family, graphics
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // > 1 if images as attachments?
		imageInfo.flags = 0; // Optional

		/*
		* TODO It is possible that the VK_FORMAT_R8G8B8A8_SRGB format is not supported by the graphics hardware. 
		* You should have a list of acceptable alternatives and go with the best one that is supported.
		*/

		engineDevice.createImageWithVMA(
			imageInfo,
			VMA_MEMORY_USAGE_GPU_ONLY, // GPU-only memory for optimal performance
			image,
			allocation // reference
		);

		// Track our VMA allocations
		allImageAllocations.push_back(allocation);

		transitionImageLayout(image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevel);
		engineDevice.copyBufferToImage(stagingBuffer.getBuffer(), image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1);
		
		generateMipmaps(image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, mipLevel);

	}

	/*
	 * Deprecated
	 */
	void ImageSystem::generateMipmaps(VkImage _image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t _mipLevels)
	{
		// Check if image format supports linear blitting
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(engineDevice.physicalDevice(), imageFormat, &formatProperties);

		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) 
		{
			// Continue without MipMapping
			std::cout << "This image does not support linear blitting" << std::endl;
			transitionImageLayout(_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, _mipLevels);
			/*throw std::runtime_error("texture image format does not support linear blitting!");*/
			return;
		}

		VkCommandBuffer commandBuffer = engineDevice.beginSingleTimeCommands();

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = _image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		int32_t mipWidth = texWidth;
		int32_t mipHeight = texHeight;

		for (uint32_t i = 1; i < _mipLevels; i++) {
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			VkImageBlit blit{};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;

			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;

			vkCmdBlitImage(commandBuffer,
				_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		barrier.subresourceRange.baseMipLevel = _mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		images.push_back(_image);

		engineDevice.endSingleTimeCommands(commandBuffer);
	}

	void ImageSystem::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
	{
		// Start writing a command buffer
		VkCommandBuffer commandBuffer = engineDevice.beginSingleTimeCommands();

		/*
		* Set up an image memory barrier. This will ensure we are never
		* reading from a resource which hasn't been completely written.
		*/
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;	// These next 2 fields are used when transferring queue family ownership of the image
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;	// These would then be the indices of the queue families
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
		{
			// Undefined transfer destination : transfer writes that don't need to wait on anything
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;	// A very early stage
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
		{
			// shader reading: shader reads should wait on transfer writes, 
			// specifically the shader reads in the fragment shader, because 
			// that's where we're going to use the texture
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT; // A pseudostage. See https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPipelineStageFlagBits.html
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else {
			throw std::invalid_argument("Error: unsupported layout transition!");
		}

		// Record the command to submit pipeline barriers
		vkCmdPipelineBarrier(
			commandBuffer,
			sourceStage,		// The pipeline stage in which the operations occur that should happen BEFORE the barrier
			destinationStage,	// The pipeline stage in which operations will wait on the barrier to complete before proceeding
			0,					//  either 0 or VK_DEPENDENCY_BY_REGION_BIT. The latter turns the barrier into a per-region condition. That means that the implementation is allowed to already begin reading from the parts of a resource that were written to, thus far.
			0, nullptr,
			0, nullptr,
			1, &barrier
		);

		/*
			The pipeline stages that you are allowed to specify before and 
			after the barrier depend on how you use the resource before and 
			after the barrier. The allowed values are listed here
			https://www.khronos.org/registry/vulkan/specs/1.0/html/vkspec.html#synchronization-access-types-supported

			For example,  if you're going to read from a uniform after the barrier, you would 
			specify a usage of VK_ACCESS_UNIFORM_READ_BIT and the earliest shader that will 
			read from the uniform as pipeline stage, for example VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT. 
			It would not make sense to specify a non-shader pipeline stage for this type of usage and the 
			validation layers will warn you when you specify a pipeline stage that does not match the type of usage
		*/

		engineDevice.endSingleTimeCommands(commandBuffer);
	}


	void ImageSystem::createTextureImageView(VkImage image, size_t i)
	{
		VkImageView textureImageView = createImageView(image, VK_FORMAT_R8G8B8A8_SRGB, mipLevels[i]);
		textureImageViews.push_back(textureImageView);
	}

	VkImageView ImageSystem::createImageView(VkImage _image, VkFormat format, uint32_t mipLevels)
	{

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = _image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		VkImageView imageView;
		if (vkCreateImageView(engineDevice.device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) 
		{
			throw std::runtime_error("failed to create texture image view!");
		}


		return imageView;
	}

	void ImageSystem::createTextureSampler() 
	{

		auto smallest_mip = min_element(std::begin(mipLevels), std::end(mipLevels));

		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(engineDevice.physicalDevice(), &properties);

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f; // Optional
		samplerInfo.maxLod = (*smallest_mip * 1.0f); // OR static_cast<float>(mipLevel);
		samplerInfo.mipLodBias = 0.0f; // Optional

		if (vkCreateSampler(engineDevice.device(), &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) 
		{
			throw std::runtime_error("failed to create texture sampler!");
		}
	}

	void ImageSystem::createImageDescriptors(std::vector<VkImageView> views)
	{
		// Dynamic texture array - create exactly the number of descriptors we have
		for (VkImageView view : views) {
			// Image Descriptor
			VkDescriptorImageInfo descriptorImageInfo{};
			descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			descriptorImageInfo.imageView = view;
			descriptorImageInfo.sampler = textureSampler;

			imageInfosArray.push_back(descriptorImageInfo);
		}

		// Ensure we have at least one descriptor for empty scenes
		if (imageInfosArray.empty()) {
			throw std::runtime_error("No texture descriptors available - scenes must have at least one texture");
		}

		std::cout << "Created " << imageInfosArray.size() << " image descriptors (dynamic array size)." << std::endl;
	}

} 