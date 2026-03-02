#include "FilesystemAssetSource.h"
#include <fstream>
#include <iterator>
#include <cstdio>

namespace aveng {

    std::vector<std::byte> FilesystemAssetSource::readModelBytes(const AssetKey& key) {
        // key is assumed to be a normalized filesystem path at this stage
        std::ifstream file(key, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::printf("[FilesystemAssetSource] Failed to open file: %s\n", key.c_str());
            return {};
        }

        const std::streamsize size = file.tellg();
        if (size <= 0) {
            std::printf("[FilesystemAssetSource] File empty or unreadable: %s\n", key.c_str());
            return {};
        }

        std::vector<std::byte> buffer(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            std::printf("[FilesystemAssetSource] Failed to read file: %s\n", key.c_str());
            return {};
        }

        return buffer;
    }

}