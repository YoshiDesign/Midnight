#include "AnimationRenderingSystem.h"

#include <iostream>
#include <cassert>

#include "CoreVK/EngineDevice.h"
#include "CoreVK/ComputePipeline.h"
#include "CoreVK/aveng_buffer.h"
#include "CoreVK/aveng_descriptors.h"
#include "CoreVK/GFXPipeline.h"
#include "Core/Renderer/PipelineConfigManager.h"
#include "Logger.h"

namespace aveng {

AnimationRenderingSystem::AnimationRenderingSystem(EngineDevice& device)
    : engineDevice(device)
{
    std::cout << "AnimationRenderingSystem: Initializing..." << std::endl;
}

AnimationRenderingSystem::~AnimationRenderingSystem()
{
    std::cout << "AnimationRenderingSystem: Destroying..." << std::endl;
    // Cleanup handled by unique_ptr destructors
}

void AnimationRenderingSystem::initializeDescriptors(AvengDescriptorPool& descriptorPool, int maxFramesInFlight)
{
    if (initialized) {
        std::cerr << "AnimationRenderingSystem: Already initialized!" << std::endl;
        return;
    }

    std::cout << "AnimationRenderingSystem: Setting up descriptors for " << maxFramesInFlight << " frames" << std::endl;

    createAnimationDescriptorSetLayout();
    createAnimationBuffers(maxFramesInFlight);
    setupAnimationDescriptorSets(descriptorPool, maxFramesInFlight);
    
    initialized = true;
    std::cout << "AnimationRenderingSystem: Descriptor setup complete" << std::endl;
}

void AnimationRenderingSystem::createAnimationDescriptorSetLayout()
{
    std::cout << "AnimationRenderingSystem: Creating descriptor set layout..." << std::endl;
    
    // Animation descriptor set layout (Set 3)
    animationDescriptorSetLayout =
        AvengDescriptorSetLayout::Builder(engineDevice)
        .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 1)  // Animation UBO
        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 1)  // Bone matrices SSBO
        .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)                               // Instance animation data SSBO
        .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)                               // Input animated vertices SSBO
        .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, 1)  // Transformed vertices SSBO
        .build();
        
    std::cout << "AnimationRenderingSystem: Descriptor set layout created" << std::endl;
}

void AnimationRenderingSystem::createAnimationBuffers(int maxFramesInFlight)
{
    std::cout << "AnimationRenderingSystem: Creating animation buffers..." << std::endl;
    
    // Resize buffer vectors
    u_AnimationBuffers.resize(maxFramesInFlight);
    boneMatrixBuffers.resize(maxFramesInFlight);
    instanceAnimationBuffers.resize(maxFramesInFlight);
    animatedVertexBuffers.resize(maxFramesInFlight);
    transformedVertexBuffers.resize(maxFramesInFlight);
    animatedIndexBuffers.resize(maxFramesInFlight);

    // Create buffers for each frame in flight
    for (int i = 0; i < maxFramesInFlight; i++) {
        // Animation UBO buffer
        u_AnimationBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(AnimationUbo), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            1, // minOffsetAlignment
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        u_AnimationBuffers[i]->map();

        // Bone matrices SSBO (conservative size for 128 bones)
        boneMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(glm::mat4) * 128, 1,  // 128 bones max per model
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO,
            16, // minOffsetAlignment for mat4
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        boneMatrixBuffers[i]->map();

        // Instance animation data SSBO (for up to 1000 instances)
        instanceAnimationBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(InstanceAnimationData) * 1000, 1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO,
            16, // minOffsetAlignment for aligned structs
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        instanceAnimationBuffers[i]->map();

        // Animated vertex SSBO (sized for large models like animTVGuy - 100k vertices)
        animatedVertexBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(AnimatedVertex) * 100000, 1,  // Large enough for stress test models
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO,
            16, // minOffsetAlignment
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        animatedVertexBuffers[i]->map();

        // Transformed vertex output SSBO (using TransformedVertex - no bone data)
        transformedVertexBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(TransformedVertex) * 100000, 1,  // Match input buffer size
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            16, // minOffsetAlignment
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        transformedVertexBuffers[i]->map();

        // Index buffer for animated meshes (sized for large models - 350k indices)
        animatedIndexBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(uint32_t) * 350000, 1,  // Large enough for 322k+ indices
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO,
            4, // minOffsetAlignment for uint32_t
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        animatedIndexBuffers[i]->map();
    }
    
    std::cout << "AnimationRenderingSystem: Created buffers for " << maxFramesInFlight << " frames" << std::endl;
}

