#include "aveng_math.h"

namespace aveng {

	/*
	* Trace a radius around a circle
	*/
	glm::vec3 unitCircleTransform_vec3(float theta, glm::vec3 viewerTranslation, float radius, float modPI, glm::vec3 playerTranslation)
	{
		float degreesXZ = -theta + ((modPI) / 2);	// Yaw
		// if (abs(viewerTranslation.x - playerTranslation.x) > 0.01f)
			// return glm::vec3{ 0.0f, viewerTranslation.y, (viewerTranslation.z + sin(degreesXZ) * radius) };
		// else
			return glm::vec3{ (viewerTranslation.x + cos(degreesXZ) * radius), viewerTranslation.y, (viewerTranslation.z + sin(degreesXZ) * radius )};
	}

	/*
	* Trace a radius around a sphere
	*/
	glm::vec3 unitSphereTransform_vec3(float theta, float omega, float alpha)
	{ 
		float degreexYX = glm::degrees<float>(alpha);	// Roll - Unused so far
		float degreesYZ = glm::degrees<float>(omega);	// Pitch
		float degreesXZ = glm::degrees<float>(theta);	// Yaw
		return glm::vec3{ cos(degreesXZ), sin(degreesXZ), sin(degreesYZ) };
	}

}
