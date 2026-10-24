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
