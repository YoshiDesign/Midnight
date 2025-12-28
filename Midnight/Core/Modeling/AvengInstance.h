#pragma once
#include "Core/Modeling/ModelRegistry.h"
#include "Core/aveng_model.h"
#include "InstanceSettings.h"

namespace aveng {
	/*
		For basic static geometry
	*/
	class AvengInstance {
	public:
		AvengInstance(
			ModelId mid, 
			glm::vec3 position = glm::vec3(0.0f), 
			glm::vec3 rotation = glm::vec3(0.0f), 
			float modelScale = 1.0f);

		ModelId modelId() const { return modelId_; }
		InstanceSettings instanceSettings() const { return mInstanceSettings; };
		void setInstanceSettings(const InstanceSettings& is) { mInstanceSettings = is; };
		void updateModelRootMatrix();
		
	private:

		ModelId modelId_ = 0;
		InstanceSettings mInstanceSettings{};

		glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
		glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
		glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);
		glm::mat4 mLocalSwapAxisMatrix = glm::mat4(1.0f);

		glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);

		glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
		glm::mat4 mModelRootMatrix = glm::mat4(1.0f);
	};
}