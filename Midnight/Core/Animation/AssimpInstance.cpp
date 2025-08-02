#include "AssimpInstance.h"
#include "Logger.h"
#include "Tools.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <iostream>
#include <cmath>  // For fmod
#include <unordered_map>  // For bone name to index mapping
#include <algorithm>  // For std::min

namespace aveng {

// Static debug control initialization
int AssimpInstance::sGeometryDebugLevel = 1;  // Default: show bone matrix analysis

AssimpInstance::AssimpInstance(std::shared_ptr<AssimpModel> model, glm::vec3 position, glm::vec3 rotation, float modelScale) 
    : mAssimpModel(model) {
    if (!model) {
        Logger::log(0, "AssimpInstance: Cannot create instance with null model\n");
        return;
    }

    // Initialize instance settings
    mInstanceSettings.isWorldPosition = position;
    mInstanceSettings.isWorldRotation = rotation;
    mInstanceSettings.isScale = modelScale;

    // Initialize animation data with memory pre-allocation optimization
    const auto& boneList = mAssimpModel->getBoneList();
    const size_t boneCount = boneList.size();
    
    // Pre-allocate exact memory to avoid reallocations during animation
    mNodeTransformData.resize(boneCount);
    mBoneTransformMatrices.resize(boneCount, glm::mat4(1.0f));
    
    // Initialize NodeTransformData with default values for better cache performance
    NodeTransformData defaultTransform;
    defaultTransform.translation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    defaultTransform.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  // Identity quaternion
    defaultTransform.scale = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    std::fill(mNodeTransformData.begin(), mNodeTransformData.end(), defaultTransform);

    // Save model root matrix
    mModelRootMatrix = mAssimpModel->getRootTransformationMatrix();

    // Initialize current animation state
    if (mAssimpModel->hasAnimations() && !mAssimpModel->getAnimClips().empty()) {
        mCurrentAnimationClip = mAssimpModel->getAnimClips()[0];
        Logger::log(1, "AssimpInstance: Initialized with animation '%s'\n", 
                   mCurrentAnimationClip->getClipName().c_str());
    }

    updateModelRootMatrix();
    
    Logger::log(1, "AssimpInstance: Created instance for model '%s' with %d bones\n", 
               mAssimpModel->getModelFileName().c_str(), boneList.size());
}

void AssimpInstance::updateModelRootMatrix() {
    // Build transformation matrices
    mLocalScaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(mInstanceSettings.isScale));

    // Handle coordinate system conversion if needed
    if (mInstanceSettings.isSwapYZAxis) {
        glm::mat4 flipMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        mLocalSwapAxisMatrix = glm::rotate(flipMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    } else {
        mLocalSwapAxisMatrix = glm::mat4(1.0f);
    }

    // Rotation
    mLocalRotationMatrix = glm::mat4_cast(glm::quat(glm::radians(mInstanceSettings.isWorldRotation)));

    // Translation  
    mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mInstanceSettings.isWorldPosition);

    // Combine transformations
    mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix * mLocalSwapAxisMatrix * mLocalScaleMatrix;
    mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
}

