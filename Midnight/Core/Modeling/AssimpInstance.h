#pragma once
#include "avpch.h"
#include "Core/aveng_model.h"
#include "Core/Modeling/AssimpNode.h"
#include "Core/Modeling/AssimpBone.h"
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
        ~AssimpInstance() = default;

        void init(ModelId id, const ModelMeta& meta, const TransformSettings& ts, const AnimSettings& as);

        ModelId modelId() const             { return modelId_; }
        void setAnimClipNr(uint32_t clipNr) { anim.clipNr = clipNr; }
        void setModelId(ModelId id)         { modelId_ = id; };
        void resizeNodeTransformData(unsigned int boneCount) { mNodeTransformData.resize(boneCount); };

        void setTranslation(glm::vec3 position);
        void setRotation(glm::vec3 rotation);
        void setScale(float scale);
        void setSwapYZAxis(bool value);

        glm::vec3 getWorldPosition();
        glm::mat4 getWorldTransformMatrix();
        std::vector<NodeTransformData> getNodeTransformData();
        const std::vector<NodeTransformData> getNodeTransformData() const;

        //glm::vec3 getTranslation();
        //glm::vec3 getRotation();
        //float getScale();
        //bool getSwapYZAxis();

        void updateModelRootMatrix();
        void updateAnimation(float deltaTime, const IModelAnimQuery& animQ);

        void ensurePoseStorage(size_t boneCount);
       
        void clearPoseFast();
        
    private:

        ModelId modelId_ = 0; // Never derive it from AvengModel*, never ask the model for it later.
        struct AnimationMeta animationMeta {}; // Used for queries

        // Sent to the Compute Shader - bone TRS transforms
        std::vector<NodeTransformData> mNodeTransformData{};
    };

}