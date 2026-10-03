$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    & 'C:\devkitPro\msys2\usr\bin\bash.exe' -c 'export PATH=/c/devkitPro/msys2/usr/bin:$PATH; mkdir -p build; gcc -std=c11 -Wall -Wextra -Werror -Iinclude source/cJSON.c source/other_apps_release.c tools/test_other_apps.c -lm -o build/test_other_apps.exe && ./build/test_other_apps.exe && gcc -std=c11 -Wall -Wextra -Werror -Itools/host-stubs -Iinclude -I/c/devkitPro/portlibs/switch/include -DNPLAY_SD_ROOT=\"build/test-sd\" -Drename=mock_rename -Dremove=mock_remove -Dfwrite=mock_fwrite -Dfclose=mock_fclose -c source/other_apps.c -o build/other_apps_faults.o && gcc -std=c11 -Wall -Wextra -Werror -Iinclude -I/c/devkitPro/portlibs/switch/include -DNPLAY_SD_ROOT=\"build/test-sd\" source/cJSON.c source/other_apps_release.c tools/test_other_apps_install.c build/other_apps_faults.o -lm -o build/test_other_apps_install.exe && ./build/test_other_apps_install.exe'
    if ($LASTEXITCODE) { throw 'Falha nos testes do instalador.' }
} finally { Pop-Location }
