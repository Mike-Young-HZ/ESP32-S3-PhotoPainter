@echo off
call "D:\VS\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
"C:\Users\liuyujian\.espressif\python_env\idf6.1_py3.14_env\Scripts\python.exe" -X utf8 "%~dp0..\tests\photopainter\run_host_tests.py"
exit /b %errorlevel%