void AssimpInstance::updateAnimation(float deltaTime) {
    // Robust validation and error handling
    if (!mAssimpModel) {
        Logger::log(0, "AssimpInstance: Cannot update animation - no model loaded\n");
        return;
    }
    
    if (!mCurrentAnimationClip) {
        Logger::log(2, "AssimpInstance: No animation clip selected for updating\n");
        return;
    }
    
    if (mNodeTransformData.empty()) {
        Logger::log(0, "AssimpInstance: NodeTransformData not initialized\n");
        return;
    }

    // PERFORMANCE: Time critical animation operations
    static int callCounter = 0;
    static float totalBoneTime = 0.0f;
    
    // Time handling with validation
    float speedFactor = mInstanceSettings.isAnimSpeedFactor;
    if (speedFactor < 0.0f) {
        Logger::log(1, "AssimpInstance: Warning - negative speed factor %f, clamping to 0\n", speedFactor);
        speedFactor = 0.0f;
    }
    
    // Update animation time
    mInstanceSettings.isAnimPlayTimePos += deltaTime * speedFactor;
    
    // Handle negative time (edge case??)
    if (mInstanceSettings.isAnimPlayTimePos < 0.0f) {
        Logger::log(2, "AssimpInstance: Negative animation time %f, wrapping to positive\n", 
                   mInstanceSettings.isAnimPlayTimePos);
        float duration = mCurrentAnimationClip->getClipDuration();
        float ticksPerSecond = mCurrentAnimationClip->getClipTicksPerSecond();
        if (ticksPerSecond > 0.0f && duration > 0.0f) {
            float durationInSeconds = duration / ticksPerSecond;
            while (mInstanceSettings.isAnimPlayTimePos < 0.0f) {
                mInstanceSettings.isAnimPlayTimePos += durationInSeconds;
            }
        } else {
            mInstanceSettings.isAnimPlayTimePos = 0.0f;
        }
    }
    
    auto boneStartTime = std::chrono::high_resolution_clock::now();
    
    // Convert time to ticks with validation
    float ticksPerSecond = mCurrentAnimationClip->getClipTicksPerSecond();
    float duration = mCurrentAnimationClip->getClipDuration();
    
    if (ticksPerSecond <= 0.0f || duration <= 0.0f) {
        Logger::log(1, "AssimpInstance: Invalid animation timing - ticks: %f, duration: %f\n", 
                   ticksPerSecond, duration);
        return;
    }
    
    float wrappedTime = fmod(mInstanceSettings.isAnimPlayTimePos * ticksPerSecond, duration);
    if (wrappedTime < 0.0f) {
        wrappedTime += duration; // Ensure positive wrapped time
    }
    
    // 🔧 FIXED: HIERARCHICAL CPU BONE CALCULATION - Proper skeletal animation
    
    // Step 1: Update animation channels using fast node map lookups
    updateAnimationChannels(wrappedTime);
    
    // Step 2: Calculate bone transforms hierarchically using node list traversal
    calculateBoneTransformsHierarchical();

    
    // Update instance root matrix for positioning (like reference)
    updateModelRootMatrix();
    
    // Optimize cache performance for next frame
    optimizeAnimationCache();
    
    auto boneEndTime = std::chrono::high_resolution_clock::now();
    float boneTimeMs = std::chrono::duration<float, std::milli>(boneEndTime - boneStartTime).count();
    totalBoneTime += boneTimeMs;
    callCounter++;
    
    if (callCounter % 120 == 0) {
        const auto& nodeList = mAssimpModel->getNodeList();
        const auto& boneList = mAssimpModel->getBoneList();
        const auto& animChannels = mCurrentAnimationClip->getChannels();
        
        std::cout << "🔧 HIERARCHICAL ANIMATION PERFORMANCE (" << callCounter << " calls avg):" << std::endl;
        std::cout << "  Hierarchical bone calculation: " << totalBoneTime / callCounter << " ms/call" << std::endl;
        std::cout << "  Animation channels processed: " << animChannels.size() << std::endl;
        std::cout << "  Nodes in hierarchy: " << nodeList.size() << std::endl; 
        std::cout << "  Bones calculated: " << boneList.size() << std::endl;
        std::cout << "  Total per instance: " << totalBoneTime / callCounter << " ms/call" << std::endl;
        std::cout << "  Est. per frame (3 instances): " << totalBoneTime / callCounter * 3 << " ms/frame" << std::endl;
    }
}

void AssimpInstance::updateAnimationChannels(float wrappedTime) {
    // 🔧 PHASE 1: Use mNodeMap for O(1) animation channel updates
    const auto& nodeMap = mAssimpModel->getNodeMap();
    const auto& animChannels = mCurrentAnimationClip->getChannels();
    
    Logger::log(2, "AssimpInstance: Updating %zu animation channels at time %.3f\n", 
               animChannels.size(), wrappedTime);
    
    // Update only animated nodes, preserve bind pose for others
    for (const auto& channel : animChannels) {
        std::string nodeName = channel->getTargetNodeName();
        
        // Fast O(1) lookup in node map
        if (auto nodeIter = nodeMap.find(nodeName); nodeIter != nodeMap.end()) {
            auto node = nodeIter->second;
            
            // Get interpolated animation data
            glm::vec3 pos = channel->getInterpolatedPosition(wrappedTime);
            glm::quat rot = channel->getInterpolatedRotation(wrappedTime);
            glm::vec3 scale = channel->getInterpolatedScale(wrappedTime);
            
            // Update node's local transform using existing AssimpNode methods
            node->setTranslation(pos);    // ✅ Uses existing infrastructure
            node->setRotation(rot);
            node->setScaling(scale);
            
            Logger::log(3, "  Updated node '%s': pos(%.2f,%.2f,%.2f) rot(%.2f,%.2f,%.2f,%.2f) scale(%.2f,%.2f,%.2f)\n",
                       nodeName.c_str(), pos.x, pos.y, pos.z, rot.x, rot.y, rot.z, rot.w, scale.x, scale.y, scale.z);
        } else {
            Logger::log(1, "AssimpInstance: Warning - Animation channel targets unknown node '%s'\n", nodeName.c_str());
        }
    }
}

