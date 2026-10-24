#include "glob.hpp"
#include <cstddef>
#include <string_view>
#include <vector>
namespace smelt {
namespace {
auto splitSegments(std::string_view s) -> std::vector<std::string_view> {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const std::size_t slash = s.find('/', start);
    const std::size_t stop = slash == std::string_view::npos ? s.size() : slash;
    if (stop > start)
      out.push_back(s.substr(start, stop - start));
    if (slash == std::string_view::npos)
      break;
    start = slash + 1;
  }
  return out;
}

auto matchChars(std::string_view p, std::string_view s) -> bool {
  if (p.empty())
    return s.empty();
  if (p[0] == '*') {
    for (std::size_t k = 0; k <= s.size(); ++k) {
      if (matchChars(p.substr(1), s.substr(k)))
        return true;
    }
    return false;
  }
  if (s.empty())
    return false;
  if (p[0] != '?' && p[0] != s[0])
    return false;
  return matchChars(p.substr(1), s.substr(1));
}

auto matchSegments(const std::vector<std::string_view> &pat, std::size_t pi,
                   const std::vector<std::string_view> &path, std::size_t si)
    -> bool {
  if (pi == pat.size())
    return si == path.size();

  if (pat[pi] == "**") {
    for (std::size_t k = si; k <= path.size(); ++k) {
      if (matchSegments(pat, pi + 1, path, k))
        return true;
    }
    return false;
  }

  if (si == path.size())
    return false;
  if (!matchChars(path[pi], path[si]))
    return false;
  return matchSegments(pat, pi + 1, path, si + 1);
}
} // namespace

auto globMatch(std::string_view pattern, std::string_view path) -> bool {
  return matchSegments(splitSegments(pattern), 0, splitSegments(path), 0);
}

} // namespace smelt
