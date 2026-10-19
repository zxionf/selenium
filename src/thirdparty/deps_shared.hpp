#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(SELE_DEP_BUILD)
#define SELE_DEP_API __declspec(dllexport)
#else
#define SELE_DEP_API __declspec(dllimport)
#endif
#else
#define SELE_DEP_API __attribute__((visibility("default")))
#endif

extern "C" {

SELE_DEP_API const char* sele_cli11_version();
SELE_DEP_API const char* sele_nlohmann_json_version();

} // extern "C"
