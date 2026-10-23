#pragma once

#include "diagnostics.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace smelt {

struct Value {
  enum class Kind : std::uint8_t { String, Integer, Bool, Word, List };

  Kind kind = Kind::String;
  SourceLoc loc;
  std::string text;
  std::int64_t integer = 0;
  bool boolean = false;
  std::vector<Value> items;
};

struct Assign {
  SourceLoc loc;
  std::string key;
  Value value;
};

struct OnBlock {
  SourceLoc loc;
  std::string platform;
  std::vector<Assign> assigns;
};

struct ProfileDecl {
  SourceLoc loc;
  std::string name;
  std::vector<Assign> assigns;
};

struct TargetDecl {
  SourceLoc loc;
  std::string name;
  std::vector<Assign> assigns;
  std::vector<OnBlock> onBlocks;
};

struct ProjectDecl {
  SourceLoc loc;
  std::string name;
  std::vector<Assign> assigns;
};

struct ProjectFile {
  std::vector<ProjectDecl> projects;
  std::vector<ProfileDecl> profiles;
  std::vector<TargetDecl> targets;
};
} // namespace smelt