void AnimationRenderingSystem::setupAnimationDescriptorSets(AvengDescriptorPool& descriptorPool, int maxFramesInFlight)
{
    std::cout << "AnimationRenderingSystem: Setting up descriptor sets..." << std::endl;
    
    // Resize descriptor set vector
    animationDescriptorSets.resize(maxFramesInFlight);

    // Write descriptor sets
    for (int i = 0; i < maxFramesInFlight; i++) {
        auto animationBufferInfo = u_AnimationBuffers[i]->descriptorInfo(sizeof(AnimationUbo), 0);
        auto boneMatrixBufferInfo = boneMatrixBuffers[i]->descriptorInfo();
        auto instanceAnimationBufferInfo = instanceAnimationBuffers[i]->descriptorInfo();
        auto animatedVertexBufferInfo = animatedVertexBuffers[i]->descriptorInfo();
        auto transformedVertexBufferInfo = transformedVertexBuffers[i]->descriptorInfo();
        
        AvengDescriptorSetWriter(*animationDescriptorSetLayout, descriptorPool)
            .writeBuffer(0, &animationBufferInfo)         // Animation UBO
            .writeBuffer(1, &boneMatrixBufferInfo)       // Bone matrices SSBO
            .writeBuffer(2, &instanceAnimationBufferInfo) // Instance animation data SSBO
            .writeBuffer(3, &animatedVertexBufferInfo)   // Input animated vertices SSBO
            .writeBuffer(4, &transformedVertexBufferInfo) // Transformed vertices SSBO
            .build(animationDescriptorSets[i]);
    }
    
    std::cout << "AnimationRenderingSystem: Descriptor sets configured" << std::endl;
}

void AnimationRenderingSystem::createAnimationComputePipeline(VkPipelineLayout pipelineLayout)
{
    std::cout << "AnimationRenderingSystem: Creating compute pipeline..." << std::endl;
    
    // Create compute pipeline config
    ComputePipelineConfig computeConfig{};
    ComputePipeline::defaultComputePipelineConfig(computeConfig);
    computeConfig.pipelineLayout = pipelineLayout;
    
    // Create compute pipeline
    animationComputePipeline = std::make_unique<ComputePipeline>(
        engineDevice,
        "shaders/skeletal_animation.comp.spv",
        computeConfig
    );
    
    std::cout << "AnimationRenderingSystem: Compute pipeline created successfully!" << std::endl;
}

