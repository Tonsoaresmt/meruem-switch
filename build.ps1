# Compila o Meruem.nro usando o toolchain devkitPro.
# Uso:  clique direito neste arquivo > "Executar com o PowerShell"
#   ou: powershell -ExecutionPolicy Bypass -File build.ps1
# Aceita argumentos extras do make, ex:  build.ps1 clean

$bashArgs = 'export DEVKITPRO=/c/devkitPro; ' +
            'export DEVKITA64=/c/devkitPro/devkitA64; ' +
            'export PATH=/c/devkitPro/msys2/usr/bin:/c/devkitPro/devkitA64/bin:/c/devkitPro/tools/bin:/c/devkitPro/portlibs/switch/bin:$PATH; ' +
            "make -j4 $($args -join ' ')"

Push-Location $PSScriptRoot
try {
    & "C:\devkitPro\msys2\usr\bin\bash.exe" -c $bashArgs
    $buildExit = $LASTEXITCODE
} finally { Pop-Location }

if ($buildExit -eq 0) {
    Write-Host "`n[OK] Meruem.nro gerado em $PSScriptRoot\Meruem.nro" -ForegroundColor Green
} else {
    Write-Host "`n[ERRO] Falha na compilacao (exit $buildExit)" -ForegroundColor Red
}
exit $buildExit
