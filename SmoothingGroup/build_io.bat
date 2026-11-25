@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set SDK=D:\Mizuho\mqsdk\mqsdk491a\mqsdk
cl /nologo /std:c++17 /EHsc /O2 /W3 /utf-8 /D "_WINDOWS" /LD ^
  /I "%SDK%" ^
  D:\Mizuho\sg_plugin\SmoothingGroupIO.cpp ^
  "%SDK%\MQPlugin.cpp" ^
  "%SDK%\MQInit.cpp" ^
  "%SDK%\MQBasePlugin.cpp" ^
  "%SDK%\MQWidget.cpp" ^
  "%SDK%\MQSetting.cpp" ^
  /FeD:\Mizuho\sg_plugin\SmoothingGroupIO.dll ^
  /link /NOLOGO
exit /b %ERRORLEVEL%
