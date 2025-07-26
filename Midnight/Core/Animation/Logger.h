#pragma once

#include <iostream>
#include <string>
#include <cstdarg>

namespace aveng {

/**
 * Simple logging utility for the animation system
 * Level 0: Errors only
 * Level 1: Info + Errors  
 * Level 2: Debug + Info + Errors
 */
class Logger {
public:
    static void log(int level, const char* format, ...) {
        if (level > s_logLevel) return;
        
        va_list args;
        va_start(args, format);
        
        const char* prefix = "";
        switch(level) {
            case 0: prefix = "[ERROR] "; break;
            case 1: prefix = "[INFO]  "; break;
            case 2: prefix = "[DEBUG] "; break;
        }
        
        std::printf("%s", prefix);
        std::vprintf(format, args);
        va_end(args);
    }
    
    static void setLogLevel(int level) { s_logLevel = level; }
    static int getLogLevel() { return s_logLevel; }

private:
    static int s_logLevel;
};

} // namespace aveng 