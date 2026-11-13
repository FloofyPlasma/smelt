@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "ROOT=%%~fI"

if not defined CXX set "CXX=clang++"

set "SRC=%ROOT%\src\smelt_core"
set "OUT=%ROOT%\build\stage0"

if not exist "%OUT%" mkdir "%OUT%"

"%CXX%" ^
  -std=c++23 ^
  -O2 ^
  -Wall -Wextra ^
  "-ffile-prefix-map=%ROOT%=." ^
  -I "%SRC%" ^
  "%SRC%\token.cpp" ^
  "%SRC%\lexer.cpp" ^
  "%SRC%\parser.cpp" ^
  "%SRC%\sema.cpp" ^
  "%SRC%\glob.cpp" ^
  "%SRC%\process.cpp" ^
  "%SRC%\discover.cpp" ^
  "%SRC%\workspace.cpp" ^
  "%SRC%\build.cpp" ^
  "%SRC%\lock.cpp" ^
  "%SRC%\git.cpp" ^
  "%SRC%\fetch.cpp" ^
  "%SRC%\registry.cpp" ^
  "%SRC%\compdb.cpp" ^
  "%SRC%\install.cpp" ^
  "%SRC%\console.cpp" ^
  "%SRC%\tools.cpp" ^
  "%SRC%\tree.cpp" ^
  "%SRC%\init.cpp" ^
  "%SRC%\hash.cpp" ^
  "%SRC%\std_module.cpp" ^
  "%SRC%\module_scan.cpp" ^
  "%ROOT%\src\smelt\main.cpp" ^
  -o "%OUT%\smelt.exe"

echo built "%OUT%\smelt.exe"
