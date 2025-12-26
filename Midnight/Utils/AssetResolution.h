#pragma once
#include "avpch.h"

namespace aveng {

    std::string resolveModelTexturePath(
        const std::string& modelBaseDir,
        const std::string& contentRoot,
        const std::string& ref
    );

    std::string joinPath(const std::string& base, const std::string& rel);

}