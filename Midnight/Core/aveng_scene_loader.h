#pragma once
#include <string>
#include <unordered_map>
#include "app_object.h"

namespace aveng {
    class AvengSceneLoader {
    public:
        AvengSceneLoader();
        ~AvengSceneLoader();


        /**
         * read() should read
         */
        void read(const char* filepath);
        void reset();


    private:
        void load();

        std::string scene_id;
        std::unordered_map<unsigned int, AvengAppObject>  scenes;
    };
}