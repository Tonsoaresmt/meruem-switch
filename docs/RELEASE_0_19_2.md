# Meruem Switch 0.19.2

Adiciona Outros apps do desenvolvedor com Nplay, acessivel na tela inicial
sem login ou assinatura Meruem e em Conta e ajustes. Atalho de controle: `-`.

O instalador consulta a release estavel de Tonsoaresmt/nplay-switch, baixa
somente Nplay.nro, valida HTTPS/TLS, tamanho, formato e SHA-256 informado
pelo GitHub. Salva em switch/Nplay/Nplay.nro, com backup do NRO anterior.
Download com progresso, cancelamento por controle/toque e limite de 64 MiB.
Nao envia tokens Meruem ao GitHub nem altera dados de contas ou leitores.

Inclui as melhorias locais da 0.19.1 (modo noturno de PDF/EPUB).

Validacao: build ARM64; testes host de contratos de release e NRO;
testes de transporte/instalacao com falhas, cancelamento e rollback.
Teste fisico ainda necessario: Switch sem conta > - > baixar Nplay;
cancelar e tentar novamente; abrir Nplay no hbmenu; repetir em modo TV.

Fonte preparada em copia isolada no workspace Meruem, pasta
.codex-tmp/meruem-switch-nplay. O checkout C:/MeruemSwitch foi preservado.
