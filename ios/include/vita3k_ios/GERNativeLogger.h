#pragma once

#include <string>

namespace ger::ios {

class NativeLogger {
public:
    static std::string log_directory();
    static std::string log_file();
    static bool ensure_storage();
    static void write(const std::string &message);
};

} // namespace ger::ios
