# Envia o Meruem.nro para o Switch pela rede WiFi (nxlink) — sem precisar mexer no SD.
#
# Pre-requisitos:
#   1) Switch e PC na MESMA rede WiFi.
#   2) No Switch, abra o Homebrew Menu (ele fica "escutando" automaticamente).
#
# Uso:
#   clique direito > "Executar com o PowerShell"
#   ou, se a descoberta automatica falhar, passe o IP do Switch:
#       powershell -ExecutionPolicy Bypass -File send.ps1 192.168.0.42
#   (o IP aparece no Switch em: Configuracoes > Internet > Status da Conexao)

param([string]$ip)

$nxlink = "C:\devkitPro\tools\bin\nxlink.exe"
$nro    = "C:\MeruemSwitch\Meruem.nro"

if (-not (Test-Path $nro)) { Write-Host "Meruem.nro nao encontrado. Compile primeiro (build.ps1)." -ForegroundColor Red; exit 1 }

Write-Host "Enviando Meruem.nro para o Switch... (abra o Homebrew Menu no console)" -ForegroundColor Cyan
if ($ip) {
    & $nxlink -a $ip $nro
} else {
    & $nxlink $nro      # broadcast: descobre o Switch sozinho
}

if ($LASTEXITCODE -eq 0) { Write-Host "`n[OK] Enviado! O Meruem deve estar rodando no Switch." -ForegroundColor Green }
else { Write-Host "`n[ERRO] nxlink falhou (exit $LASTEXITCODE). Tente passar o IP do Switch: send.ps1 192.168.x.x" -ForegroundColor Red }