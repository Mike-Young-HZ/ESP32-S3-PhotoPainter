param(
    [string]$Source = (Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'xiaozhi-esp32'),
    [string]$Destination = (Split-Path $PSScriptRoot),
    [string]$Manifest = (Join-Path (Split-Path $PSScriptRoot) 'migration/source-manifest.json')
)
$ErrorActionPreference = 'Stop'
$items = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$failures = [Collections.Generic.List[string]]::new()
foreach ($item in $items) {
    $src = Join-Path $Source $item.path
    $dst = Join-Path $Destination $item.path
    if (!(Test-Path -LiteralPath $dst -PathType Leaf)) { $failures.Add("Missing destination path: $($item.path)") }
    if (!$item.sha256) { $failures.Add("Missing baseline hash: $($item.path)"); continue }
    try {
        $stream = [IO.File]::Open($src, 'Open', 'Read', 'ReadWrite')
        try {
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $sha.Dispose() }
        } finally { $stream.Dispose() }
        if ($hash -ne $item.sha256) { $failures.Add("Source changed: $($item.path)") }
    } catch { $failures.Add("Source unreadable: $($item.path)") }
}
if ($failures.Count) { throw ($failures -join "`n") }
Write-Output "PASS: $($items.Count) original file paths retained; source hashes unchanged."
