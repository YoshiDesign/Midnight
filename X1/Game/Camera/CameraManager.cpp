#include "CameraManager.h"
#include "avpch.h"

namespace aveng {

	CameraId CameraManager::createCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver) {
		
		cameras_.push_back(
			{ // CameraSlot rvalue
				AvengCamera{},
				CameraTransform{},
				std::move(cameraDriver),
				std::move(name),
				false
			}
		);

		return CameraId{ static_cast<uint32_t>(cameras_.size() - 1) };
	}

}