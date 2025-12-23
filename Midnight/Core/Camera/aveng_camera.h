#pragma once
#include "avpch.h"

namespace aveng {

	struct CameraTransform {
		glm::vec3 translation{ 0.f };
		glm::vec3 rotation{ 0.f };   // YXZ convention
		glm::vec3 scale{ 1.f };
	};

	class AvengCamera {
	
	public:
		
		void setOrthographicProjection(float left, float right, float top, float bottom, float near, float far);
		void setPerspectiveProjection(float fovy, float aspect, float near, float far);

		void setViewDirection(glm::vec3 position, glm::vec3 direction, glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
		void setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up = glm::vec3{ 0.f, -1.f, 0.f });
		void setViewYXZ(glm::vec3 position, glm::vec3 rotation);

		void setTransform(CameraTransform& transform);
		const CameraTransform& transform() const { return transform_; }

		const glm::mat4& getProjection() const { return projectionMatrix; }
		const glm::mat4& getView() const { return viewMatrix; }
		const glm::vec4 getCameraView();
			
	private:
		CameraTransform transform_{};
		glm::mat4 projectionMatrix{ 1.f };
		glm::mat4 viewMatrix{ 1.f };
		bool dirtyView_ = true;
		
	};


}