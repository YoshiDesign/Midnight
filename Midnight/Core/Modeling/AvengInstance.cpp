#include "AvengInstance.h"

namespace aveng {

    AvengInstance::AvengInstance()
	{
	
	}

    void AvengInstance::init(ModelId id, const ModelMeta& meta, const TransformSettings& ts) {
        InstanceTransform t{};
        t.pos = ts.worldPosition;
        t.rotEuler = ts.worldRotation;
        t.scale = ts.scale;

        // Note: We're ignoring the swapYZ flag for now.

        common.init(id, meta.root, t);
    }
    void AvengInstance::updateModelRootMatrix() {
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

}