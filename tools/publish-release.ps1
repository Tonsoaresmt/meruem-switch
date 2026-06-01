param(
    [Parameter(Mandatory = $true)]
    [string]$RepoOwner,

    [string]$RepoName = "meruem-switch",
    [string]$Tag = "v0.10.0",
    [string]$Title = "v0.10.0",
    [string]$Notes = "Release publica do app Meruem para Nintendo Switch."
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$gh = "gh"
$ghInstalled = Get-Command $gh -ErrorAction SilentlyContinue
if (-not $ghInstalled) {
    $gh = "C:\Program Files\GitHub CLI\gh.exe"
}

if (-not (Test-Path $gh)) {
    throw "Nao encontrei o GitHub CLI (gh)."
}

Push-Location $root
try {
    & $gh auth status | Out-Null

    $repoSlug = "$RepoOwner/$RepoName"
    $hasOrigin = (& git remote) -contains "origin"
    $repoExists = $true
    & $gh repo view $repoSlug | Out-Null
    if ($LASTEXITCODE -ne 0) { $repoExists = $false }

    if (-not $repoExists) {
        & $gh repo create $repoSlug --public --source . --remote origin --push
    } elseif (-not $hasOrigin) {
        & git remote add origin "https://github.com/$repoSlug.git"
    }

    & git push -u origin main

    & powershell -ExecutionPolicy Bypass -File .\build.ps1
    if ($LASTEXITCODE -ne 0) {
        throw "A build falhou; release cancelada."
    }

    & $gh release create $Tag .\Meruem.nro --repo $repoSlug --title $Title --notes $Notes
}
finally {
    Pop-Location
}
