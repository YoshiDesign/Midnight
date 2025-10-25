#include "AnimationRenderingSystem.h"

#include <iostream>
#include <cassert>
#include <chrono>

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
        // FIXED: Added VK_BUFFER_USAGE_VERTEX_BUFFER_BIT for direct vertex rendering
        animatedVertexBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
            sizeof(AnimatedVertex) * 100000, 1,  // Large enough for stress test models
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

    //lastInstanceCount = static_cast<uint32_t>(instances.size());
    //lastVertexCount = calculateTotalVertices(instances);

    ////std::cout << "AnimationRenderingSystem: Updating data for " << lastInstanceCount 
    ////          << " instances (" << lastVertexCount << " vertices)" << std::endl;

    //// Prepare animation UBO data
    //AnimationUbo animUBO{};
    //animUBO.computeData.deltaTime = deltaTime;
    //animUBO.computeData.totalInstances = lastInstanceCount;
    //animUBO.computeData.maxBonesPerInstance = 128;
    //animUBO.computeData.verticesPerInstance = lastVertexCount;

    //// Update animation UBO
    //u_AnimationBuffers[currentFrameIndex]->writeToBuffer(&animUBO);
    //u_AnimationBuffers[currentFrameIndex]->flush();

    //// Process each instance
    //std::vector<InstanceAnimationData> instanceData;
    //std::vector<glm::mat4> allBoneMatrices;
    //std::vector<AnimatedVertex> allVertices;
    //std::vector<uint32_t> allIndices;

    //// FIXED: Always rebuild layout metadata (not just first frame)
    //std::vector<InstanceLayout> currentFrameLayouts;
    //uint32_t currentBoneOffset = 0;
    //uint32_t currentVertexOffset = 0;
    //uint32_t currentIndexOffset = 0;

    //for (size_t i = 0; i < instances.size(); ++i) {
    //    auto& instance = instances[i];
    //    
    //    // FIXED: Create layout metadata for this instance
    //    InstanceLayout layout{};
    //    layout.vertexOffset = currentVertexOffset;
    //    layout.indexOffset = currentIndexOffset;
    //    layout.boneOffset = currentBoneOffset;
    //    layout.debugName = "Instance_" + std::to_string(i);  // Simple debug name
    //    
    //    // Create instance animation data
    //    InstanceAnimationData instData{};
    //    instData.modelMatrix = instance->getInstanceRootMatrix();
    //    instData.animationTime = instance->getInstanceSettings().isAnimPlayTimePos;
    //    instData.animationClipIndex = instance->getInstanceSettings().isAnimClipNr;
    //    instData.boneMatrixOffset = currentBoneOffset;

    //    // Get bone matrices from instance
    //    const auto& boneMatrices = instance->getBoneTransformMatrices();
    //    instData.boneCount = static_cast<uint32_t>(boneMatrices.size());
    //    layout.boneCount = instData.boneCount;
    //    currentBoneOffset += instData.boneCount;

    //    // DEBUG: Check first bone matrix for corruption
    //    if (i == 0 && !boneMatrices.empty()) {
    //        const auto& firstBone = boneMatrices[0];
    //        /*std::cout << " DEBUG: First bone matrix for instance " << i << ":" << std::endl;
    //        std::cout << "  [" << firstBone[0][0] << ", " << firstBone[0][1] << ", " << firstBone[0][2] << ", " << firstBone[0][3] << "]" << std::endl;
    //        std::cout << "  [" << firstBone[1][0] << ", " << firstBone[1][1] << ", " << firstBone[1][2] << ", " << firstBone[1][3] << "]" << std::endl;
    //        std::cout << "  [" << firstBone[2][0] << ", " << firstBone[2][1] << ", " << firstBone[2][2] << ", " << firstBone[2][3] << "]" << std::endl;
    //        std::cout << "  [" << firstBone[3][0] << ", " << firstBone[3][1] << ", " << firstBone[3][2] << ", " << firstBone[3][3] << "]" << std::endl;*/
    //    }

    //    // Add bone matrices to global buffer (ALWAYS needed for animation)
    //    allBoneMatrices.insert(allBoneMatrices.end(), boneMatrices.begin(), boneMatrices.end());

    //    // FIXED: ALWAYS collect vertex/index data and layout metadata (not just first frame)
    //    const auto& meshes = instance->getModel()->getModelMeshes();
    //    uint32_t instanceVertexCount = 0;
    //    uint32_t instanceIndexCount = 0;
    //    
    //    for (const auto& mesh : meshes) {
    //        // Add all vertices from this mesh
    //        allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    //        instanceVertexCount += static_cast<uint32_t>(mesh.vertices.size());
    //        
    //        // FIXED: Add indices with proper offset (no double offsetting in rendering)
    //        const auto& meshIndices = mesh.indices;
    //        for (uint32_t index : meshIndices) {
    //            allIndices.push_back(index + currentVertexOffset);  // Pre-offset for global vertex buffer
    //        }
    //        instanceIndexCount += static_cast<uint32_t>(meshIndices.size());
    //        
    //        currentVertexOffset += static_cast<uint32_t>(mesh.vertices.size());
    //    }
    //    
    //    // Complete layout metadata for this instance
    //    layout.vertexCount = instanceVertexCount;
    //    layout.indexCount = instanceIndexCount;
    //    currentIndexOffset += instanceIndexCount;
    //    
    //    // Store layout and instance data
    //    currentFrameLayouts.push_back(layout);
    //    instanceData.push_back(instData);
    //}

    //// Upload instance animation data
    //if (!instanceData.empty()) {
    //    instanceAnimationBuffers[currentFrameIndex]->writeToBuffer(instanceData.data(), 
    //        sizeof(InstanceAnimationData) * instanceData.size());
    //    instanceAnimationBuffers[currentFrameIndex]->flush();
    //}

    //// 🔧 FIXED: Store persistent layout metadata (always update)
    //persistentInstanceLayouts = std::move(currentFrameLayouts);
    //
    //// Upload bone matrices
    //if (!allBoneMatrices.empty()) {
    //    boneMatrixBuffers[currentFrameIndex]->writeToBuffer(allBoneMatrices.data(),
    //        sizeof(glm::mat4) * allBoneMatrices.size());
    //    boneMatrixBuffers[currentFrameIndex]->flush();
    //}

    //// 🔧 FIXED: Upload mesh data based on actual changes, not arbitrary flag
    //// Upload if we have new vertex/index data OR if not previously uploaded
    //if (!allVertices.empty() && (!staticDataUploaded || allVertices.size() != lastVertexCount)) {
    //    std::cout << " PERFORMANCE: Uploading mesh data (" 
    //              << allVertices.size() << " vertices, " << allIndices.size() << " indices)" << std::endl;
    //    
    //    // 🔧 FIXED: Debug layout metadata for validation
    //    std::cout << "LAYOUT METADATA DEBUG:" << std::endl;
    //    for (size_t i = 0; i < persistentInstanceLayouts.size(); ++i) {
    //        const auto& layout = persistentInstanceLayouts[i];
    //        std::cout << "  " << layout.debugName << ": vertices[" << layout.vertexOffset 
    //                  << "-" << (layout.vertexOffset + layout.vertexCount - 1) << "] "
    //                  << "indices[" << layout.indexOffset << "-" << (layout.indexOffset + layout.indexCount - 1) << "] "
    //                  << "bones[" << layout.boneOffset << "-" << (layout.boneOffset + layout.boneCount - 1) << "]" << std::endl;
    //    }
    //    
    //    // DEBUG: Extensive vertex collection analysis
    //    std::cout << "EXTENSIVE DEBUG ANALYSIS:" << std::endl;
    //    std::cout << "==================================" << std::endl;
    //    
    //    // Per-instance breakdown
    //    uint32_t totalVerticesExpected = 0;
    //    uint32_t totalIndicesExpected = 0;
    //    for (size_t i = 0; i < instances.size(); ++i) {
    //        const auto& meshes = instances[i]->getModel()->getModelMeshes();
    //        uint32_t instanceVertices = 0;
    //        uint32_t instanceIndices = 0;
    //        
    //        std::cout << "Instance " << i << " mesh breakdown:" << std::endl;
    //        for (size_t m = 0; m < meshes.size(); ++m) {
    //            instanceVertices += static_cast<uint32_t>(meshes[m].vertices.size());
    //            instanceIndices += static_cast<uint32_t>(meshes[m].indices.size());
    //            std::cout << "  Mesh " << m << ": " << meshes[m].vertices.size() << " vertices, " 
    //                      << meshes[m].indices.size() << " indices" << std::endl;
    //        }
    //        
    //        std::cout << "Instance " << i << " totals: " << instanceVertices << " vertices, " 
    //                  << instanceIndices << " indices" << std::endl;
    //        totalVerticesExpected += instanceVertices;
    //        totalIndicesExpected += instanceIndices;
    //    }
    //    
    //    std::cout << "EXPECTED TOTALS: " << totalVerticesExpected << " vertices, " << totalIndicesExpected << " indices" << std::endl;
    //    std::cout << "COLLECTED TOTALS: " << allVertices.size() << " vertices, " << allIndices.size() << " indices" << std::endl;
    //    
    //    if (totalVerticesExpected != allVertices.size()) {
    //        std::cout << "VERTEX COUNT MISMATCH!" << std::endl;
    //    }
    //    if (totalIndicesExpected != allIndices.size()) {
    //        std::cout << " INDEX COUNT MISMATCH!" << std::endl;
    //    }
    //    
    //    // Sample vertex data analysis
    //    std::cout << "\n SAMPLE VERTEX DATA:" << std::endl;
    //    for (size_t i = 0; i < std::min(size_t(3), allVertices.size()); ++i) {
    //        const auto& v = allVertices[i];
    //        std::cout << "  Vertex " << i << ":" << std::endl;
    //        std::cout << "    Position: (" << v.position.x << ", " << v.position.y << ", " << v.position.z << ")" << std::endl;
    //        std::cout << "    Normal: (" << v.normal.x << ", " << v.normal.y << ", " << v.normal.z << ")" << std::endl;
    //        std::cout << "    TexCoord: (" << v.position.w << ", " << v.color.w << ") [packed in .w components]" << std::endl;
    //        std::cout << "    BoneIds: (" << v.boneIds.x << ", " << v.boneIds.y << ", " << v.boneIds.z << ", " << v.boneIds.w << ")" << std::endl;
    //        std::cout << "    BoneWeights: (" << v.boneWeights.x << ", " << v.boneWeights.y << ", " << v.boneWeights.z << ", " << v.boneWeights.w << ")" << std::endl;
    //        
    //        // Validate bone data
    //        float totalWeight = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
    //        std::cout << "    Weight Sum: " << totalWeight << std::endl;
    //        if (totalWeight > 1.1f || totalWeight < 0.9f) {
    //            std::cout << "    WARNING: Bone weights don't sum to ~1.0!" << std::endl;
    //        }
    //    }
    //    
    //    // Sample index data
    //    std::cout << "\n SAMPLE INDEX DATA:" << std::endl;
    //    std::cout << "First 15 indices: ";
    //    for (size_t i = 0; i < std::min(size_t(15), allIndices.size()); ++i) {
    //        std::cout << allIndices[i] << " ";
    //    }
    //    std::cout << std::endl;
    //    
    //    // STEP 5: Validate indices point to valid vertices (catch double-offsetting bugs)
    //    std::cout << "INDEX VALIDATION:" << std::endl;
    //    uint32_t invalidIndices = 0;
    //    uint32_t maxValidIndex = allVertices.size() - 1;
    //    for (size_t i = 0; i < std::min(size_t(20), allIndices.size()); ++i) {  // Check first 20 indices
    //        if (allIndices[i] > maxValidIndex) {
    //            std::cout << "  Index " << i << " = " << allIndices[i] << " (max valid: " << maxValidIndex << ")" << std::endl;
    //            invalidIndices++;
    //        } else if (i < 10) {  // Show first 10 valid indices for reference
    //            std::cout << "  Index " << i << " = " << allIndices[i] << " (valid)" << std::endl;
    //        }
    //    }
    //    if (invalidIndices > 0) {
    //        std::cerr << "ERROR: Found " << invalidIndices << " invalid indices! This indicates offset calculation bugs." << std::endl;
    //    } else {
    //        std::cout << "All checked indices are valid!" << std::endl;
    //    }
    //    
    //    // Upload vertices to ALL frames (since we use different buffers per frame)
    //    for (int frame = 0; frame < animatedVertexBuffers.size(); ++frame) {
    //        // Verify buffer is large enough
    //        uint32_t requiredVertexBufferSize = sizeof(AnimatedVertex) * allVertices.size();
    //        if (animatedVertexBuffers[frame]->getBufferSize() < requiredVertexBufferSize) {
    //            std::cerr << "ERROR: Animated vertex buffer too small! Required: " << requiredVertexBufferSize 
    //                      << ", Available: " << animatedVertexBuffers[frame]->getBufferSize() << std::endl;
    //            return;
    //        }
    //        
    //        animatedVertexBuffers[frame]->writeToBuffer(allVertices.data(),
    //            sizeof(AnimatedVertex) * allVertices.size());
    //        animatedVertexBuffers[frame]->flush();
    //    }
    //    
    //    // Upload indices to ALL frames  
    //    for (int frame = 0; frame < animatedIndexBuffers.size(); ++frame) {
    //        // Verify buffer is large enough
    //        uint32_t requiredIndexBufferSize = sizeof(uint32_t) * allIndices.size();
    //        if (animatedIndexBuffers[frame]->getBufferSize() < requiredIndexBufferSize) {
    //            std::cerr << "ERROR: Animated index buffer too small! Required: " << requiredIndexBufferSize 
    //                      << ", Available: " << animatedIndexBuffers[frame]->getBufferSize() << std::endl;
    //            return;
    //        }
    //        
    //        animatedIndexBuffers[frame]->writeToBuffer(allIndices.data(),
    //            sizeof(uint32_t) * allIndices.size());
    //        animatedIndexBuffers[frame]->flush();
    //    }
    //    
    //    staticDataUploaded = true;
    //    lastVertexCount = static_cast<uint32_t>(allVertices.size());  // Track for upload condition
    //    std::cout << "PERFORMANCE: Mesh data uploaded successfully!" << std::endl;
    //}

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

    // PERFORMANCE: Time compute shader dispatch
    static int dispatchCount = 0;
    static float totalDispatchTime = 0.0f;
    
    auto dispatchStartTime = std::chrono::high_resolution_clock::now();
    
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
    
    auto dispatchEndTime = std::chrono::high_resolution_clock::now();
    auto dispatchDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(dispatchEndTime - dispatchStartTime).count();
    
    totalDispatchTime += dispatchDuration;
    dispatchCount++;
    
    // Report every 60 dispatches (~1 second at 60fps)
    if (dispatchCount >= 60) {
        float avgDispatchTime = totalDispatchTime / dispatchCount;
        std::cout << " COMPUTE SHADER PERFORMANCE (60 dispatches avg):" << std::endl;
        std::cout << "  Compute dispatch: " << avgDispatchTime << " ms/frame" << std::endl;
        std::cout << "  Vertices processed: " << vertexCount << " per dispatch" << std::endl;
        std::cout << "  Workgroups: " << dispatchX << " per dispatch" << std::endl;
        
        // Reset counters
        dispatchCount = 0;
        totalDispatchTime = 0.0f;
    }
}

