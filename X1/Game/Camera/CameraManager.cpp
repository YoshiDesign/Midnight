#include "CameraManager.h"
#include "avpch.h"

namespace aveng {

	CameraId CameraManager::createCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver) {
		
		CameraTransform transform{};
		cameras_.push_back(
			{ // CameraSlot rvalue
				AvengCamera{},
				transform,
				std::move(cameraDriver),
				std::move(name),
				false
			}
		);

		transformsByName_[name] = transform;

		return CameraId{ static_cast<uint32_t>(cameras_.size() - 1) };
	}

}