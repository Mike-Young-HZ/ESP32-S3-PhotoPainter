$ErrorActionPreference = 'Stop'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('photopainter-layout-' + [guid]::NewGuid())
New-Item -ItemType Directory "$tmp/src", "$tmp/dst" | Out-Null
Set-Content "$tmp/src/a.txt" 'baseline'
Copy-Item "$tmp/src/a.txt" "$tmp/dst/a.txt"
@(@{path='a.txt';sha256=(Get-FileHash "$tmp/src/a.txt").Hash}) | ConvertTo-Json -AsArray | Set-Content "$tmp/manifest.json"
function Check([bool]$ExpectedFailure) {
    $failed = $false
    try { & "$PSScriptRoot/verify-layout.ps1" -Source "$tmp/src" -Destination "$tmp/dst" -Manifest "$tmp/manifest.json" | Out-Null }
    catch { $failed = $true }
    if ($failed -ne $ExpectedFailure) { throw 'Integrity checker returned the wrong result' }
}
Check $false
Set-Content "$tmp/dst/added.txt" 'allowed'
Check $false
Rename-Item "$tmp/dst/a.txt" 'renamed.txt'
Check $true
Rename-Item "$tmp/dst/renamed.txt" 'a.txt'
Set-Content "$tmp/src/a.txt" 'modified'
Check $true
Write-Output 'PASS: baseline, additions, renamed/missing path, changed source.'
