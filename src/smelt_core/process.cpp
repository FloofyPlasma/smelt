#include "process.hpp"
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <mutex>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace smelt {

#ifdef _WIN32
namespace {
std::mutex &spawnMutex() {
  static std::mutex m;
  return m;
}

auto quoteArg(const std::string &a) -> std::string {
  if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos)
    return a;

  std::string out = "\"";
  std::size_t backslashes = 0;
  for (const char c : a) {
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    if (c == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out += '"';
    } else {
      out.append(backslashes, '\\');
      out += c;
    }
    backslashes = 0;
  }
  out.append(backslashes * 2, '\\');
  out += '"';
  return out;
}

auto buildCommandLine(const std::vector<std::string> &argv) -> std::string {
  std::string cmd;
  for (const auto &a : argv) {
    if (!cmd.empty())
      cmd += ' ';
    cmd += quoteArg(a);
  }
  return cmd;
}

void reportSpawnError(const std::string &program) {
  std::cerr << program << ": cannot start process (error " << GetLastError()
            << ")\n";
}
} // namespace

auto runProcess(const std::vector<std::string> &argv) -> int {
  std::cout.flush();
  std::cerr.flush();

  std::string cmd = buildCommandLine(argv);

  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};

  BOOL ok = FALSE;
  {
    std::lock_guard lock(spawnMutex());
    ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &pi);
  }
  if (!ok) {
    reportSpawnError(argv[0]);
    return -1;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code != 0)
    std::cerr << "smelt: " << argv[0] << " exited with 0x" << std::hex << code
              << std::dec << "\n";
  return static_cast<int>(code);
}

auto runProcessLogged(const std::vector<std::string> &argv,
                      const std::string &logFile) -> int {
  std::string cmd = buildCommandLine(argv);

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};
  BOOL ok = FALSE;

  {
    std::lock_guard lock(spawnMutex());

    HANDLE log =
        CreateFileA(logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
      return -1;

    si.hStdOutput = log;
    si.hStdError = log;

    ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        nullptr, &si, &pi);
    CloseHandle(log);
  }

  if (!ok) {
    reportSpawnError(argv[0]);
    return -1;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (code != 0)
    std::cerr << "smelt: " << argv[0] << " exited with 0x" << std::hex << code
              << std::dec << "\n";
  return static_cast<int>(code);
}

auto runCapture(const std::vector<std::string> &argv, const bool quietStderr)
    -> std::optional<std::string> {
  std::string cmd = buildCommandLine(argv);

  std::cout.flush();
  std::cerr.flush();

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  PROCESS_INFORMATION pi{};
  BOOL ok = FALSE;

  {
    std::lock_guard lock(spawnMutex());

    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
      return std::nullopt;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = INVALID_HANDLE_VALUE;
    HANDLE errHandle = GetStdHandle(STD_ERROR_HANDLE);
    if (quietStderr) {
      nul =
          CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (nul != INVALID_HANDLE_VALUE)
        errHandle = nul;
    }

    STARTUPINFOA si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writePipe;
    si.hStdError = errHandle;

    ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        nullptr, &si, &pi);

    CloseHandle(writePipe);
    if (nul != INVALID_HANDLE_VALUE)
      CloseHandle(nul);
  }

  if (!ok) {
    CloseHandle(readPipe);
    reportSpawnError(argv[0]);
    return std::nullopt;
  }

  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(readPipe, buf, sizeof buf, &n, nullptr) && n > 0)
    out.append(buf, n);
  CloseHandle(readPipe);

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (code != 0)
    return std::nullopt;
  return out;
}

#else
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

auto runCapture(const std::vector<std::string> &argv, const bool quietStderr)
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
    if (quietStderr) {
      const int null = ::open("/dev/null", O_WRONLY);
      if (null >= 0) {
        ::dup2(null, STDERR_FILENO);
        ::close(null);
      }
    }
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

#endif

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
