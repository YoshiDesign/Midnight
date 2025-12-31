#include "AssimpInstance.h"
#include "avpch.h"

namespace aveng {

    AssimpInstance::~AssimpInstance() {}

    AssimpInstance::AssimpInstance() {}

    void AssimpInstance::init(ModelId id, const ModelMeta& meta, const TransformSettings& ts, const AnimSettings& as) {
        InstanceTransform t{};
        t.pos = ts.worldPosition;
        t.rotEuler = ts.worldRotation;
        t.scale = ts.scale;

        common.init(id, meta.root, t);
        anim = as;

        resizeNodeTransformData(meta.boneCount);
#ifdef M_DEBUG
        // For sanity's sake
        ensurePoseStorage(meta.boneCount);
#endif
    }

    //void AssimpInstance::updateModelRootMatrix() {
    //    mLocalScaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(mInstanceSettings.isScale));

    //    if (mInstanceSettings.isSwapYZAxis) {
    //        glm::mat4 flipMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    //        mLocalSwapAxisMatrix = glm::rotate(flipMatrix, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    //    }
    //    else {
    //        mLocalSwapAxisMatrix = glm::mat4(1.0f);
    //    }

    //    // Update Root Rotation
    //    mLocalRotationMatrix = glm::mat4_cast(glm::quat(glm::radians(mInstanceSettings.isWorldRotation)));

    //    // Update Root Translation
    //    mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mInstanceSettings.isWorldPosition);

    //    // Do the math
    //    mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix * mLocalSwapAxisMatrix * mLocalScaleMatrix;

    //    // Final answer
    //    mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
    //}

    void AssimpInstance::ensurePoseStorage(size_t boneCount) {
        if (mNodeTransformData.size() != boneCount) {
            std::cout << "Resizing NodeTransformData\n";
            resizeNodeTransformData(boneCount);
        }
    }

    void AssimpInstance::updateAnimation(float deltaTime, const IModelAnimQuery& animQ) {
        /* Clip metadata + channel list should be stable per (modelId, clipNr) */

        /*
            Consider this: animation data is finally decoupled from the model after introducing IModelAnimQuery.
            This creates a bottleneck, and in inefficient pattern, but it's ok for now.
            There are several ways to improve this. Cacheing + SoA design

            This gets called every update because the animation clipNr could change to begin a different animation.
        */
        if(!animQ.tryGetClipMeta(modelId_, anim.clipNr, animationMeta))
        {
            std::cout << "AssimpInstance::updateAnimation - Failed to retrieve clip meta\n";
            return;
        }

        if (animationMeta.durationTicks <= 0.0f) return; // avoid fmod by zero / negative durations

        anim.playTime += deltaTime * animationMeta.ticksPerSecond * anim.speed;
        if (anim.playTime < 0.0f) anim.playTime += animationMeta.durationTicks;
        anim.playTime = std::fmod(anim.playTime, animationMeta.durationTicks);

        // std::vector<std::shared_ptr<AssimpAnimChannel>> animChannels = mAvengModel->getAnimClips().at(mInstanceSettings.isAnimClipNr)->getChannels();

        clearPoseFast();
        
        /* animate clip via channels */
        for (const auto& channel : animationMeta.animChannels) {

            const int boneId = channel.getBoneId();
            if (boneId < 0) continue;

            NodeTransformData nodeTransform;
            nodeTransform.translation = channel.getTranslation(anim.playTime);
            nodeTransform.rotation = channel.getRotation(anim.playTime);
            nodeTransform.scale = channel.getScaling(anim.playTime);

            mNodeTransformData[boneId] = nodeTransform;
            
        }

        /* set root node transform matrix, enabling instance movement */
        //updateModelRootMatrix();
    }

    void AssimpInstance::clearPoseFast() {
        /*
         * Optimization Path:
         *  Instead of clearing the whole vector:
         *  track which bones were written this frame
         *  treat all others as implicit identity
         * 
         * But this is great for now.
         * Buut if we have the default pose, we could also memcpy it for a slight boost
         */
        std::fill(mNodeTransformData.begin(),
            mNodeTransformData.end(),
            NodeTransformData{});
    }

    std::vector<NodeTransformData> AssimpInstance::getNodeTransformData() {
        return mNodeTransformData;
    }

    const std::vector<NodeTransformData> AssimpInstance::getNodeTransformData() const {
        return mNodeTransformData;
    }

}