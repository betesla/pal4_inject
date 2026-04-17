if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

if(NOT DEFINED VERSION)
    set(VERSION "v0.0.0-dev")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

string(TIMESTAMP build_id "%Y%m%d-%H%M%SZ" UTC)

file(WRITE "${OUTPUT}" "#pragma once\n\nnamespace pal4::inject {\ninline constexpr const char* kPal4InjectVersion = \"${VERSION}\";\ninline constexpr const char* kPal4InjectBuildId = \"${build_id}\";\n}  // namespace pal4::inject\n")
