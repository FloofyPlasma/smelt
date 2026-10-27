#include "process.hpp"
#include <bits/types/locale_t.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
namespace smelt {
auto runProcess(const std::vector<std::string> &argv) -> int {
  std::vector<char *> args;
  args.reserve(argv.size() + 1);
  for (const auto &a : argv)
    args.push_back(const_cast<char *>(a.c_str()));
  args.push_back(nullptr);

  std::cout.flush();
  std::cerr.flush();

  const pid_t pid = ::fork();
  if (pid < 0) {
    std::perror("fork");
    return -1;
  }

  if (pid == 0) {
    ::execvp(args[0], args.data());
    std::perror(args[0]);
    ::_exit(127);
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0)
    return -1;
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return -1;
}

auto runProcessLogged(const std::vector<std::string> &argv,
                      const std::string &logFile) -> int {
  std::vector<char *> args;
  args.reserve(argv.size() + 1);
  for (const auto &a : argv)
    args.push_back(const_cast<char *>(a.c_str()));
  args.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0)
    return -1;

  if (pid == 0) {
    const int fd = ::open(logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
      ::_exit(127);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    ::close(fd);
    ::execvp(args[0], args.data());
    ::_exit(127);
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0)
    return -1;
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
  return -1;
}

auto runCapture(const std::vector<std::string> &argv)
    -> std::optional<std::string> {
  std::vector<char *> args;
  args.reserve(argv.size() + 1);
  for (const auto &a : argv)
    args.push_back(const_cast<char *>(a.c_str()));
  args.push_back(nullptr);

  int fds[2];
  if (::pipe(fds) != 0)
    return std::nullopt;

  std::cout.flush();
  std::cerr.flush();

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(fds[0]);
    ::close(fds[1]);
    return std::nullopt;
  }

  if (pid == 0) {
    ::dup2(fds[1], STDOUT_FILENO);
    ::close(fds[0]);
    ::close(fds[1]);
    ::execvp(args[0], args.data());
    ::_exit(127);
  }

  ::close(fds[1]);
  std::string out;
  char buf[4096];
  ssize_t n;
  while ((n = ::read(fds[0], buf, sizeof buf)) > 0)
    out.append(buf, static_cast<std::size_t>(n));
  ::close(fds[0]);

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0)
    return std::nullopt;
  return out;
}

auto joinCommand(const std::vector<std::string> &argv) -> std::string {
  std::string out;
  for (const auto &a : argv) {
    if (!out.empty())
      out += ' ';
    out += a;
  }
  return out;
}
} // namespace smelt
