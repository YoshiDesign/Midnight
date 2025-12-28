#include "AssimpInstance.h"
#include "avpch.h"

namespace aveng {

    AssimpInstance::~AssimpInstance() { }

    AssimpInstance::AssimpInstance(
        ModelId mid, 
        glm::vec3 position, 
        glm::vec3 rotation, 
        float modelScale)
        : modelId_{ mid }
    {

        mInstanceSettings.isWorldPosition = position;
        mInstanceSettings.isWorldRotation = rotation;
        mInstanceSettings.isScale = modelScale;

        // Not sure if this is necessary yet
        updateModelRootMatrix();
    }

    void AssimpInstance::updateModelRootMatrix() {
        mLocalScaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(mInstanceSettings.isScale));

        if (mInstanceSettings.isSwapYZAxis) {
            glm::mat4 flipMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            mLocalSwapAxisMatrix = glm::rotate(flipMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        }
        else {
            mLocalSwapAxisMatrix = glm::mat4(1.0f);
        }

        // Update Root Rotation
        mLocalRotationMatrix = glm::mat4_cast(glm::quat(glm::radians(mInstanceSettings.isWorldRotation)));

        // Update Root Translation
        mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mInstanceSettings.isWorldPosition);

        // Do the math
        mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix * mLocalSwapAxisMatrix * mLocalScaleMatrix;

        // Final answer
        mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
    }

    void AssimpInstance::updateAnimation(float deltaTime, const IModelAnimQuery& animQ) {

        /*
            Consider this: animation data is finally decoupled from the model after introducing IModelAnimQuery.
            This creates a bottleneck, and in inefficient pattern, but it's ok for now.
            There are several ways to improve this. Cacheing + SoA design
        */
        if(!animQ.tryGetClipMeta(modelId_, mInstanceSettings.isAnimClipNr, animationMeta))
        {
            std::cout << "AssimpInstance::updateAnimation - Failed to retrieve clip meta returned false\n";
            return;
        }

        if (animationMeta.durationTicks <= 0.0f) return; // avoid fmod by zero / negative durations

        mInstanceSettings.isAnimPlayTimePos += deltaTime * animationMeta.ticksPerSecond * mInstanceSettings.isAnimSpeedFactor;
        if (mInstanceSettings.isAnimPlayTimePos < 0.0f) mInstanceSettings.isAnimPlayTimePos += animationMeta.durationTicks;
        mInstanceSettings.isAnimPlayTimePos = std::fmod(mInstanceSettings.isAnimPlayTimePos, animationMeta.durationTicks);

        // std::vector<std::shared_ptr<AssimpAnimChannel>> animChannels = mAvengModel->getAnimClips().at(mInstanceSettings.isAnimClipNr)->getChannels();

        std::fill(mNodeTransformData.begin(), mNodeTransformData.end(), NodeTransformData{});

        /* animate clip via channels */
        for (const auto& channel : animationMeta.animChannels) {
            NodeTransformData nodeTransform;

            nodeTransform.translation = channel->getTranslation(mInstanceSettings.isAnimPlayTimePos);
            nodeTransform.rotation = channel->getRotation(mInstanceSettings.isAnimPlayTimePos);
            nodeTransform.scale = channel->getScaling(mInstanceSettings.isAnimPlayTimePos);

            int boneId = channel->getBoneId();
            if (boneId >= 0) {
                mNodeTransformData.at(boneId) = nodeTransform;
            }
        }

        /* set root node transform matrix, enabling instance movement */
        updateModelRootMatrix();
    }

    void AssimpInstance::setInstanceSettings(const InstanceSettings& settings) {
        // Determine whether we actually need to recompute transforms
        const bool transformDirty =
            settings.isWorldPosition != mInstanceSettings.isWorldPosition ||
            settings.isWorldRotation != mInstanceSettings.isWorldRotation ||
            settings.isScale != mInstanceSettings.isScale ||
            settings.isSwapYZAxis != mInstanceSettings.isSwapYZAxis;

        // Always copy settings (anim changes are cheap & valid)
        mInstanceSettings = settings;

        // Only rebuild matrices if something transform-related changed
        if (transformDirty) {
            updateModelRootMatrix();
        }
    }

    glm::vec3 AssimpInstance::getWorldPosition() {
        return mInstanceSettings.isWorldPosition;
    }

    glm::mat4 AssimpInstance::getWorldTransformMatrix() {
        return mInstanceRootMatrix;
    }

    void AssimpInstance::setTranslation(glm::vec3 position) {
        mInstanceSettings.isWorldPosition = position;
        updateModelRootMatrix();
    }

    void AssimpInstance::setRotation(glm::vec3 rotation) {
        mInstanceSettings.isWorldRotation = rotation;
        updateModelRootMatrix();
    }

    void AssimpInstance::setScale(float scale) {
        mInstanceSettings.isScale = scale;
        updateModelRootMatrix();
    }

    void AssimpInstance::setSwapYZAxis(bool value) {
        mInstanceSettings.isSwapYZAxis = value;
        updateModelRootMatrix();
    }

    glm::vec3 AssimpInstance::getRotation() {
        return mInstanceSettings.isWorldRotation;
    }

    glm::vec3 AssimpInstance::getTranslation() {
        return mInstanceSettings.isWorldPosition;
    }

    float AssimpInstance::getScale() {
        return mInstanceSettings.isScale;
    }

    bool AssimpInstance::getSwapYZAxis() {
        return mInstanceSettings.isSwapYZAxis;
    }

    std::vector<NodeTransformData> AssimpInstance::getNodeTransformData() {
        return mNodeTransformData;
    }

}