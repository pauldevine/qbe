@echo off
REM Build script for QBE on DOS with OpenWatcom
REM Usage: build_dos.bat [16|32]
REM   16 = 16-bit real mode (default)
REM   32 = 32-bit protected mode (recommended)

if "%1"=="32" goto build32
if "%1"=="16" goto build16
if "%1"=="" goto build16

echo Invalid argument. Use: build_dos.bat [16^|32]
goto end

:build16
echo Building QBE for 16-bit DOS (real mode)...
echo WARNING: This may fail due to memory constraints!
wmake -f watcom.mak
goto end

:build32
echo Building QBE for 32-bit DOS (protected mode with DOS4GW)...
echo This is the recommended build method.
wmake -f watcom32.mak
goto end

:end
