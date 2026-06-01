# Meruem — Leitor de CBZ para Nintendo Switch

Homebrew (`.nro`) que lê quadrinhos/mangás do servidor Meruem.

> ⚠️ **Local do projeto:** `C:\MeruemSwitch` (sem espaço no caminho). O `make` do devkitPro
> quebra com espaços, por isso NÃO usamos `C:\...\Meruem Switch`.

## Servidor
O app usa por padrão:
```text
https://meruem.tonserverlocal.uk
```

Para trocar o servidor sem recompilar, edite no SD:
```text
sdmc:/switch/Meruem/server.txt
```

## Como compilar
**Opção 1 (mais fácil):** clique com o botão direito em `build.ps1` → *Executar com o PowerShell*.

**Opção 2:** abra o shell **"devkitPro MSYS2"** (Menu Iniciar) e rode:
```bash
cd /c/MeruemSwitch
make -j4
```
Gera `Meruem.nro`. Para limpar: `make clean` (ou `build.ps1 clean`).

## Instalar no Switch
Copie `Meruem.nro` para o SD em `sdmc:/switch/` (ou `sdmc:/switch/Meruem/`).
No console (Atmosphère/CFW), abra o **Homebrew Menu** e selecione *Meruem*.

## Roadmap (fases)
1. ✅ **Toolchain** — devkitPro (devkitA64 + libnx + SDL2/curl/zlib/png/jpeg) instalado.
2. ✅ **Hello World** — `.nro` com console de texto. Build validado.
3. ⏳ **API /switch** — rota dedicada com auth por token.
4. ⏳ **Rede** — baixar catálogo JSON via libcurl, listar séries.
5. ⏳ **Leitura** — exibir páginas com SDL2 + decodificação de imagem.
6. ⏳ **CBZ offline** — baixar `.cbz`, abrir com miniz, cache + progresso.
7. ⏳ **UI + auto-update** — polir interface e atualização via GitHub Releases.

## Auto-update
O app agora pode consultar a release mais recente do GitHub e baixar o novo `.nro`
direto para o mesmo caminho do executável atual.

Antes de compilar a versão final da release, configure no `Makefile`:
```make
UPDATE_REPO_OWNER ?= seu-usuario
UPDATE_REPO_NAME  ?= meruem-switch
```

Quando encontrar uma tag mais nova que `APP_VERSION`, o Meruem mostra um prompt no boot
para baixar e trocar o `.nro` automaticamente.

Para publicar a primeira release depois do `gh auth login`, use:
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish-release.ps1 -RepoOwner seu-usuario
```

## Estrutura
```
C:\MeruemSwitch\
  Makefile          # baseado no template application do devkitPro
  build.ps1         # atalho de compilação (seta env + make)
  icon.jpg          # ícone do app (NACP) — placeholder, trocar depois
  source/
    main.c          # código principal
  include/          # (futuro) stb_image.h, miniz.h, cabeçalhos extras
  Meruem.nro        # binário gerado (não versionado)
```
