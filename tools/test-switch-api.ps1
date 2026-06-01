# Testa a API /switch do servidor Meruem (rode DEPOIS do deploy no Pi).
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\test-switch-api.ps1
#   (ou clique direito > Executar com o PowerShell)
# Opcional: -User / -Pass / -BaseUrl

param(
    [string]$BaseUrl,
    [string]$User,
    [string]$Pass
)

$ErrorActionPreference = "Stop"
if (-not $BaseUrl) { $BaseUrl = Read-Host "URL do servidor Meruem" }
if (-not $User) { $User = Read-Host "Usuario Meruem" }
if (-not $Pass) { $Pass = Read-Host "Senha Meruem" }

Write-Host "`n[1/4] Login em $BaseUrl/auth/login ..." -ForegroundColor Cyan
$body  = @{ username = $User; password = $Pass } | ConvertTo-Json
$login = Invoke-RestMethod -Uri "$BaseUrl/auth/login" -Method Post -ContentType "application/json" -Body $body
$token = $login.token
if (-not $token) { Write-Host "ERRO: login nao retornou token." -ForegroundColor Red; exit 1 }
Write-Host "  OK - token recebido (tamanho $($token.Length)), usuario=$($login.user.username) tier=$($login.user.tier)" -ForegroundColor Green
$h = @{ Authorization = "Bearer $token" }

Write-Host "`n[2/4] GET /switch/ping ..." -ForegroundColor Cyan
$ping = Invoke-RestMethod -Uri "$BaseUrl/switch/ping" -Headers $h
Write-Host "  OK - api=$($ping.api) serverTime=$($ping.serverTime)" -ForegroundColor Green

Write-Host "`n[3/4] GET /switch/catalog ..." -ForegroundColor Cyan
$cat = Invoke-RestMethod -Uri "$BaseUrl/switch/catalog" -Headers $h
Write-Host "  OK - $($cat.count) series no catalogo" -ForegroundColor Green
$cat.series | Select-Object -First 8 | Format-Table id, title, area, booksCount -AutoSize

if ($cat.series.Count -gt 0) {
    $sid = $cat.series[0].id
    Write-Host "`n[4/4] GET /switch/series/$sid ..." -ForegroundColor Cyan
    $ser = Invoke-RestMethod -Uri "$BaseUrl/switch/series/$sid" -Headers $h
    Write-Host "  OK - serie '$($ser.series.title)' ($($ser.series.area)): $($ser.count) capitulos" -ForegroundColor Green
    $ser.chapters | Select-Object -First 8 | Format-Table number, title, pages, sizeBytes -AutoSize
    Write-Host "Exemplo de URL de pagina 1 do 1o capitulo:" -ForegroundColor DarkGray
    Write-Host "  $BaseUrl$($ser.chapters[0].pageBase)1" -ForegroundColor DarkGray
} else {
    Write-Host "`n[4/4] Catalogo vazio - pulei o teste de serie." -ForegroundColor Yellow
}

Write-Host "`n=== API /switch respondeu certo! ===" -ForegroundColor Green
