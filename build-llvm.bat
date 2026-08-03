@echo off
REM Run any command inside the VS18 x64 developer environment (for clang-cl + Windows SDK/MSVC libs)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Users\ben\projects\sgct"
%*
exit /b %ERRORLEVEL%
