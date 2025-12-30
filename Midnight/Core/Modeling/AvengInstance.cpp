#include "AvengInstance.h"

namespace aveng {

    AvengInstance::AvengInstance()
	{
	
	}

    void AvengInstance::init(ModelId id, const ModelMeta& meta, const TransformSettings& ts) {
        InstanceTransform t{};
        t.pos = ts.worldPosition;
        t.rotEuler = ts.worldRotation;
        t.scale = ts.scale;

        // Note: We're ignoring the swapYZ flag for now.

        common.init(id, meta.root, t);
    }

}