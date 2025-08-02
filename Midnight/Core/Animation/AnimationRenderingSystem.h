#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include "../data.h"
#include "AssimpInstance.h"

namespace aveng {

// Forward declarations
class EngineDevice;
class ComputePipeline;
class AvengBuffer;
class AvengDescriptorSetLayout;
class AvengDescriptorPool;
class GFXPipeline;
class PipelineConfigManager;

// 🔧 FIXED: Persistent layout tracking to eliminate data flow inconsistencies
struct InstanceLayout {
    uint32_t vertexOffset;     // Where this instance starts in global vertex buffer
    uint32_t vertexCount;      // How many vertices this instance has  
    uint32_t indexOffset;      // Where this instance's indices start in global index buffer
    uint32_t indexCount;       // How many indices this instance has
    uint32_t boneOffset;       // Where this instance's bone matrices start
    uint32_t boneCount;        // How many bone matrices this instance has
    
    // Debug information
    std::string debugName;     // For debugging - instance/model name
};

/**
 * Dedicated animation rendering system that handles:
 * - Animation data upload to GPU buffers
 * - Compute shader dispatch for skeletal animation
 * - Animated model rendering
 * - Animation descriptor set management
 */
class AnimationRenderingSystem {
public:
    AnimationRenderingSystem(EngineDevice& device);
    ~AnimationRenderingSystem();

    AnimationRenderingSystem(const AnimationRenderingSystem&) = delete;
    AnimationRenderingSystem& operator=(const AnimationRenderingSystem&) = delete;

    // Initialization - called during renderer setup
    void initializeDescriptors(AvengDescriptorPool& descriptorPool, int maxFramesInFlight);
    void createAnimationComputePipeline(VkPipelineLayout pipelineLayout);
    
    // Per-frame operations
    void updateAnimationData(const std::vector<std::shared_ptr<AssimpInstance>>& instances, 
                           float deltaTime, int currentFrameIndex);
    void dispatchAnimationCompute(VkCommandBuffer commandBuffer, uint32_t vertexCount, 
                                VkPipelineLayout pipelineLayout, int currentFrameIndex);
    void renderAnimatedModels(VkCommandBuffer commandBuffer, 
                            const std::vector<std::shared_ptr<AssimpInstance>>& instances,
                            VkPipelineLayout pipelineLayout, 
                            PipelineConfigManager* pipelineManager,
                            int currentObjectMode, int currentFrameIndex);

    // Getters for descriptor set integration
    VkDescriptorSetLayout getAnimationDescriptorSetLayout() const;
    VkDescriptorSet getAnimationDescriptorSet(int frameIndex) const;
    
    // Statistics and debug info
    uint32_t getLastVertexCount() const { return lastVertexCount; }
    uint32_t getLastInstanceCount() const { return lastInstanceCount; }

private:
    // Core setup methods
    void createAnimationDescriptorSetLayout();
    void createAnimationBuffers(int maxFramesInFlight);
    void setupAnimationDescriptorSets(AvengDescriptorPool& descriptorPool, int maxFramesInFlight);

    // Helper methods
    uint32_t calculateTotalVertices(const std::vector<std::shared_ptr<AssimpInstance>>& instances);
    
    EngineDevice& engineDevice;
    
    // Animation compute pipeline
    std::unique_ptr<ComputePipeline> animationComputePipeline;
    
    // Descriptor set layout and sets
    std::unique_ptr<AvengDescriptorSetLayout> animationDescriptorSetLayout;
    std::vector<VkDescriptorSet> animationDescriptorSets;
    
    // Animation-specific buffers (per frame in flight)
    std::vector<std::unique_ptr<AvengBuffer>> u_AnimationBuffers;     // Animation UBO
    std::vector<std::unique_ptr<AvengBuffer>> boneMatrixBuffers;      // Bone transformation matrices
    std::vector<std::unique_ptr<AvengBuffer>> instanceAnimationBuffers; // Per-instance animation data
    std::vector<std::unique_ptr<AvengBuffer>> animatedVertexBuffers;  // Input animated vertices
    std::vector<std::unique_ptr<AvengBuffer>> transformedVertexBuffers; // Output transformed vertices (from compute shader)
    std::vector<std::unique_ptr<AvengBuffer>> animatedIndexBuffers;   // Index buffers for animated meshes
    
    // Statistics for debugging
    uint32_t lastVertexCount = 0;
    uint32_t lastInstanceCount = 0;
    
    bool initialized = false;
    bool staticDataUploaded = false; // PERFORMANCE: Track if mesh data uploaded once
    
    // 🔧 FIXED: Persistent layout tracking to fix data flow inconsistencies
    std::vector<InstanceLayout> persistentInstanceLayouts;  // Tracks where each instance's data lives in global buffers
};

} // namespace aveng 