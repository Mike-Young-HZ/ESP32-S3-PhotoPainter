$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/activate-idf61.ps1"
$root = Split-Path $PSScriptRoot
python "$PSScriptRoot/compress-prompts.py"
if ($LASTEXITCODE -ne 0) { throw 'Prompt compression failed' }
# A separate CMake project keeps the legacy managed_components paths intact.
python "$env:IDF_PATH/tools/idf.py" -C "$root/migration/idf61-project" -B "$root/build-idf61" build
exit $LASTEXITCODE
