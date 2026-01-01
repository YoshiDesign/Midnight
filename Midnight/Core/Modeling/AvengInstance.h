#pragma once
// #include "Core/Modeling/ModelRegistry.h" provided by InstanceCommon (and aveng_model)
#include "Core/Modeling/InstanceCommon.h"
#include "Core/aveng_model.h"

namespace aveng {
	/*
		For basic static geometry
	*/
	class AvengInstance {
	public:
		InstanceCommon common;

		AvengInstance();
		void init(ModelId id, const ModelMeta& meta, const TransformSettings& ts);
		ModelId modelId() const { return modelId_; }

		// Used by the InstanceManager
		void setModelId(ModelId id) { modelId_ = id; };
		// void setModelRootMatrix(glm::mat4 rootMatrix) { mModelRootMatrix = rootMatrix; };
		
	private:

		ModelId modelId_ = 0;

		glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
		glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
		glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);
		glm::mat4 mLocalSwapAxisMatrix = glm::mat4(1.0f);

		glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);

		glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
		glm::mat4 mModelRootMatrix = glm::mat4(1.0f);
	};
}