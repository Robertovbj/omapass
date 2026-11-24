# omapass

[![CI](https://github.com/higorhgon/omapass/actions/workflows/ci.yml/badge.svg)](https://github.com/higorhgon/omapass/actions/workflows/ci.yml)

Gerenciador de senhas em C++ com interface Qt Quick, no visual dos apps do Omarchy (Hype,
Omajim). Segue as cores do tema do Omarchy, os cantos das janelas do Hyprland, a fonte do sistema
e o modo claro/escuro. Senhas e códigos aparecem em fonte monoespaçada. Funciona com
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
- [URLs adicionais](https://github.com/higorhgon/omapass/wiki/URLs-adicionais): a mesma entrada em vários endereços, também na busca e no navegador
- [Integração com o navegador](https://github.com/higorhgon/omapass/wiki/Navegador) pela extensão KeePassXC-Browser, que preenche e salva logins nos sites (Linux)
- Integração com o agente SSH para chaves do Bitwarden e anexos configurados no KeePassXC
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

Com um banco desbloqueado, clique no nome dele no cabeçalho ou use `Ctrl+Tab`
para alternar entre bancos abertos. **Abrir outro banco…** permite desbloquear
mais um. Na lista de entradas, `Esc`, `q` e o cadeado bloqueiam só o banco
exibido. O bloqueio por inatividade e o da sessão protegem todos os bancos.

A extensão busca logins nos bancos abertos que você conectou ao navegador.
Ao atualizar uma senha, ela grava no banco de origem da entrada. Para um login
novo, usa o banco exibido se ele estiver conectado.

Fora de um campo de texto, pressione `f` e digite as letras exibidas sobre o item
que deseja clicar. `Esc` cancela. Para pesquisar na lista, use `/` ou `i`.

Na tela de bancos, `Ctrl+A` cria um banco ou adiciona uma conta. `Ctrl+?` mostra todos os
atalhos, e `Ctrl+O` abre as configurações.

### Navegador

O omapass atende a extensão KeePassXC-Browser no lugar do KeePassXC. Ligue "Integração com o
navegador" em `Ctrl+O`, instale a extensão e reinicie o navegador (o Firefox e os derivados, como
o Zen, só enxergam a mudança ao abrir de novo). Depois, com um banco desbloqueado, clique em
"Conectar" na extensão e dê um nome à conexão. Por enquanto, só no Linux. O resto está em
[Navegador](https://github.com/higorhgon/omapass/wiki/Navegador).

### Agente SSH

O omapass usa o agente indicado por `SSH_AUTH_SOCK`. No Linux, quando a variável está
vazia, procura o socket do OpenSSH em `$XDG_RUNTIME_DIR/ssh-agent.socket`. Em sistemas
que fornecem a unidade `ssh-agent.socket`, habilite-a uma vez com
`systemctl --user enable --now ssh-agent.socket`. Para o terminal usar o mesmo agente,
defina `export SSH_AUTH_SOCK="$XDG_RUNTIME_DIR/ssh-agent.socket"` no ambiente da sessão.

Com as ferramentas OpenSSH instaladas, abra o menu de uma chave SSH para adicioná-la ou removê-la do agente. Em **Opções do agente SSH**,
você pode habilitar a inclusão no desbloqueio, definir o tempo de vida e pedir confirmação
a cada uso. As opções são locais e passam a valer na próxima inclusão da chave.

O omapass usa itens de chave SSH do Bitwarden e lê as opções de anexos `KeeAgent.settings`
do KeePassXC. A chave privada precisa estar sem frase secreta e é enviada ao OpenSSH
pela entrada padrão. Ao bloquear o banco ou fechar o aplicativo, o omapass remove
do agente as chaves que adicionou.

## Documentação

- [Instalação](https://github.com/higorhgon/omapass/wiki/Instala%C3%A7%C3%A3o)
- [Configuração](https://github.com/higorhgon/omapass/wiki/Configura%C3%A7%C3%A3o): `config.toml`, cores, fonte e tamanho do texto
- [Atalhos](https://github.com/higorhgon/omapass/wiki/Atalhos)
- Backends: [KeePass](https://github.com/higorhgon/omapass/wiki/KeePass), [pass](https://github.com/higorhgon/omapass/wiki/pass), [Bitwarden](https://github.com/higorhgon/omapass/wiki/Bitwarden), [1Password](https://github.com/higorhgon/omapass/wiki/1Password)
- [Desbloqueio por PIN](https://github.com/higorhgon/omapass/wiki/Desbloqueio-por-PIN)
- [TOTP](https://github.com/higorhgon/omapass/wiki/TOTP)
- [Ícones](https://github.com/higorhgon/omapass/wiki/%C3%8Dcones)
- [Navegador](https://github.com/higorhgon/omapass/wiki/Navegador)
- [URLs adicionais](https://github.com/higorhgon/omapass/wiki/URLs-adicionais)
- [Gerador de senhas](https://github.com/higorhgon/omapass/wiki/Gerador-de-senhas)
- [Segurança](https://github.com/higorhgon/omapass/wiki/Seguran%C3%A7a)
- [Releases](https://github.com/higorhgon/omapass/wiki/Releases)
