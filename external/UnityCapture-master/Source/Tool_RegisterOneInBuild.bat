@echo off

:: BatchGotAdmin
:-------------------------------------
REM  --> Check for permissions
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"

REM --> If error flag set, we do not have admin.
if '%errorlevel%' NEQ '0' (
    echo Requesting administrative privileges...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    set params = %*:"=""
    echo UAC.ShellExecute "cmd.exe", "/c %~s0 %params%", "", "runas", 1 >> "%temp%\getadmin.vbs"

    "%temp%\getadmin.vbs"
    del "%temp%\getadmin.vbs"
    exit /B

:gotAdmin
    pushd "%CD%"
    CD /D "%~dp0\Build"

    ECHO Select the configuration and platform variation you want to register
    CLS
    ECHO 1. Debug   - 64 bit
    ECHO 2. Release - 64 bit
    ECHO 3. Debug   - 32 bit
    ECHO 4. Release - 64 bit
    ECHO.

    CHOICE /C 1234 /M "Enter your choice:"

    :: Note - list ERRORLEVELS in decreasing order
    IF ERRORLEVEL 4 GOTO Release32bit
    IF ERRORLEVEL 3 GOTO Debug32bit
    IF ERRORLEVEL 2 GOTO Releae64bit
    IF ERRORLEVEL 1 GOTO Debug64bit

:Debug64bit
    regsvr32 "Debug-UnityCaptureFilter64bit\UnityCaptureFilter64bit.dll"
    GOTO End

:Releae64bit
    regsvr32 "Release-UnityCaptureFilter64bit\UnityCaptureFilter64bit.dll"
    GOTO End

:Debug32bit
    regsvr32 "Debug-UnityCaptureFilter32bit\UnityCaptureFilter32bit.dll"
    GOTO End

:Release32bit
    regsvr32 "Release-UnityCaptureFilter32bit\UnityCaptureFilter32bit.dll"
    GOTO End
:--------------------------------------
