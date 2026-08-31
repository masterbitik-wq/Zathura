@echo off
gcc -std=c11 -Wall -Wextra -O0 -g -o page_cache_scroll.exe page_cache_scroll.c
if errorlevel 1 exit /b 1
echo ===== BUG =====
page_cache_scroll.exe
echo ===== FIX =====
page_cache_scroll.exe --fix
