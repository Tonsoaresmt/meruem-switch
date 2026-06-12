# Changelog

Versoes publicadas do Meruem Switch. Todas as releases (com o `Meruem.nro`)
ficam em: <https://github.com/Tonsoaresmt/meruem-switch/releases>

## v0.18.12
- Agora e possivel favoritar ou remover uma obra dos favoritos direto pelo
  Nintendo Switch.
- A tela de capitulos ganhou o botao `Favoritar`/`Favorito` ao lado da ordem
  `Mais novos`/`Mais antigos`, sem depender de abrir o site.
- O app consulta o estado atual da obra ao abrir os capitulos, atualiza a aba
  Favoritos ao voltar e mostra aviso claro quando a conta ainda nao tem acesso
  ao recurso.

## v0.18.11
- Instalador de atualizacao mais rapido no SD: depois de baixar o `.nro`, o app
  tenta trocar o arquivo por `rename` em vez de copiar os ~45 MB uma segunda vez.
- Tela de atualizacao agora deixa claro que esta baixando e instalando, evitando
  a sensacao de travamento.
- A entrada em Mangas, HQ e Livros voltou a escolher uma pagina aleatoria do
  acervo e embaralhar a pagina, evitando abrir sempre nas mesmas obras.
- Tela inicial sem conta ganhou `Criar conta gratis / Premium`, com QR para o
  celular, valores dos planos e atalho para voltar ao login QR depois.
- Livros EPUB preservam melhor a posicao aproximada ao girar a tela ou trocar
  `Texto P/M/G/XG`.
- Capas/arte de PDF continuam preenchendo e centralizadas, mas deixam de se
  comportar como pagina de texto arrastavel.
- Se um livro baixado do Meruem ficar corrompido/partial no SD, o app remove
  esse cache e orienta abrir de novo para baixar limpo.

## v0.18.10
- Hotfix no leitor de livros: capas/arte em PDF voltam a ser tratadas como capa,
  ficando centralizadas e preenchendo melhor a tela.
- Paginas com bastante texto continuam preservadas como folha de livro, sem o
  recorte agressivo que tinha causado aspecto de imagem.
- Login QR agora mostra uma mensagem clara quando o servidor/proxy publico ainda
  nao recebeu as rotas de QR.

## v0.18.9
- Leitor de livros deixou de mostrar `Zoom P/M/G/XG`; agora o controle aparece
  como `Texto P/M/G/XG`, alinhado com leitura de livro.
- PDFs/EPUBs preservam a folha inteira, incluindo margens e capas, sem recorte
  automatico agressivo que fazia a pagina parecer uma imagem ampliada.
- Pinch e toque duplo nao aplicam mais zoom em livros. O usuario rola a folha e
  so avanca quando chega ao fim da pagina.

## v0.18.8
- Login por QR: o usuario pode escanear um QR no Switch, entrar pelo celular e o
  app salva automaticamente a conta/token no SD.
- A aba Conta ganhou o botao `QR login`, alem do login tradicional por
  usuario/senha.
- O QR usa sessao temporaria com `secret` separado: o QR mostra so URL/codigo, e
  o token real volta apenas pelo polling seguro do Switch.
- O app ja aplica restricoes explicitas de area quando o servidor enviar
  `allowedAreas`.

## v0.18.7
- Conta ganhou a tela `Areas visiveis`, aberta pelo botao `Areas` ou por `L/R`,
  para ocultar `Mangas`, `HQ`, `Livros`, `Baixados` e `Local`.
- O botao `Area`, a tela `Continuar lendo`, os atalhos `X/Y` e a area restaurada
  ao abrir o app agora respeitam as areas ocultas.
- O app impede ocultar todas as areas ao mesmo tempo, evitando que o usuario
  fique sem caminho para abrir a biblioteca.

## v0.18.6
- Livros agora abrem por padrao em `M`; quando o usuario muda para `P`, `G` ou
  `XG`, essa escolha fica salva naquele livro.
- PDFs usam a folha inteira como leitura continua: o usuario rola ate o fim da
  pagina e so entao avanca para a proxima.
- Ajuste fino de zoom para `P`, `M`, `G` e `XG`, mantendo a pagina preenchendo a
  area util do Switch e evitando a sensacao de "foto estatica".
- Catalogo evita a segunda chamada de rede na variacao aleatoria quando ja sabe
  o total de paginas da area, reduzindo demora ao alternar Mangas/HQ/Livros.
- Loop principal reduz redraw desnecessario quando capas terminam de baixar fora
  das telas de catalogo, poupando CPU/GPU e bateria.