void AnimationRenderingSystem::updateAnimationData(const std::vector<std::shared_ptr<AssimpInstance>>& instances, 
                                                  float deltaTime, int currentFrameIndex)
{
    if (instances.empty()) return;

    lastInstanceCount = static_cast<uint32_t>(instances.size());
    lastVertexCount = calculateTotalVertices(instances);

    //std::cout << "AnimationRenderingSystem: Updating data for " << lastInstanceCount 
    //          << " instances (" << lastVertexCount << " vertices)" << std::endl;

    // Prepare animation UBO data
    AnimationUbo animUBO{};
    animUBO.computeData.deltaTime = deltaTime;
    animUBO.computeData.totalInstances = lastInstanceCount;
    animUBO.computeData.maxBonesPerInstance = 128;
    animUBO.computeData.verticesPerInstance = lastVertexCount;

    // Update animation UBO
    u_AnimationBuffers[currentFrameIndex]->writeToBuffer(&animUBO);
    u_AnimationBuffers[currentFrameIndex]->flush();

    // Process each instance
    std::vector<InstanceAnimationData> instanceData;
    std::vector<glm::mat4> allBoneMatrices;
    std::vector<AnimatedVertex> allVertices;
    std::vector<uint32_t> allIndices;

    uint32_t currentBoneOffset = 0;
    uint32_t currentVertexOffset = 0;

    for (size_t i = 0; i < instances.size(); ++i) {
        auto& instance = instances[i];
        
        // Create instance animation data
        InstanceAnimationData instData{};
        instData.modelMatrix = instance->getInstanceRootMatrix();
        instData.animationTime = instance->getInstanceSettings().isAnimPlayTimePos;
        instData.animationClipIndex = instance->getInstanceSettings().isAnimClipNr;
        instData.boneMatrixOffset = currentBoneOffset;

        // Get bone matrices from instance
        const auto& boneMatrices = instance->getBoneTransformMatrices();
        instData.boneCount = static_cast<uint32_t>(boneMatrices.size());
        currentBoneOffset += instData.boneCount;

        // Add bone matrices to global buffer
        allBoneMatrices.insert(allBoneMatrices.end(), boneMatrices.begin(), boneMatrices.end());

        // Get vertex data from model meshes
        const auto& meshes = instance->getModel()->getModelMeshes();
        for (const auto& mesh : meshes) {
            // Add all vertices from this mesh
            allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            
            // Add all indices from this mesh (offset by current vertex offset)
            const auto& meshIndices = mesh.indices;
            for (uint32_t index : meshIndices) {
                allIndices.push_back(index + currentVertexOffset);
            }
            
            currentVertexOffset += static_cast<uint32_t>(mesh.vertices.size());
        }

        instanceData.push_back(instData);
    }

    // Upload instance animation data
    if (!instanceData.empty()) {
        instanceAnimationBuffers[currentFrameIndex]->writeToBuffer(instanceData.data(), 
            sizeof(InstanceAnimationData) * instanceData.size());
        instanceAnimationBuffers[currentFrameIndex]->flush();
    }

    // Upload bone matrices
    if (!allBoneMatrices.empty()) {
        boneMatrixBuffers[currentFrameIndex]->writeToBuffer(allBoneMatrices.data(),
            sizeof(glm::mat4) * allBoneMatrices.size());
        boneMatrixBuffers[currentFrameIndex]->flush();
    }

    // Upload animated vertices
    if (!allVertices.empty()) {
        // Verify buffer is large enough (should be with our 100k vertex allocation)
        uint32_t requiredVertexBufferSize = sizeof(AnimatedVertex) * allVertices.size();
        if (animatedVertexBuffers[currentFrameIndex]->getBufferSize() < requiredVertexBufferSize) {
            std::cerr << "ERROR: Animated vertex buffer too small! Required: " << requiredVertexBufferSize 
                      << ", Available: " << animatedVertexBuffers[currentFrameIndex]->getBufferSize() << std::endl;
            return; // Fail gracefully instead of crashing
        }
        
        animatedVertexBuffers[currentFrameIndex]->writeToBuffer(allVertices.data(),
            sizeof(AnimatedVertex) * allVertices.size());
        animatedVertexBuffers[currentFrameIndex]->flush();
    }

    // Upload animated indices
    if (!allIndices.empty()) {
        // Verify buffer is large enough (should be with our 350k index allocation)
        uint32_t requiredIndexBufferSize = sizeof(uint32_t) * allIndices.size();
        if (animatedIndexBuffers[currentFrameIndex]->getBufferSize() < requiredIndexBufferSize) {
            std::cerr << "ERROR: Animated index buffer too small! Required: " << requiredIndexBufferSize 
                      << ", Available: " << animatedIndexBuffers[currentFrameIndex]->getBufferSize() << std::endl;
            return; // Fail gracefully instead of crashing
        }
        
        animatedIndexBuffers[currentFrameIndex]->writeToBuffer(allIndices.data(),
            sizeof(uint32_t) * allIndices.size());
        animatedIndexBuffers[currentFrameIndex]->flush();
    }

    //std::cout << "AnimationRenderingSystem: Data uploaded - " << instanceData.size() << " instances, " 
    //          << allBoneMatrices.size() << " bone matrices, " 
    //          << allVertices.size() << " vertices, " 
    //          << allIndices.size() << " indices" << std::endl;
}

void AnimationRenderingSystem::dispatchAnimationCompute(VkCommandBuffer commandBuffer, uint32_t vertexCount, 
                                                       VkPipelineLayout pipelineLayout, int currentFrameIndex)
{
    if (!animationComputePipeline || vertexCount == 0) {
        return;
    }

    // Verify transformed vertex buffer is large enough for compute output
    uint32_t requiredTransformedBufferSize = sizeof(TransformedVertex) * vertexCount;
    if (transformedVertexBuffers[currentFrameIndex]->getBufferSize() < requiredTransformedBufferSize) {
        std::cerr << "ERROR: Transformed vertex buffer too small! Required: " << requiredTransformedBufferSize 
                  << ", Available: " << transformedVertexBuffers[currentFrameIndex]->getBufferSize() << std::endl;
        return; // Fail gracefully instead of crashing
    }

    //std::cout << "AnimationRenderingSystem: Dispatching compute for " << vertexCount << " vertices" << std::endl;
    
    // Bind compute pipeline
    animationComputePipeline->bind(commandBuffer);
    
    // Bind animation descriptor set (Set 3)
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
        3, 1, &animationDescriptorSets[currentFrameIndex], 0, nullptr);
    
    // Calculate dispatch size (64 threads per workgroup)
    uint32_t dispatchX = (vertexCount + 63) / 64;  // Round up division
    
    // Dispatch compute shader
    vkCmdDispatch(commandBuffer, dispatchX, 1, 1);
    
    // Memory barrier to ensure compute writes are visible to vertex shader
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    
    std::cout << "AnimationRenderingSystem: Compute dispatched - " << dispatchX << " workgroups" << std::endl;
}

