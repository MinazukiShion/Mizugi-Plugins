@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >NUL 2>&1
if "%MQSDK%"=="" set "MQSDK=D:\Mizuho\mqsdk\mqsdk491a\mqsdk"
set SDK=%MQSDK%
cl /nologo /std:c++17 /EHsc /O2 /W3 /utf-8 /D "_WINDOWS" /LD ^
  /I "%SDK%" /I "%~dp0" ^
  "%~dp0ImportFBX.cpp" ^
  "%~dp0ufbx.c" ^
  "%SDK%\MQPlugin.cpp" ^
  "%SDK%\MQInit.cpp" ^
  "%SDK%\MQBasePlugin.cpp" ^
  "%SDK%\MQWidget.cpp" ^
  "%SDK%\MQSetting.cpp" ^
  /Fe"%~dp0ImportFBX.dll" ^
  /link /NOLOGO
echo EXITCODE=%ERRORLEVEL%
