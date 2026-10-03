$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    & curl.exe --fail --silent --show-error --location --proto '=https' --proto-redir '=https' --connect-timeout 12 --max-time 30 --cacert data/cacert.bin --user-agent 'Meruem-Switch/route-audit' --output build/live-nplay-release.json 'https://api.github.com/repos/Tonsoaresmt/nplay-switch/releases/latest'
    if ($LASTEXITCODE) { throw 'Falha na consulta anonima do GitHub.' }
    & 'C:\devkitPro\msys2\usr\bin\bash.exe' -c './build/test_other_apps.exe build/live-nplay-release.json'
    if ($LASTEXITCODE) { throw 'O parser do instalador recusou a release real.' }
    $release = Get-Content build/live-nplay-release.json -Raw | ConvertFrom-Json
    $asset = $release.assets | Where-Object name -eq 'Nplay.nro'
    if (-not $asset) { throw 'Release sem Nplay.nro.' }
    & curl.exe --fail --silent --show-error --location --proto '=https' --proto-redir '=https' --max-redirs 5 --connect-timeout 12 --max-time 300 --cacert data/cacert.bin --output build/live-Nplay.nro $asset.browser_download_url
    if ($LASTEXITCODE) { throw 'Falha no download anonimo real.' }
    $hash = (Get-FileHash build/live-Nplay.nro -Algorithm SHA256).Hash.ToLowerInvariant()
    if ((Get-Item build/live-Nplay.nro).Length -ne $asset.size -or ('sha256:' + $hash) -ne $asset.digest) { throw 'Tamanho/hash remoto divergente.' }
    & 'C:\devkitPro\msys2\usr\bin\bash.exe' -c "./build/test_other_apps.exe build/live-Nplay.nro $($asset.size)"
    if ($LASTEXITCODE) { throw 'NRO remoto invalido.' }
    Write-Output 'PASS: anonymous HTTPS API and asset download, redirects, bundled CA, actual parser, NRO size and SHA-256'
} finally { Pop-Location }
