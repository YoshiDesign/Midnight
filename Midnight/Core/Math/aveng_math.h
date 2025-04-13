#pragma once
#include "../../avpch.h"

namespace aveng {

	glm::vec3 unitCircleTransform_vec3(float theta, glm::vec3 viewerTranslation, float radius, float modPI, glm::vec3 playerTranslation);
	glm::vec3 unitSphereTransform_vec3(float theta, float omega, float alpha);

}