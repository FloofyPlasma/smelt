#pragma once

#include "ast.hpp"
#include "diagnostics.hpp"
#include "token.hpp"
#include <vector>
namespace smelt {
[[nodiscard]] auto parseProject(const std::vector<Token> &tokens,
                                Diagnostics &diags) -> ProjectFile;
}