void AssimpInstance::calculateBoneTransformsHierarchical() {
    // 🔧 PHASE 2: Use mNodeList for hierarchical traversal in correct order
    const auto& nodeList = mAssimpModel->getNodeList();  // ✅ Preserves hierarchy order!
    const auto& boneList = mAssimpModel->getBoneList();
    
    // Clear and resize bone matrices
    mBoneTransformMatrices.clear();
    mBoneTransformMatrices.resize(boneList.size(), glm::mat4(1.0f));
    
    // Create bone name to index mapping for fast lookup
    std::unordered_map<std::string, int> boneNameToIndex;
    for (size_t i = 0; i < boneList.size(); ++i) {
        boneNameToIndex[boneList[i]->getBoneName()] = i;
    }
    
    Logger::log(2, "AssimpInstance: Calculating bone transforms for %zu nodes, %zu bones\n", 
               nodeList.size(), boneList.size());
    
    // Traverse nodes in hierarchy order - parents processed before children
    int processedBones = 0;
    for (const auto& node : nodeList) {
        // Update the node's TRS matrix (accumulates parent transforms automatically)
        node->updateTRSMatrix();  // ✅ Uses existing parent-child logic!
        
        // Check if this node corresponds to a bone
        std::string nodeName = node->getNodeName();
        if (auto boneIter = boneNameToIndex.find(nodeName); boneIter != boneNameToIndex.end()) {
            int boneIndex = boneIter->second;
            
            // Get the world transform (with accumulated parent transforms)
            glm::mat4 worldTransform = node->getTRSMatrix();
            
            // Apply bone offset matrix (inverse bind pose)
            glm::mat4 offsetMatrix = boneList[boneIndex]->getOffsetMatrix();
            mBoneTransformMatrices[boneIndex] = worldTransform * offsetMatrix;
            
            processedBones++;
            
            Logger::log(3, "  Bone %d ('%s'): World transform calculated with hierarchy\n", 
                       boneIndex, nodeName.c_str());
        }
    }
    
    Logger::log(2, "AssimpInstance: Processed %d/%zu bones in hierarchical order\n", 
               processedBones, boneList.size());
    
    // Validation: Check for missing bones
    if (processedBones != static_cast<int>(boneList.size())) {
        Logger::log(1, "AssimpInstance: Warning - %zu bones defined but only %d processed. Some bones may not have corresponding nodes.\n",
                   boneList.size(), processedBones);
    }
    
    // Debug: Sample bone matrix validation (first 3 bones)
    static int hierarchyCallCounter = 0;
    hierarchyCallCounter++;
    if (sGeometryDebugLevel >= 1 && hierarchyCallCounter % 60 == 0) {
        for (size_t i = 0; i < std::min(size_t(3), mBoneTransformMatrices.size()); ++i) {
            const auto& matrix = mBoneTransformMatrices[i];
            float determinant = glm::determinant(matrix);
            Logger::log(1, "🔧 HIERARCHICAL BONE %zu ('%s'): det=%.6f\n", 
                       i, boneList[i]->getBoneName().c_str(), determinant);
            
            if (std::abs(determinant) < 0.001f) {
                Logger::log(0, "  ⚠️  WARNING: Bone matrix near-singular (det=%.6f) - potential deformation issue!\n", determinant);
            }
        }
    }
}

void AssimpInstance::updateNodeTransformations() {
    const auto& nodeList = mAssimpModel->getNodeList();
    
    // Update all nodes with current animation state
    for (auto& node : nodeList) {
        node->setRootTransformMatrix(mInstanceRootMatrix);
        node->updateTRSMatrix();
    }
}