## v0.18.5
- Area Livros restaurada no Meruem principal, usando `/switch/books/*` para
  listar, baixar e abrir PDF/EPUB do Meruem no mesmo leitor do app.
- Leitura Local agora tambem reconhece PDF/EPUB no SD, com pasta padrao
  `sdmc:/Livros`, progresso em Continuar lendo e suporte offline sem servidor.
- Leitor de PDF/EPUB ganhou area segura contra sobreposicao da barra, controle
  `Y`/botao `Texto` ou `Zoom` e mensagens mais claras de download/abertura.
- PDF/EPUB agora corta margem branca automaticamente e usa preenchimento em
  paginas de capa/arte, para ocupar melhor a tela do Switch sem deformar.
- Tamanho de texto/zoom de livros agora tem `P`, `M`, `G` e `XG`.
- O app salva a ultima area usada (`Mangas`, `HQ`, `Livros`, `Baixados` ou
  `Local`) e volta para ela na proxima abertura/biblioteca quando possivel.
- Catalogo online agora varia a pagina inicial ao entrar em uma area sem busca,
  ajudando o usuario a descobrir obras diferentes em Mangas, HQ e Livros.
- Leitor de PDF/EPUB nao mostra mais legenda inferior por cima do texto; PDFs
  em `M`, `G` e `XG` abrem como leitura rolavel em vez de pagina estatica.
- Busca no app agora mostra `Limpar`, e `B`/`X` limpam uma busca ativa antes de
  sair; o proxy tambem tenta achar livros pelo nome do volume/PDF quando a busca
  por obra volta vazia.

## v0.18.3
- Webtoon/manhwa: paginas em tira vertical agora preenchem a LARGURA da tela e
  rolam na vertical, em vez de aparecerem minusculas e exigirem zoom (que borrava
  a imagem). Comecam no topo da tira.
- Arraste com um dedo rola a tira; no controle, A/R/direita/baixo descem a tira
  e so viram de pagina ao chegar no fim; L/esquerda/cima sobem. Toque ou
  arraste lateral tambem respeita esse fluxo.
- Imagens grandes nao tem mais a largura destruida: o redimensionamento passou a
  limitar largura e altura separadamente, mantendo a nitidez das tiras altas.

## v0.18.2
- Textos longos nao invadem mais botoes nem saem da tela: botoes, barras de topo,
  rodapes, modais, listas, Conta, Local, Baixados e o leitor agora desenham com
  limite de largura.
- Texto que nao cabe termina com reticencias (`...`) em vez de cortar no meio da
  palavra. O titulo da serie nao sobrepoe mais os botoes da direita.
- Conta > Leitura local mostra o fim do caminho da pasta (`.../Mangas/Deadpool`).
- Layout revisado nos modos portatil (em pe) e TV (dock), sem sobreposicao.

## v0.18.1
- Leitura Local: abre CBZ e pastas com imagens (`.jpg`, `.jpeg`, `.png`, `.webp`,
  `.bmp`) direto do SD, sem precisar de rede.
- Nova area Local no ciclo de areas (Mangas, HQ, Baixados, Local), usando o mesmo
  leitor das obras online (toque, giro, zoom, cache e progresso).
- Pasta padrao `sdmc:/Mangas` e navegador para escolher e fixar a sua pasta.

## v0.17.0
- Selo de serie ja baixada no catalogo (verde = completa, `1/2` = parcial) e botao
  que muda para "Baixada"/"Completar" na tela de capitulos.
- Toque duplo para dar zoom no leitor.
- Tela Baixados mostra o espaco usado no SD e ganha o botao "Apagar tudo".

## v0.15.0
- Download offline em paralelo (ate 4 paginas ao mesmo tempo), bem mais rapido que
  o modelo antigo pagina a pagina.
- Paginas ja salvas sao puladas, entao retomar um download continua de onde parou.

## v0.14.0
- Menos consumo de bateria: render preguicoso e cache de texturas de texto.
- Melhor qualidade de imagem: paginas muito grandes sao redimensionadas com
  suavizacao (menos serrilhado).

## v0.13.0
- Baixar uma serie inteira de uma vez (todos os capitulos) pela tela de capitulos.
- Gerenciador de baixados: tamanho por item e total, apagar e refazer falhas.
- O atualizador passa a mostrar o changelog da nova versao.

## v0.11.2
- Base do app: catalogo de mangas e HQs, login com conta Meruem, favoritos e
  continuar lendo.
- Leitor com toque e controle, modo retrato e zoom por pinca.
- Atualizacao do `.nro` pelo proprio app via GitHub Releases.
