#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
namespace smelt {
class Console {
public:
  Console();

  void setTotal(std::size_t total);

  void out(const std::string &line);
  void err(const std::string &line);

  void startJob(const std::string &label);

  void finishJob(const std::string &label, bool printLine);
  void finishTarget(const std::string &name, double seconds, bool built);
  void failJob(const std::string &label);

  void finish();

private:
  void clearLocked();
  void drawLocked();
  void eraseRunningLocked(const std::string &label);

  std::mutex mu_;
  bool tty_ = false;
  bool drawn_ = false;
  std::size_t total_ = 0;
  std::size_t done_ = 0;
  std::vector<std::string> running_;
};
} // namespace smelt
