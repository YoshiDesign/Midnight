#include "AssetResolution.h"

namespace aveng {

    static bool isAbsolutePath(const std::string& p) {
#ifdef _WIN32
        if (p.size() > 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':') return true;
        if (p.rfind("\\\\", 0) == 0) return true; // UNC
#endif
        return p.rfind("/", 0) == 0;
    }

    std::string joinPath(const std::string& base, const std::string& rel) {
        if (base.empty()) return rel;
        if (rel.empty()) return base;
        char last = base.back();
        if (last == '/' || last == '\\') return base + rel;
#ifdef _WIN32
        return base + "\\" + rel;
#else
        return base + "/" + rel;
#endif
    }

    static std::string fileNameOnly(const std::string& p) {
        auto pos = p.find_last_of("/\\");
        return (pos == std::string::npos) ? p : p.substr(pos + 1);
    }

    // if you have std::filesystem available, use exists(); otherwise use your own
    static bool fileExists(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        return f.good();
    }

    /*
    * Stick to the policy:
        modelBaseDir and contentRoot represent two different namespaces, 
        and combining them would be guessing rather than resolving
    */
    std::string resolveModelTexturePath(
        const std::string& modelBaseDir, // Where the model expects the external dependency to be (e.g. .gltf internal reference)
        const std::string& contentRoot,  // The engine's globally managed content
        const std::string& ref
    ) {
        if (ref.empty()) return {};
        if (isAbsolutePath(ref)) return ref;

        // 1) Relative to model
        {
            auto p = joinPath(modelBaseDir, ref);
            if (fileExists(p)) return p;
        }

        // 2) Relative to content root
        {
            auto p = joinPath(contentRoot, ref);
            if (fileExists(p)) return p;
        }

        // 3) In global textures dir by filename
        {
            auto p = joinPath(joinPath(contentRoot, "textures"), fileNameOnly(ref));
            if (fileExists(p)) return p;
        }

        // Give up: return the "most likely" so loadTexture prints a path
        return joinPath(modelBaseDir, ref);
    }

}