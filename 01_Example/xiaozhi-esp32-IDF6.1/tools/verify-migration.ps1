$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
& "$PSScriptRoot/build-idf61.ps1"
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed' }
& "$PSScriptRoot/test-layout.ps1"
& "$PSScriptRoot/verify-layout.ps1"
& "$PSScriptRoot/test-host.cmd"
if ($LASTEXITCODE -ne 0) { throw 'Host behavior tests failed' }
& 'C:/Users/liuyujian/.espressif/python_env/idf6.1_py3.14_env/Scripts/python.exe' -X utf8 "$PSScriptRoot/verify-contracts.py"
if ($LASTEXITCODE -ne 0) { throw 'Migration contracts failed' }
& 'C:/Users/liuyujian/.espressif/python_env/idf6.1_py3.14_env/Scripts/python.exe' -X utf8 "$PSScriptRoot/verify-artifacts.py"
if ($LASTEXITCODE -ne 0) { throw 'Linked artifact checks failed' }
Write-Output 'Firmware build and local migration checks passed; hardware checklist is still required.'
