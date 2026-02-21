#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <format>
#include "Core/Math/Vector.h"
namespace aveng {

	namespace fs = std::filesystem;

	class Debug {
	public:
		
        static void writeBlueNoiseDataToFile(const fs::path& path,
            const std::pmr::vector<Vec2>& data)
        {
            // Ensure parent directory exists
            if (!path.parent_path().empty())
            {
                fs::create_directories(path.parent_path());
            }

            std::ofstream file(path);

            if (!file)
            {
                throw std::runtime_error(
                    std::format("Failed to open debug file: {}", path.string()));
            }

            file << "--- Blue Noise Points ---\n";
            file << "Count: " << data.size() << "\n\n";

            // Column header
            file << "Index,X,Z\n";

            for (size_t i = 0; i < data.size(); ++i)
            {
                file << i << ","
                    << data[i].x << ","
                    << data[i].y << "\n";
            }
        }

	private:
	};
}