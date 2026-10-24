#pragma once

#include <string_view>
namespace smelt {
[[nodiscard]] auto globMatch(std::string_view pattern, std::string_view path)
    -> bool;
}
