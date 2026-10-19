param([string]$IdfRoot = 'C:\Users\liuyujian\esp\v6.1\esp-idf')
$ErrorActionPreference = 'Stop'
$expected = 'fff9895c82d744c7237be8847347bdd1b07c6643'
if (!(Test-Path -LiteralPath "$IdfRoot/export.ps1")) { throw "IDF missing: $IdfRoot" }
$actual = git -C $IdfRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $expected) { throw "Unexpected IDF revision: $actual" }
$env:PATH = 'C:\Users\liuyujian\.espressif\python_env\idf6.1_py3.14_env\Scripts;' + $env:PATH
. "$IdfRoot/export.ps1"
$env:CMAKE_NINJA_FORCE_RESPONSE_FILE = '1'
$env:PYTHONUTF8 = '1'
