#pragma once

#include <string>

namespace pal4::inject {

bool StartIpcServer(std::string* error);
void StopIpcServer();

}  // namespace pal4::inject
