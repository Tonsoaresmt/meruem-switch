# Instalacao no Nintendo Switch

Este guia e para usuarios que querem instalar o app Meruem no Nintendo Switch.

## Requisitos

Voce precisa de:

- um Nintendo Switch capaz de abrir o Homebrew Menu;
- um cartao SD acessivel no computador;
- conexao com a internet no Switch;
- uma conta Meruem.

O app e distribuido como `.nro`. Ele nao e instalado pela eShop.

## 1. Baixar o App

Abra:

https://github.com/Tonsoaresmt/meruem-switch/releases/latest

Baixe:

```text
Meruem.nro
```

Use sempre o arquivo chamado exatamente `Meruem.nro`.

## 2. Copiar para o SD

No cartao SD, crie a pasta:

```text
sdmc:/switch/Meruem/
```

Copie o arquivo para:

```text
sdmc:/switch/Meruem/Meruem.nro
```

## 3. Abrir no Switch

1. Coloque o SD no Switch.
2. Abra o Homebrew Menu.
3. Selecione `Meruem`.
4. Faca login com sua conta Meruem.

Depois do login, o app carrega catalogo, favoritos, continuar lendo e dados da
conta.

## Ler Mangas Proprios no SD

Para ler arquivos proprios sem passar pelo servidor Meruem, coloque seus `.cbz`
ou pastas com imagens em:

```text
sdmc:/Mangas
```

No app, abra `Conta`, toque em `Usar padrao` e depois troque a area para
`Local`. Veja o guia completo em [LOCAL.md](LOCAL.md).

## Conta Gratuita e Assinatura

O Meruem pode oferecer leitura gratuita com limite diario. Para leitura sem
limite, e necessario ter assinatura ativa.

O plano de entrada parte de **R$ 6,90 por 3 meses** para leitura sem limite na
area escolhida, conforme os planos disponiveis na plataforma.

Esse preco e propositalmente baixo para manter o projeto acessivel e ajudar nos
custos reais de operacao: plataforma, armazenamento, trafego, manutencao e novas
melhorias.

Consulte a tela de planos no Meruem para ver as opcoes atuais.

## 4. Atualizar sem DBI

O app possui atualizador proprio.

Quando uma nova versao for publicada, o Meruem pode mostrar uma janela de
atualizacao ao abrir. Ao confirmar, ele baixa o novo `Meruem.nro` e substitui o
arquivo atual no SD.

Se a atualizacao automatica falhar:

1. baixe manualmente o `Meruem.nro` da release mais recente;
2. substitua o arquivo antigo em `sdmc:/switch/Meruem/Meruem.nro`;
3. abra o app novamente.

## Solucao de Problemas

### O app nao aparece no Homebrew Menu

Confira se o arquivo esta em:

```text
sdmc:/switch/Meruem/Meruem.nro
```

Tambem pode funcionar em:

```text
sdmc:/switch/Meruem.nro
```

Mas a pasta propria e recomendada.

### Login falha

Confira:

- se o Switch esta conectado a internet;
- se usuario e senha estao corretos;
- se a conta Meruem esta ativa;
- se a plataforma Meruem esta online.

### Algumas obras nao aparecem

O app do Switch mostra apenas conteudo compativel com leitura por paginas de
imagem. PDF, EPUB e livros de texto sao ocultados por enquanto.

### A tela fica roxa ao abrir uma obra

Isso geralmente indica que a obra nao esta disponivel como paginas de imagem.
Atualize o app e tente novamente. Se continuar, aquela obra provavelmente nao e
compativel com o leitor do Switch no momento.

### A atualizacao nao aparece

Confira:

- se existe uma release mais nova;
- se o Switch tem internet;
- se o arquivo atual esta em um caminho gravavel no SD;
- se o asset da release se chama `Meruem.nro`.
