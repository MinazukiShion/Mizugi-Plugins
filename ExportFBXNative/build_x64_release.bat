@echo off
setlocal

echo ==========================================
echo ExportFBXNative v2.2.1 - MSVC Build
echo ==========================================

REM Visual Studio 18 Community
set "VSROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
set "VCVARS=%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"

REM Metasequoia SDK: set MQSDK env var or edit the default below
if "%MQSDK%"=="" set "MQSDK=D:\Mizuho\mqsdk\mqsdk491a\mqsdk"
set "SDK=%MQSDK%"

REM Source and output
set "SRC=%~dp0ExportFBXNative.cpp"
set "OUT=%~dp0x64\Release"

if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found.
    echo Check Visual Studio installation path.
    pause
    exit /b 1
)

if not exist "%SDK%\MQBasePlugin.h" (
    echo ERROR: Metasequoia SDK not found.
    echo SDK=%SDK%
    echo Set the MQSDK environment variable to your mqsdk directory.
    pause
    exit /b 1
)

call "%VCVARS%"
if errorlevel 1 (
    echo ERROR: MSVC environment initialization failed.
    pause
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

echo.
echo Compiling ExportFBXNative v2.2.1...

cl /nologo /std:c++17 /EHsc /O2 /W3 /utf-8 /D "_WINDOWS" /LD ^
  /I "%SDK%" ^
  "%SRC%" ^
  "%SDK%\MQPlugin.cpp" ^
  "%SDK%\MQInit.cpp" ^
  "%SDK%\MQBasePlugin.cpp" ^
  "%SDK%\MQWidget.cpp" ^
  "%SDK%\MQSetting.cpp" ^
  "%SDK%\MQ3DLib.cpp" ^
  /Fe"%OUT%\ExportFBXNative.dll" ^
  /link /NOLOGO shlwapi.lib

echo.
if "%ERRORLEVEL%"=="0" (
    echo BUILD SUCCESS: %OUT%\ExportFBXNative.dll
) else (
    echo BUILD FAILED: %ERRORLEVEL%
)
pause
exit /b %ERRORLEVEL%
