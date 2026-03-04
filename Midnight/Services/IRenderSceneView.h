#pragma once
#include "Core/Asset/AssetRegistry.h"
#include "Core/Renderer/FramePacketBuilder.h"
namespace aveng {

	struct IRenderSceneView {
		virtual ~IRenderSceneView() = default;

		/* Inheriting classes must be capable of providing a model query ref */
		virtual const IModelQuery& modelQuery() const = 0;

		virtual FramePacketBuilder::PoolInputs<StaticTag, AvengInstance>
			staticPoolInputs() = 0;

		virtual FramePacketBuilder::PoolInputs<AnimatedTag, AssimpInstance>
			animatedPoolInputs() = 0;
	};

}