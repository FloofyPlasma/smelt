#include "hash.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {

auto hashHex(const std::string &text) -> std::string {
  std::uint64_t h = 0xCBF29CE484222325; // FNV-1a
  for (const unsigned char c : text) {
    h ^= c;
    h *= 0x00000100000001B3;
  }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << h;
  return out.str();
}

auto signatureOf(const std::vector<std::string> &args) -> std::string {
  std::string joined;
  for (const auto &a : args) {
    joined += a;
    joined += '\x1f';
  }
  return hashHex(joined);
}

auto fileHash(const fs::path &p) -> std::string {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return hashHex(ss.str());
}

} // namespace smelt