void AssimpInstance::setAnimation(const std::string& animationName) {
    const auto& animClips = mAssimpModel->getAnimClips();
    
    for (const auto& clip : animClips) {
        if (clip->getClipName() == animationName) {
            mCurrentAnimationClip = clip;
            mInstanceSettings.isAnimPlayTimePos = 0.0f; // Reset animation time
            Logger::log(1, "AssimpInstance: Switched to animation '%s'\n", animationName.c_str());
            return;
        }
    }
    
    Logger::log(0, "AssimpInstance: Animation '%s' not found\n", animationName.c_str());
}

void AssimpInstance::setAnimationByIndex(unsigned int index) {
    const auto& animClips = mAssimpModel->getAnimClips();
    
    if (index < animClips.size()) {
        mCurrentAnimationClip = animClips[index];
        mInstanceSettings.isAnimPlayTimePos = 0.0f;
        Logger::log(1, "AssimpInstance: Switched to animation %d ('%s')\n", 
                   index, mCurrentAnimationClip->getClipName().c_str());
    } else {
        Logger::log(0, "AssimpInstance: Animation index %d out of range\n", index);
    }
}

std::shared_ptr<AssimpModel> AssimpInstance::getModel() {
    return mAssimpModel;
}

void AssimpInstance::optimizeAnimationCache() {
    // Pre-touch memory to improve cache locality for next frame
    // This helps with cache warming for frequently accessed data
    
    if (!mNodeTransformData.empty()) {
        // Touch first and last elements to ensure the entire array is in cache
        volatile NodeTransformData* first = &mNodeTransformData.front();
        volatile NodeTransformData* last = &mNodeTransformData.back();
        (void)first;  // Prevent unused variable warning
        (void)last;   // Prevent unused variable warning
    }
    
    if (!mBoneTransformMatrices.empty()) {
        // Touch bone matrices for cache warming
        volatile glm::mat4* first = &mBoneTransformMatrices.front();
        volatile glm::mat4* last = &mBoneTransformMatrices.back();
        (void)first;  // Prevent unused variable warning
        (void)last;   // Prevent unused variable warning
    }
    
    // Optimize animation channels for next update
    if (mCurrentAnimationClip) {
        const auto& channels = mCurrentAnimationClip->getChannels();
        for (const auto& channel : channels) {
            // Touch channel data to warm cache (bone ID access pattern)
            volatile int boneId = channel->getBoneId();
            (void)boneId;  // Prevent unused variable warning
        }
    }
}

bool AssimpInstance::validateAnimationData() const {
    // Comprehensive validation of animation system state
    
    if (!mAssimpModel) {
        Logger::log(0, "AssimpInstance: Validation failed - no model loaded\n");
        return false;
    }
    
    if (mNodeTransformData.size() != mAssimpModel->getBoneList().size()) {
        Logger::log(0, "AssimpInstance: Validation failed - NodeTransformData size mismatch (%zu vs %zu bones)\n",
                   mNodeTransformData.size(), mAssimpModel->getBoneList().size());
        return false;
    }
    
    if (mBoneTransformMatrices.size() != mAssimpModel->getBoneList().size()) {
        Logger::log(0, "AssimpInstance: Validation failed - BoneTransformMatrices size mismatch (%zu vs %zu bones)\n",
                   mBoneTransformMatrices.size(), mAssimpModel->getBoneList().size());
        return false;
    }
    
    // Validate animation clip
    if (mCurrentAnimationClip) {
        const auto& channels = mCurrentAnimationClip->getChannels();
        for (const auto& channel : channels) {
            int boneId = channel->getBoneId();
            if (boneId >= 0 && boneId >= static_cast<int>(mNodeTransformData.size())) {
                Logger::log(0, "AssimpInstance: Validation failed - Channel bone ID %d out of range (max: %zu)\n",
                           boneId, mNodeTransformData.size());
                return false;
            }
        }
        
        // Validate timing
        float duration = mCurrentAnimationClip->getClipDuration();
        float ticksPerSecond = mCurrentAnimationClip->getClipTicksPerSecond();
        if (duration <= 0.0f || ticksPerSecond <= 0.0f) {
            Logger::log(0, "AssimpInstance: Validation failed - Invalid animation timing (duration: %f, ticks: %f)\n",
                       duration, ticksPerSecond);
            return false;
        }
    }
    
    // Validate bone matrices for NaN/Inf
    for (size_t i = 0; i < mBoneTransformMatrices.size(); ++i) {
        const auto& matrix = mBoneTransformMatrices[i];
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                float value = matrix[col][row];
                if (std::isnan(value) || std::isinf(value)) {
                    Logger::log(0, "AssimpInstance: Validation failed - Bone matrix %zu contains invalid value at [%d][%d]: %f\n",
                               i, row, col, value);
                    return false;
                }
            }
        }
    }
    
    Logger::log(2, "AssimpInstance: Animation data validation passed\n");
    return true;
}

