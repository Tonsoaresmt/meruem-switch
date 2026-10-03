# Auditoria do instalador Nplay — Meruem 0.19.3

Data: 02/10/2026. Base auditada: release 0.19.2, commit 586b51f.

Correcoes e NRO 0.19.3 validados; publicacao explicitamente autorizada pelo
usuario em 02/10/2026. Release:
https://github.com/Tonsoaresmt/meruem-switch/releases/tag/v0.19.3
NRO preparado: 45.008.443 bytes; SHA-256
6cc9d291f3755b67004abf10653ddf54b5ef9c1bd79a82fbcc6aac9c83753e21.

## Problemas encontrados e corrigidos

| Situacao | Comportamento anterior | Correcao |
|---|---|---|
| B no aviso de erro | Modal generico ignorava B | Aviso proprio com Voltar por B/+ ou toque |
| Toque fora dos botoes na confirmacao | Modal aceitava qualquer toque | Somente Baixar agora ou A autoriza instalar |
| Verificacao SHA-256 | Sem eventos durante a leitura do SD | Progresso por fase e cancelamento durante verificacao |
| Falha de instalacao e de rollback | Mensagem podia sugerir que o arquivo antigo estava ativo | Informa o caminho do backup; nova tentativa recupera antes de baixar |
| JSON com lixo final/estrutura de assets incorreta | Parser permissivo | JSON completo e assets como array |
| Destino ou backup como diretorio | Podia tentar renomear o diretorio | Recusa antes de alterar o destino |
| Falha TLS/404/limite GitHub | Mensagem generica | Mensagem para corrigir data/hora ou tentar mais tarde |

## Validacoes executadas

- 34 simulacoes compilando as funcoes reais de main.c, extraidas automaticamente:
  tela inicial, menu Nplay, confirmacao, progresso e authenticate.
- Controle e toque, retrato e TV; limites geometricos dos botoes e desenhos.
- Caminho sem login, com zero chamadas de autenticacao e zero alteracoes de conta.
- Confirmar, voltar, cancelar consulta/download, falhar, retornar, tentar novamente
  e sucesso; limite de iteracoes detecta loops que deixariam o usuario preso.
- Release com arquivo errado, host/protocolo errado, digest ausente/invalido,
  tamanho fracionario/fora do limite, draft/prerelease, JSON invalido e NRO truncado.
- DNS, conexao, timeout, TLS, excesso de redirects, HTTP 403/404/429/503,
  resposta HTML e resposta acima do limite de memoria.
- Download parcial/acima do tamanho, checksum e NRO invalidos; cancelamento
  na consulta, download, verificacao e antes da ativacao.
- Falhas injetadas de escrita/fechamento (SD cheio), ativacao, limpeza de backup
  e rollback; recuperacao de backup e reinstalacao.
- Consulta e download reais, anonimos, via HTTPS com o bundle CA embarcado.
  Parser do instalador aceitou a release real v0.12.38 (24.201.039 bytes).
  NRO real e SHA-256 conferidos contra a API.
- Compilacao ARM64 completa com -Wall e -Werror.

## Rotas e dependencia de login

O instalador usa https://api.github.com/repos/Tonsoaresmt/nplay-switch/releases/latest
e o browser_download_url do asset Nplay.nro na release correspondente. Nao depende
de rotas /auth ou /switch do servidor Meruem, nem envia seus tokens ao GitHub.
Permite somente HTTPS, ate 5 redirects, conexao de ate 12 s, consulta de ate 30 s
e download de ate 300 s. Em erro retorna ao menu, permitindo voltar e repetir.

## Como repetir

1. tools/test_other_apps.ps1
2. node tools/test_apps_navigation.mjs
3. tools/test_live_nplay.ps1 (requer internet, depois dos testes host)
4. build.ps1 EXTRA_CFLAGS=-Werror

## Limites da verificacao

Navegacao usa eventos SDL/libnx simulados, e falhas de SD usam injecao de erros.
O teste host de instalacao usa transporte e SHA simulados para exercitar os caminhos;
o checksum do arquivo real foi validado separadamente. A build do Switch usa
libcurl/mbedTLS reais. O teste HTTPS externo usa curl do desktop, nao o console.
Nao equivale a um teste fisico de TLS, controles, renderizacao, hbmenu ou cartao SD.

Confirmacao pendente no Switch: abrir sem conta, pressionar -, cancelar e repetir,
instalar e abrir Nplay no hbmenu; repetir no dock; desconectar a rede durante o
download e confirmar retorno ao menu. Nenhum teste alterou dados de producao.
