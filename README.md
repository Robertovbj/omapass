# omapass

[![CI](https://github.com/higorhgon/omapass/actions/workflows/ci.yml/badge.svg)](https://github.com/higorhgon/fpass/actions/workflows/ci.yml)

Gerenciador de senhas em C++ com interface Qt Quick. Segue o tema do Omarchy, a
fonte monoespaçada do sistema e o modo claro/escuro automaticamente. Funciona com
quatro backends: KeePass/KeePassXC (o `.kdbx` é lido e gravado pelo próprio
omapass, sem programa externo), [pass](https://www.passwordstore.org/) (via
`gpg`), Bitwarden (direto na API do servidor, seja bitwarden.com ou um
Vaultwarden próprio, sem CLI) e 1Password (via o CLI oficial `op`).

Segue a linha do [omacalc](https://github.com/omacom-io/omacalc),
[omawrite](https://github.com/omacom-io/omawrite) e
[omacut](https://github.com/omacom-io/omacut): Qt 6 + QML, com as cores lidas do
tema ativo do Omarchy e retintadas ao vivo quando o tema muda.

A documentação completa está na [wiki](https://github.com/higorhgon/omapass/wiki).

## Funcionalidades

- Detecta bancos KeePassXC e pass sozinho e ajusta a interface a cada um
- Contas Bitwarden (bitwarden.com, bitwarden.eu, Vaultwarden, self-hosted) e 1Password, com login e verificação em duas etapas pela própria interface
- Criação de bancos pela interface, sem sair do omapass
- Busca multi-termo, navegação estilo vim e ordenação por frecency (frequência + recência de uso)
- Adição, edição, exclusão e renomeação de grupos e entradas
- [Códigos TOTP](https://github.com/higorhgon/omapass/wiki/TOTP) nos quatro backends, com `Ctrl+T` para copiar
- [Ícones](https://github.com/higorhgon/omapass/wiki/%C3%8Dcones) dos sites na lista (opcional) ou escolhidos à mão para cada entrada
- [Integração com o navegador](https://github.com/higorhgon/omapass/wiki/Navegador) pela extensão KeePassXC-Browser, que preenche e salva logins nos sites (Linux)
- [Gerador de senhas e de frases](https://github.com/higorhgon/omapass/wiki/Gerador-de-senhas) (diceware, com lista em pt-BR e em inglês)
- [Desbloqueio opcional por PIN](https://github.com/higorhgon/omapass/wiki/Desbloqueio-por-PIN), um PIN por banco
- Clipboard marcado como sensível e limpo depois de 10 segundos
- Trava por inatividade e fecha quando a tela do sistema é bloqueada
- Cores do tema do Omarchy, com sobreposição opcional por tema próprio em TOML
- Suporte a mouse, além do teclado

## Instalação

Precisa de Qt 6 (`qt6-base`, `qt6-declarative`), Botan 3 (ou 2.19+) e zlib. Cada backend
tem os seus requisitos (`pass` e `gpg`, o `op` do 1Password); a lista completa, os pacotes do
Ubuntu e os builds para Windows e macOS estão em
[Instalação](https://github.com/higorhgon/omapass/wiki/Instala%C3%A7%C3%A3o).

```bash
git clone https://github.com/higorhgon/omapass.git
cd omapass
make
sudo make install   # ou: make install PREFIX="$HOME/.local"
```

O binário fica em `build/omapass`, e os testes rodam com `make test`. Há AppImage,
instalador para Windows e DMG para macOS Intel nas
[releases](https://github.com/higorhgon/omapass/releases).

`omapass --doctor` mostra o que foi encontrado no sistema e para que serve cada coisa.

## Uso

```bash
omapass
```

O omapass procura arquivos `.kdbx` e password-stores do pass (diretórios com `.gpg-id`)
dentro do diretório configurado (padrão: o home) e lista também as contas Bitwarden e
1Password adicionadas por ele. Havendo um único banco, ele vai direto para o desbloqueio.

Na tela de bancos, `Ctrl+A` cria um banco ou adiciona uma conta. `Ctrl+?` mostra todos os
atalhos, e `Ctrl+O` abre as configurações.

### Navegador

O omapass atende a extensão KeePassXC-Browser no lugar do KeePassXC. Ligue "Integração com o
navegador" em `Ctrl+O`, instale a extensão e reinicie o navegador (o Firefox e os derivados, como
o Zen, só enxergam a mudança ao abrir de novo). Depois, com um banco desbloqueado, clique em
"Conectar" na extensão e dê um nome à conexão. Por enquanto, só no Linux. O resto está em
[Navegador](https://github.com/higorhgon/omapass/wiki/Navegador).

## Documentação

- [Instalação](https://github.com/higorhgon/omapass/wiki/Instala%C3%A7%C3%A3o)
- [Configuração](https://github.com/higorhgon/omapass/wiki/Configura%C3%A7%C3%A3o): `config.toml`, cores, fonte e tamanho do texto
- [Atalhos](https://github.com/higorhgon/omapass/wiki/Atalhos)
- Backends: [KeePass](https://github.com/higorhgon/omapass/wiki/KeePass), [pass](https://github.com/higorhgon/omapass/wiki/pass), [Bitwarden](https://github.com/higorhgon/omapass/wiki/Bitwarden), [1Password](https://github.com/higorhgon/omapass/wiki/1Password)
- [Desbloqueio por PIN](https://github.com/higorhgon/omapass/wiki/Desbloqueio-por-PIN)
- [TOTP](https://github.com/higorhgon/omapass/wiki/TOTP)
- [Ícones](https://github.com/higorhgon/omapass/wiki/%C3%8Dcones)
- [Navegador](https://github.com/higorhgon/omapass/wiki/Navegador)
- [Gerador de senhas](https://github.com/higorhgon/omapass/wiki/Gerador-de-senhas)
- [Segurança](https://github.com/higorhgon/omapass/wiki/Seguran%C3%A7a)
- [Releases](https://github.com/higorhgon/omapass/wiki/Releases)
