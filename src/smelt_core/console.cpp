#include "console.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>
namespace smelt {
Console::Console() : tty_(::isatty(STDOUT_FILENO) != 0) {}

void Console::setTotal(const std::size_t total) {
  std::lock_guard lock(mu_);
  total_ = total;
}

void Console::clearLocked() {
  if (tty_ && drawn_) {
    std::cout << "\r\x1b[K" << std::flush;
    drawn_ = false;
  }
}

void Console::drawLocked() {
  if (!tty_)
    return;
  std::cout << "\r\x1b[K[" << done_ << "/" << total_ << "]";
  if (!running_.empty()) {
    std::cout << " " << running_.back();
    if (running_.size() > 1)
      std::cout << " (+" << (running_.size() - 1) << " more)";
  }
  std::cout << std::flush;
  drawn_ = true;
}

void Console::eraseRunningLocked(const std::string &label) {
  const auto it = std::find(running_.begin(), running_.end(), label);
  if (it != running_.end())
    running_.erase(it);
}

void Console::startJob(const std::string &label) {
  std::lock_guard lock(mu_);
  clearLocked();
  running_.push_back(label);
  drawLocked();
}

void Console::out(const std::string &line) {
  std::lock_guard lock(mu_);
  clearLocked();
  std::cout << line << "\n";
  std::cout.flush();
}

void Console::err(const std::string &line) {
  std::lock_guard lock(mu_);
  clearLocked();
  std::cerr << line << "\n";
  std::cerr.flush();
}

void Console::finishJob(const std::string &label, const bool printLine) {
  std::lock_guard lock(mu_);
  clearLocked();
  eraseRunningLocked(label);
  if (printLine) {
    std::cout << "  " << label << "\n" << std::flush;
  }
  ++done_;
  drawLocked();
}

void Console::finishTarget(const std::string &name, double seconds,
                           bool built) {
  std::lock_guard lock(mu_);
  clearLocked();
  if (built) {
    std::ostringstream t;
    t << std::fixed << std::setprecision(1) << seconds;
    std::cout << "  done " << name << " in " << t.str() << "s\n";
  } else {
    std::cout << "  up to date " << name << "\n";
  }
  std::cout << std::flush;
  drawLocked();
}

void Console::failJob(const std::string &label) {
  std::lock_guard lock(mu_);
  clearLocked();
  eraseRunningLocked(label);
  drawLocked();
}

void Console::finish() {
  std::lock_guard lock(mu_);
  clearLocked();
  running_.clear();
  done_ = 0;
  total_ = 0;
}

} // namespace smelt
