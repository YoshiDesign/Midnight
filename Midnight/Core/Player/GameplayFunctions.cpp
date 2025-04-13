#include "GameplayFunctions.h"
#include "../data.h"
#include <iostream>
#define LOG(x, y) std::cout << x << "\t" << y << std::endl

namespace aveng {
	
	float GameplayFunctions::torque  = 1.f;
	float GameplayFunctions::deltaPI  = 0.f;
	float GameplayFunctions::deltaRoll  = 100.f;
	float GameplayFunctions::e_out   = 1.f;
	float GameplayFunctions::_nudge  = 0.5f;
	float GameplayFunctions::camera_deltaX = 0.0;
	bool  GameplayFunctions::clamped = false;


	/**/
	glm::vec3 GameplayFunctions::player_spawn_point()
	{
		return { 0.0f, -0.75f, -100.0f };
	}

	/**/
	bool GameplayFunctions::doBarrelRoll(AvengAppObject& appObject, float dt, int dir)
	{
		return true;
	}

	// Delta X vectoring
	float GameplayFunctions::dpi_high(float dt, float rate)
	{
		deltaPI += 0.1f;
		if (deltaPI > 1.0f) deltaPI = 1.0f;
		return (rate + 0.1 * .01) * deltaPI * dt;
	}
	float GameplayFunctions::dpi_low(float dt, float rate)
	{
		deltaPI -= 0.1;
		if (deltaPI < -1.0f) deltaPI = -1.0f;
		return (rate + 0.1 * .01) * deltaPI * dt;
	}

	// Camera delta X vectoring
	float GameplayFunctions::camera_dx_high(float dt, float rate)
	{
		camera_deltaX += 0.1f;
		if (deltaPI > 1.0f) deltaPI = 1.0f;
		return deltaPI * dt * rate;
	}
	float GameplayFunctions::camera_dx_low(float dt, float rate)
	{
		camera_deltaX -= 0.1;
		if (deltaPI < -1.0f) deltaPI = -1.0f;
		return deltaPI * dt * rate;
	}

	// Delta Z vectoring
	float GameplayFunctions::dz_high(float dt)
	{
		// Not sure I'll need this
		if (deltaRoll >=  100.0f)  return  100.0f;
		return ++deltaRoll * dt;
	}
	float GameplayFunctions::dz_low(float dt)
	{
		if (deltaRoll <= 0.0f)  return 0.0f;
		return --deltaRoll * dt;
	}

	/**/
	float GameplayFunctions::counterTorque(float dt, float z)
	{
		// Save last torque state
		torque = z > 0.0f ? -1.0f : 1.0f;

		// Flip the torque if we're beyond 3.14 radians in rotation
		if (abs(z) > PI)
		{
			torque = -torque;
		}

		if (abs(z) < .003f) return 0.0f;
		return (1.9 * torque) * dt;
	}

	float GameplayFunctions::recenterObject(float dt, float x, float vx)
	{
		float d = vx > x ? -.1f : .1f;
		return d * dt;
	}

}