#pragma once
#define _CRT_SECURE_NO_WARNINGS

#ifdef AIM_PLATFORM_WINDOWS
#define NOMINMAX
#include <Windows.h>
#endif

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Core/Version.hpp>
#include <Core/Assert.hpp>
#include <Core/Base.hpp>
#include <Core/Log.hpp>
#include <Core/Exceptions.hpp>