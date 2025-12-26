#include "AvengInstance.h"

namespace aveng {

    AvengInstance::AvengInstance(
        ModelId mid, 
        AvengModel* model, 
        glm::vec3 position, 
        glm::vec3 rotation, 
        float modelScale) 
    : mAvengModel{ model }, modelId_{mid}
	{
        if (!model) {
            // std::printf("%s error: invalid model given\n", __FUNCTION__);
            return;
        }
        mInstanceSettings.isWorldPosition = position;
        mInstanceSettings.isWorldRotation = rotation;
        mInstanceSettings.isScale = modelScale;

        /* avoid resizes during fill */
        // mNodeTransformData.resize(mAvengModel->getBoneList().size());

        /* save model root matrix */
        mModelRootMatrix = mAvengModel->getRootTranformationMatrix();

        // updateModelRootMatrix();
	
	}

}