void AnimationRenderingSystem::renderAnimatedModels(VkCommandBuffer commandBuffer, 
                                                   const std::vector<std::shared_ptr<AssimpInstance>>& instances,
                                                   VkPipelineLayout pipelineLayout, 
                                                   PipelineConfigManager* pipelineManager,
                                                   int currentObjectMode, int currentFrameIndex)
{
    if (instances.empty()) return;
    
    // PERFORMANCE: Time animation rendering
    static int renderCount = 0;
    static float totalRenderTime = 0.0f;
    
    auto renderStartTime = std::chrono::high_resolution_clock::now();

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

    // DEBUG: Bind INPUT animated vertex buffers instead of transformed output
    // This tests if the raw geometry data is valid
    VkBuffer vertexBuffers[] = { animatedVertexBuffers[currentFrameIndex]->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, animatedIndexBuffers[currentFrameIndex]->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    
    
    // Use persistent layout metadata instead of recalculating offsets
    if (persistentInstanceLayouts.size() != instances.size()) {
        std::cerr << "ERROR: Layout metadata size mismatch! Expected " << instances.size() 
                  << " layouts, got " << persistentInstanceLayouts.size() << std::endl;
        return;
    }
    
    // Render each animated instance using stored layout metadata
    for (size_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex) {
        const auto& instance = instances[instanceIndex];
        if (!instance || !instance->getModel()) continue;
        
        // Get layout metadata for this instance (no recalculation!)
        const auto& layout = persistentInstanceLayouts[instanceIndex];
        
        // Get model data
        const auto& meshes = instance->getModel()->getModelMeshes();
        glm::mat4 modelMatrix = instance->getInstanceRootMatrix();
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        
        //  Use stored layout metadata - no offset recalculation!
        std::cout << "LAYOUT-BASED RENDERING: " << layout.debugName 
                  << "vertices[" << layout.vertexOffset << "+" << layout.vertexCount 
                  << "] indices[" << layout.indexOffset << "+" << layout.indexCount
                  << "] bones[" << layout.boneOffset << "+" << layout.boneCount << "]" << std::endl;
        
        struct PushConstantData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            int32_t boneMatrixOffset;  // Offset into bone matrices buffer
        } pushData{ modelMatrix, normalMatrix, static_cast<int32_t>(layout.boneOffset) };  // 🔧 Use layout bone offset

        vkCmdPushConstants(commandBuffer, pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,  // Only vertex stage uses push constants
            0, sizeof(PushConstantData), &pushData);

        // Render this entire instance in one draw call (no per-mesh iteration)
        // Since indices are pre-offset during collection, we use NO vertex offset in draw call
        if (layout.indexCount > 0) {
            // Use indexed drawing - indices already point to correct vertices in global buffer
            vkCmdDrawIndexed(commandBuffer, layout.indexCount, 1, layout.indexOffset, 0, 0);  // ✅ NO vertex offset!
        } else {
            // Fallback to non-indexed drawing if no indices
            std::cout << "DIRECT DRAW: " << layout.vertexCount << " vertices starting at " << layout.vertexOffset << std::endl;
            vkCmdDraw(commandBuffer, layout.vertexCount, 1, layout.vertexOffset, 0);
        }

    }
    
    auto renderEndTime = std::chrono::high_resolution_clock::now();
    auto renderDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(renderEndTime - renderStartTime).count();
    
    totalRenderTime += renderDuration;
    renderCount++;
    
    // Report every 60 renders (~1 second at 60fps)
    if (renderCount >= 60) {
        float avgRenderTime = totalRenderTime / renderCount;
        std::cout << " ANIMATION RENDERING PERFORMANCE (60 renders avg):" << std::endl;
        std::cout << "  Animation rendering: " << avgRenderTime << " ms/frame" << std::endl;
        std::cout << "  Instances rendered: " << instances.size() << " per frame" << std::endl;
        
        // Reset counters
        renderCount = 0;
        totalRenderTime = 0.0f;
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