void AnimationRenderingSystem::renderAnimatedModels(VkCommandBuffer commandBuffer, 
                                                   const std::vector<std::shared_ptr<AssimpInstance>>& instances,
                                                   VkPipelineLayout pipelineLayout, 
                                                   PipelineConfigManager* pipelineManager,
                                                   int currentObjectMode, int currentFrameIndex)
{
    if (instances.empty()) return;

    // Use dedicated animated pipeline (ID 4) that handles AnimatedVertex with bone data
    GFXPipeline* activePipeline = nullptr;
    if (pipelineManager) {
        // Debug: Print available pipelines
        //auto availablePipelines = pipelineManager->getPipelineNames();
        //std::cout << "Available pipelines: ";
        //for (const auto& name : availablePipelines) {
        //    std::cout << name << " ";
        //}
        //std::cout << std::endl;
        
        activePipeline = pipelineManager->getPipeline(4);  // ANIMATED pipeline ID
        if (activePipeline) {
            activePipeline->bind(commandBuffer);
        } else {
            std::cerr << "Pipeline ID 4 (ANIMATED) not found!" << std::endl;
        }
    } else {
        std::cerr << "PipelineManager is null!" << std::endl;
    }
    
    if (!activePipeline) {
        std::cerr << "AnimationRenderingSystem: No pipeline available for animated rendering" << std::endl;
        return;
    }

    // NOTE: Global and lights descriptor sets (Set 0, Set 2) are already bound by the main renderer by now.
    // We only bind the animation descriptor set (Set 3) here
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
        3, 1, &animationDescriptorSets[currentFrameIndex], 0, nullptr);

    std::cout << "AnimationRenderingSystem: Rendering " << instances.size() << " animated models" << std::endl;
    
    // Bind the vertex and index buffers once (they contain data for all instances)
    VkBuffer vertexBuffers[] = { transformedVertexBuffers[currentFrameIndex]->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, animatedIndexBuffers[currentFrameIndex]->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    // Track global offsets across all instances
    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;
    
    // Render each animated instance
    for (const auto& instance : instances) {
        if (!instance || !instance->getModel()) continue;
        
        // Get model data
        const auto& meshes = instance->getModel()->getModelMeshes();
        glm::mat4 modelMatrix = instance->getInstanceRootMatrix();
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        
        // Push constants
        struct PushConstantData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
        } pushData{ modelMatrix, normalMatrix };

        vkCmdPushConstants(commandBuffer, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConstantData), &pushData);

        std::cout << "  Rendering instance at global vertex offset " << globalVertexOffset 
                  << ", index offset " << globalIndexOffset << std::endl;

        // Render each mesh of this instance using the correct global offsets
        for (const auto& mesh : meshes) {
            if (!mesh.indices.empty()) {
                // Use indexed drawing for efficiency
                uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
                vkCmdDrawIndexed(commandBuffer, indexCount, 1, globalIndexOffset, globalVertexOffset, 0);
                globalIndexOffset += indexCount;
            } else {
                // Fallback to non-indexed drawing if no indices
                vkCmdDraw(commandBuffer, static_cast<uint32_t>(mesh.vertices.size()), 1, globalVertexOffset, 0);
            }
            
            globalVertexOffset += static_cast<uint32_t>(mesh.vertices.size());
        }
        
        std::cout << "  Finished instance with " << meshes.size() << " meshes" << std::endl;
    }
}

uint32_t AnimationRenderingSystem::calculateTotalVertices(const std::vector<std::shared_ptr<AssimpInstance>>& instances)
{
    uint32_t totalVertices = 0;
    for (const auto& instance : instances) {
        const auto& meshes = instance->getModel()->getModelMeshes();
        for (const auto& mesh : meshes) {
            totalVertices += static_cast<uint32_t>(mesh.vertices.size());
        }
    }
    return totalVertices;
}

// Getters
VkDescriptorSetLayout AnimationRenderingSystem::getAnimationDescriptorSetLayout() const
{
    return animationDescriptorSetLayout ? animationDescriptorSetLayout->getDescriptorSetLayout() : VK_NULL_HANDLE;
}

VkDescriptorSet AnimationRenderingSystem::getAnimationDescriptorSet(int frameIndex) const
{
    if (frameIndex >= 0 && frameIndex < animationDescriptorSets.size()) {
        return animationDescriptorSets[frameIndex];
    }
    return VK_NULL_HANDLE;
}

} // namespace aveng 