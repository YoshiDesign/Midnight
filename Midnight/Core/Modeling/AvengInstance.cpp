#include "AvengInstance.h"

namespace aveng {

    AvengInstance::AvengInstance(
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