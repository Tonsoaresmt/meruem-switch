# Changelog

Versoes publicadas do Meruem Switch. Todas as releases (com o `Meruem.nro`)
ficam em: <https://github.com/Tonsoaresmt/meruem-switch/releases>

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
