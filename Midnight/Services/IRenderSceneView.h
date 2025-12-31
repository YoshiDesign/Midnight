#pragma once
#include "Core/Modeling/ModelRegistry.h"
#include "Core/Renderer/FramePacketBuilder.h"
namespace aveng {

	struct IRenderSceneView {
		virtual ~IRenderSceneView() = default;

		virtual const IModelQuery& modelQuery() const = 0;

		virtual FramePacketBuilder::PoolInputs<StaticTag, AvengInstance>
			staticPoolInputs() const = 0;

		virtual FramePacketBuilder::PoolInputs<AnimatedTag, AssimpInstance>
			animatedPoolInputs() const = 0;
	};

}