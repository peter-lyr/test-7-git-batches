@echo off
setlocal enabledelayedexpansion
chcp 65001>nul
cd %~dp0

del /f /q group-file.exe
gcc -o group-file.exe group-file.c

set start=%time%

group-file.exe commit-info.txt

set end=%time%
call :TimeDiff start end diff
echo total time: %diff%
goto :EOF
:TimeDiff
setlocal
set /a "ss=(((%end:~0,2%*60)+1%end:~3,2%%%100)*60+1%end:~6,2%%%100)*100+1%end:~9,2%%%100"
set /a "ss-=(((%start:~0,2%*60)+1%start:~3,2%%%100)*60+1%start:~6,2%%%100)*100+1%start:~9,2%%%100"
if %ss% lss 0 set /a ss+=24*60*60*100
set /a "ms=ss%%100, ss/=100, s=ss%%60, ss/=60, m=ss%%60, h=ss/60"
set "diff="
if %h% gtr 0 set "diff=%h%h"
if %m% gtr 0 set "diff=%diff% %m%m"
if %s% gtr 0 set "diff=%diff% %s%s"
if %ms% gtr 0 set "diff=%diff% %ms%ms"
if "%diff%"=="" set "diff=0ms"
endlocal & set "%3=%diff%"
goto :EOF
