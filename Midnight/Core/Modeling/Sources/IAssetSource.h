#pragma once
#include "avpch.h"
#include "Core/aveng_model.h"
#include "Core/Asset/AssetRegistry.h"

/*
* Future TODO : Open a stream (better for big assets)
    Not necessary now, but good to know: you can return an std::unique_ptr<std::istream> or a custom reader.
    For now, bytes is fine.

    Variants:
    FilesystemAssetSource (Editor)
    PackModelSource / VirtualFSModelSource (Shipping build)
    MemoryModelSource (unit tests)
    NetworkModelSource (streaming / mod support)

*/

namespace aveng {

    struct IAssetSource {
        virtual ~IAssetSource() = default;
        virtual std::vector<std::byte> readModelBytes(const AssetKey& key) = 0;
    };

}