std::string AssimpInstance::getAnimationReport() const {
    std::string report = "=== AssimpInstance Animation Report ===\n";
    
    if (!mAssimpModel) {
        report += "Status: NO MODEL LOADED\n";
        return report;
    }
    
    report += "Model: " + mAssimpModel->getModelFileName() + "\n";
    report += "Bones: " + std::to_string(mAssimpModel->getBoneList().size()) + "\n";
    report += "Nodes: " + std::to_string(mAssimpModel->getNodeList().size()) + "\n";
    report += "Animations: " + std::to_string(mAssimpModel->getAnimClips().size()) + "\n";
    
    // Instance data sizes
    report += "NodeTransformData size: " + std::to_string(mNodeTransformData.size()) + "\n";
    report += "BoneTransformMatrices size: " + std::to_string(mBoneTransformMatrices.size()) + "\n";
    
    // Current animation state
    if (mCurrentAnimationClip) {
        report += "Current Animation: " + mCurrentAnimationClip->getClipName() + "\n";
        report += "Animation Time: " + std::to_string(mInstanceSettings.isAnimPlayTimePos) + " seconds\n";
        report += "Speed Factor: " + std::to_string(mInstanceSettings.isAnimSpeedFactor) + "\n";
        report += "Duration: " + std::to_string(mCurrentAnimationClip->getClipDuration()) + " ticks\n";
        report += "Ticks/Second: " + std::to_string(mCurrentAnimationClip->getClipTicksPerSecond()) + "\n";
        report += "Channels: " + std::to_string(mCurrentAnimationClip->getChannels().size()) + "\n";
    } else {
        report += "Current Animation: NONE\n";
    }
    
    // Memory usage estimation
    size_t nodeDataMemory = mNodeTransformData.size() * sizeof(NodeTransformData);
    size_t boneMatrixMemory = mBoneTransformMatrices.size() * sizeof(glm::mat4);
    size_t totalMemory = nodeDataMemory + boneMatrixMemory;
    
    report += "Memory Usage:\n";
    report += "  NodeTransformData: " + std::to_string(nodeDataMemory) + " bytes\n";
    report += "  BoneTransformMatrices: " + std::to_string(boneMatrixMemory) + " bytes\n";
    report += "  Total Animation Data: " + std::to_string(totalMemory) + " bytes\n";
    
    report += "========================================\n";
    
    return report;
}

void AssimpInstance::debugAnimationState() const {
    Logger::log(1, "%s", getAnimationReport().c_str());
    
    // Additional debug output for development
    if (mCurrentAnimationClip) {
        const auto& channels = mCurrentAnimationClip->getChannels();
        Logger::log(2, "Channel Details:\n");
        for (size_t i = 0; i < channels.size() && i < 5; ++i) { // Limit to first 5 channels
            const auto& channel = channels[i];
            Logger::log(2, "  Channel %zu: '%s' -> Bone ID %d\n", 
                       i, channel->getTargetNodeName().c_str(), channel->getBoneId());
        }
        if (channels.size() > 5) {
            Logger::log(2, "  ... and %zu more channels\n", channels.size() - 5);
        }
    }
    
    // Sample bone matrix debug (first 3 bones)
    Logger::log(2, "Sample Bone Matrices:\n");
    for (size_t i = 0; i < mBoneTransformMatrices.size() && i < 3; ++i) {
        const auto& matrix = mBoneTransformMatrices[i];
        Logger::log(2, "  Bone %zu: [%.3f, %.3f, %.3f, %.3f]\n", i,
                   matrix[0][0], matrix[1][0], matrix[2][0], matrix[3][0]);
    }
}
}