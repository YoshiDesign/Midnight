#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include "Utils/glm_includes.h"
#include "Core/Modeling/ModelRegistry.h"
#include "Core/aveng_model.h"
#include "./AssimpNode.h"
#include "./AssimpBone.h"
#include "Core/Modeling/InstanceCommon.h"

namespace aveng {
    class AssimpInstance {

        /*
        * We need to isolate the Animation component of this instance class
        * so we can combine it with AvengInstance more easily.
        */

    public:

        InstanceCommon common;
        AnimSettings anim{};

        AssimpInstance();

        void init(ModelId id, const ModelMeta& meta, const TransformSettings& ts, const AnimSettings& as);

        ModelId modelId() const { return modelId_; }

        glm::vec3 getWorldPosition();
        glm::mat4 getWorldTransformMatrix();

        void setTranslation(glm::vec3 position);
        void setRotation(glm::vec3 rotation);
        void setScale(float scale);
        void setSwapYZAxis(bool value);

        glm::vec3 getTranslation();
        glm::vec3 getRotation();
        float getScale();
        bool getSwapYZAxis();

        std::vector<NodeTransformData> getNodeTransformData();

        void setInstanceSettings(const InstanceSettings& settings);
        InstanceSettings instanceSettings() const { return mInstanceSettings; };

        void updateModelRootMatrix();
        void updateAnimation(float deltaTime, const IModelAnimQuery& animQ);

        void setAnimClipNr(uint32_t clipNr) {
            mInstanceSettings.isAnimClipNr = clipNr;
        }

        // Used by the InstanceManager
        void setModelId(ModelId id) { modelId_ = id; };
        void setModelRootMatrix(glm::mat4 rootMatrix) { mModelRootMatrix = rootMatrix; };
        void ensurePoseStorage(size_t boneCount);
        void resizeNodeTransformData(unsigned int boneCount) { mNodeTransformData.resize(boneCount); };
        void clearPoseFast();
        
    private:

        ModelId modelId_ = 0; // Never derive it from AvengModel*, never ask the model for it later.
        InstanceSettings mInstanceSettings{}; 
        //float tps = 0.0f;
        //float animationDuration = 0.0f;
        //std::vector<std::shared_ptr<AssimpAnimChannel>> animChannels;

        struct AnimationMeta animationMeta {}; // Used for queries

        glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);
        glm::mat4 mLocalSwapAxisMatrix = glm::mat4(1.0f);

        // Local TRS transforms
        glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);

        glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
        glm::mat4 mModelRootMatrix = glm::mat4(1.0f); // a "model constant" found in the instance's ModelMeta

        // Sent to the Compute Shader - bone TRS transforms
        std::vector<NodeTransformData> mNodeTransformData{};
    };

}