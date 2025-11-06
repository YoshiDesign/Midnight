#include "Tools.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cerrno>  // errno
#include <cstring> // strerror()
namespace aveng {

    std::string Tools::getFilenameExt(std::string filename) {
        size_t pos = filename.find_last_of('.');
        if (pos != std::string::npos) {
            return filename.substr(pos + 1);
        }
        return std::string();
    }

    std::string Tools::loadFileToString(std::string fileName) {
        std::ifstream inFile(fileName, std::ios::binary);
        std::string str;

        if (inFile.is_open()) {
            str.clear();
            // allocate string data (no slower realloc)
            inFile.seekg(0, std::ios::end);
            str.reserve(inFile.tellg());
            inFile.seekg(0, std::ios::beg);

            str.assign((std::istreambuf_iterator<char>(inFile)),
                std::istreambuf_iterator<char>());
            inFile.close();
        }
        else {
            std::printf("%s error: could not open file %s\n", __FUNCTION__, fileName.c_str());
            std::printf("%s error: system says '%s'\n", __FUNCTION__, strerror(errno));
            return std::string();
        }

        if (inFile.bad() || inFile.fail()) {
            std::printf("%s error: error while reading file %s\n", __FUNCTION__, fileName.c_str());
            inFile.close();
            return std::string();
        }

        inFile.close();
        std::printf("%s: file %s successfully read to string\n", __FUNCTION__, fileName.c_str());
        return str;
    }
}