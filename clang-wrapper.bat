@echo off
rem Wrapper to filter out /permissive- from clang commands
rem Skip the first argument (the compiler path passed by CMake launcher)
shift

set "args="
:loop
if "%~1"=="" goto endloop
if "%~1"=="/permissive-" (
    shift
    goto loop
)
set "args=%args% %~1"
shift
goto loop

:endloop
"%ProgramFiles%\LLVM\bin\clang++.exe" %args%
