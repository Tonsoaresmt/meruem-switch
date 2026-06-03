# Leitura Local no SD

O Meruem Switch tambem pode ler mangas proprios direto do SD do dispositivo, sem
passar pelo servidor Meruem. Isso e util para colecoes pessoais, viagens ou uso
sem internet.

## Pasta Padrao

A pasta recomendada e:

```text
sdmc:/Mangas
```

No PC, abra o SD do Switch e coloque seus arquivos dentro da pasta `Mangas`.
Depois, no app:

1. Abra `Conta`.
2. Toque em `Usar padrao`.
3. Volte para a biblioteca.
4. Troque a area para `Local`.
5. Abra o manga pelo mesmo leitor do Meruem.

## Formatos Locais Suportados

Suportado agora:

- `.cbz`;
- pastas com imagens `.jpg`, `.jpeg`, `.png`, `.webp` ou `.bmp`.

Exemplos validos:

```text
sdmc:/Mangas/Volume 1.cbz
sdmc:/Mangas/In the Land of Leadale/Volume 1.cbz
sdmc:/Mangas/Minha Obra/Capitulo 01/001.jpg
sdmc:/Mangas/Minha Obra/Capitulo 01/002.jpg
```

Ainda nao suportado para leitura local:

- `.cbr`;
- `.pdf`;
- `.epub`.

## Mesmo Leitor

Arquivos locais usam o mesmo leitor do conteudo online:

- toque para avancar e voltar paginas;
- giro/retrato;
- zoom por pinca;
- cache de pagina atual, anterior e proxima;
- tela de loading;
- progresso de leitura.

## Manhwa e Webtoon

Manhwa/Webtoon em tiras verticais tambem pode ser lido pelo app quando estiver
em `.cbz` ou em pastas com imagens. O leitor detecta imagens muito altas,
preenche a largura da tela e rola verticalmente antes de virar para a proxima
pagina.

Funciona melhor quando o capitulo esta assim:

```text
sdmc:/Mangas/Meu Manhwa/Capitulo 01.cbz
sdmc:/Mangas/Meu Webtoon/Capitulo 01/001.webp
sdmc:/Mangas/Meu Webtoon/Capitulo 01/002.webp
```

## Baixados vs Local

`Baixados` guarda capitulos baixados pelo proprio Meruem para uso offline.
`Local` mostra arquivos que voce colocou manualmente no SD.

Essas areas ficam separadas por seguranca: o botao de apagar offline nunca deve
apagar mangas pessoais colocados manualmente na pasta `sdmc:/Mangas`.

## Se Nao Aparecer Nada

Confira:

- se os arquivos estao dentro de `sdmc:/Mangas`;
- se o formato e `.cbz` ou uma pasta com imagens suportadas;
- se a area atual do app esta em `Local`;
- se a pasta escolhida em `Conta > Escolher local` aponta para o lugar certo.

Quando a pasta Local esta vazia, ela pode ser pulada ao trocar de area para
evitar uma tela em branco sem utilidade.
