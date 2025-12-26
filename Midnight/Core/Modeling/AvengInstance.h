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
			AvengModel* model, 
			glm::vec3 position = glm::vec3(0.0f), 
			glm::vec3 rotation = glm::vec3(0.0f), 
			float modelScale = 1.0f);

		AvengModel* getModel() { return mAvengModel; }
		InstanceSettings getInstanceSettings() { return mInstanceSettings; };
		void setInstanceSettings(const InstanceSettings& is) { mInstanceSettings = is; };

		ModelId modelId() const { return modelId_; }

	private:

		ModelId modelId_ = 0; // Never derive it from AvengModel*, never ask the model for it later.
		AvengModel* mAvengModel;
		InstanceSettings mInstanceSettings{};
		glm::mat4 mModelRootMatrix = glm::mat4(1.0f);
	};
}