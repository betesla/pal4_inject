#pragma once

#include <cstddef>
#include <string>

namespace pal4::inject {

bool CopyRelocatingX86Bytes(
    const void* source,
    void* destination,
    std::size_t size,
    std::string* error);

}  // namespace pal4::inject
