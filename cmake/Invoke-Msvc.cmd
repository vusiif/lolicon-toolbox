@echo off
setlocal
set VSLANG=1033
chcp 65001 >nul
%*
exit /b %errorlevel%
