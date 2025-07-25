// Example usage of ComputePipeline class
// This file demonstrates how to use compute shaders properly in the Midnight engine

#include "ComputePipeline.h"
#include "aveng_buffer.h"
#include "aveng_descriptors.h"

namespace aveng {

class ComputePipelineExample {
public:
    // Example struct for uniform buffer
    struct ComputeUBO {
        float multiplier;
        uint32_t dataCount;
    };

    void demonstrateComputeUsage(EngineDevice& device) {
        // 1. Create descriptor set layout for compute shader
        AvengDescriptorSetLayout::Builder layoutBuilder{device};
        layoutBuilder
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // Input buffer
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)  // Output buffer  
            .addBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT); // Uniform buffer

        auto descriptorSetLayout = layoutBuilder.build();

        // 2. Create pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout->getDescriptorSetLayout();

        VkPipelineLayout pipelineLayout;
        if (vkCreatePipelineLayout(device.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create compute pipeline layout");
        }

        // 3. Configure compute pipeline
        ComputePipelineConfig config{};
        ComputePipeline::defaultComputePipelineConfig(config);
        config.pipelineLayout = pipelineLayout;

        // 4. Create compute pipeline
        auto computePipeline = std::make_unique<ComputePipeline>(
            device,
            "shaders/example_compute.comp.spv",  // Make sure to compile your .comp shader to .spv
            config
        );

        // 5. Example of how to use the compute pipeline in a command buffer
        /*
        VkCommandBuffer commandBuffer = ...; // Your command buffer
        
        // Create buffers (input, output, uniform)
        const uint32_t dataSize = 1024;
        std::vector<float> inputData(dataSize, 1.0f);
        
        auto inputBuffer = std::make_unique<AvengBuffer>(
            device,
            sizeof(float),
            dataSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        
        auto outputBuffer = std::make_unique<AvengBuffer>(
            device,
            sizeof(float),
            dataSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        
        ComputeUBO ubo{};
        ubo.multiplier = 2.0f;
        ubo.dataCount = dataSize;
        
        auto uniformBuffer = std::make_unique<AvengBuffer>(
            device,
            sizeof(ComputeUBO),
            1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        // Upload data
        inputBuffer->map();
        inputBuffer->writeToBuffer(inputData.data());
        inputBuffer->unmap();
        
        uniformBuffer->map();
        uniformBuffer->writeToBuffer(&ubo);
        uniformBuffer->unmap();

        // Create descriptor sets and bind resources
        // (You'll need to set up descriptor pool and sets)
        
        // In your command buffer recording:
        computePipeline->bind(commandBuffer);
        
        // Bind descriptor sets containing your buffers
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout,
            0, 1, &descriptorSet,  // Your descriptor set
            0, nullptr
        );
        
        // Dispatch compute work
        // For 1024 elements with local_size_x = 64, we need ceil(1024/64) = 16 work groups
        uint32_t workGroupCount = (dataSize + 63) / 64;  // Round up division
        computePipeline->dispatch(commandBuffer, workGroupCount, 1, 1);
        
        // Add memory barrier if needed before using results
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            1, &barrier,
            0, nullptr,
            0, nullptr
        );
        */

        // Cleanup
        vkDestroyPipelineLayout(device.device(), pipelineLayout, nullptr);
    }
};

} // namespace aveng 