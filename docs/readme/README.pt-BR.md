<p align="center">
  <img src="../../app/res/deskport.svg" alt="DeskPort" width="128" height="128">
</p>

<h1 align="center">DeskPort</h1>

> **Current desktop release: [0.7.4 (r2)](https://github.com/keithxc/deskport/releases/tag/v0.7.4).**
> The translated package examples and verification history below still describe
> 0.7.3. Use the [current installation guide](../../README.md#install) and
> [0.7.4 release notes](../releases/RELEASE_0.7.4.md) for the replacement downloads.

<p align="center">
  <a href="../../README.md">English</a> ·
  <a href="README.zh-CN.md">简体中文</a> ·
  <a href="README.zh-TW.md">繁體中文</a> ·
  <a href="README.ja.md">日本語</a> ·
  <a href="README.ko.md">한국어</a> ·
  <a href="README.de.md">Deutsch</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.fr.md">Français</a><br>
  <b>Português (Brasil)</b> ·
  <a href="README.ru.md">Русский</a> ·
  <a href="README.it.md">Italiano</a> ·
  <a href="README.nl.md">Nederlands</a> ·
  <a href="README.pl.md">Polski</a> ·
  <a href="README.tr.md">Türkçe</a> ·
  <a href="README.uk.md">Українська</a><br>
  <a href="README.ar.md">العربية</a> ·
  <a href="README.hi.md">हिन्दी</a> ·
  <a href="README.id.md">Bahasa Indonesia</a> ·
  <a href="README.ms.md">Bahasa Melayu</a> ·
  <a href="README.th.md">ไทย</a> ·
  <a href="README.vi.md">Tiếng Việt</a> ·
  <a href="README.bo.md">བོད་ཡིག</a>
</p>

<p align="center">
  Um espaço de trabalho de área de trabalho remota baseado em Moonlight e Sunshine.<br>
  Mantenha sua área de trabalho remota pronta em segundo plano, traga-a para o seu espaço
  de trabalho atual com uma única ação e guarde-a sem reconectar.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Principais recursos

- **Um único app de desktop para visualizar e compartilhar.** Sunshine integrado, aprovação de dispositivos,
  credenciais e configurações independentes; sem instalação separada do Sunshine nos
  pacotes de host para macOS, Windows e Linux (exceto o Flatpak somente visualizador).
- **Um espaço de trabalho que acompanha seu cliente.** Dimensionamento adaptativo em hosts macOS
  e KDE compatíveis, HiDPI, layouts móveis retrato/paisagem e ajuste de tamanho por dispositivo.
  Mudanças de tamanho reconectam o vídeo brevemente; não são trocas de codificador sem interrupção.
- **Escolha a política de tela do host.** Tela principal virtual com telas espelhadas,
  tela principal virtual com as demais telas desativadas ou um espaço de trabalho estendido em hosts
  compatíveis. O DeskPort restaura o layout salvo quando a sessão termina.
- **Alterne entre dispositivos autorizados.** Confirme antes de assumir uma sessão
  ativa; o cliente desconectado recebe uma explicação autenticada sobre a tomada de controle.
- **Mantenha o trabalho no desktop ao alcance.** Oculte e chame de volta um visualizador conectado, libere
  a entrada remota quando oculto e desconecte a visualização independentemente do compartilhamento local.
- **Salve configurações por computador.** Imagem, áudio, entrada, ajustes do desktop e edição de
  endereço ficam com o dispositivo; cartões, fixação, temas e interfaces localizadas.
- **Compartilhe a área de transferência do desktop sob demanda.** O texto é atualizado imediatamente;
  imagens e arquivos compatíveis são buscados quando solicitados, entre pares de desktop vinculados
  que optaram por isso. Veja [escopo e limites da área de transferência](../CLIPBOARD.md).
- **Controles móveis nativos.** iPhone/iPad e o cliente Android oferecem
  entrada por toque/trackpad, controles de teclado e acesso direto à área de trabalho remota.
- **Inicialização opcional no login e recuperação no macOS.** Exigem um login gráfico
  ativo e aprovação do sistema; não desbloqueiam o FileVault nem fazem login por você.

## Prévia da interface

Capturas da interface atual com dispositivos de demonstração sintéticos. Clique em um painel
para ver a imagem em tamanho real; estas prévias não implicam disponibilidade em loja ou em versão publicada.

| Dispositivos e grupos | Configurações de tela por dispositivo | Manual integrado |
| --- | --- | --- |
| [![Dispositivos e grupos](../media/devices.png)](../media/devices.png) | [![Configurações de tela por dispositivo](../media/device-settings.png)](../media/device-settings.png) | [![Manual integrado](../media/manual.png)](../media/manual.png) |

## Plataformas suportadas

Os links abaixo correspondem à versão oficial 0.7.3. As verificações dos pacotes e da execução têm escopos diferentes; consulte os resultados e as limitações no [relatório de verificação](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

| Plataforma | Função | Status da versão | Download |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Visualizador + host + tela virtual nativa | **0.7.3 estável**; assinaturas Developer ID, notarização da Apple e Gatekeeper após extrair o ZIP verificados | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Visualizador + host; espaço de trabalho virtual adaptativo em hosts KWin, Mutter e Hyprland compatíveis | **0.7.3 estável**: DEB, RPM, Arch, AppImage, arquivo portátil e Nix | [Versão](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [guia de instalação](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Somente visualizador | **0.7.3 estável**, Freedesktop Platform 25.08 | [Versão](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Somente cliente | App Store; builds mais recentes testados separadamente no TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Somente cliente | **2.7 (63)** · Teste fechado do Google Play em andamento | [Baixar APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Visualizador + host + tela virtual | **0.7.3 estável**; instalador com host; ZIP portátil apenas como visualizador; pacote e CLI verificados sem instalação; aplicativo e instalador sem assinatura | [Instalador](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Mac Intel | Não qualificado | Nenhum pacote de versão verificado | — |

> **Aviso sobre antivírus no Windows:** Antivírus podem sinalizar os componentes auxiliares do DeskPort para Windows como falso positivo.
> Não encontramos esse problema em nossos próprios testes. Mantenha o Defender ativado; se aparecer um alerta,
> consulte o relatório de verificação da versão e o [histórico de amostras](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Os pacotes nativos para Linux exigem glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
Arch atual. O AppImage inclui um runtime privado para desktops com glibc mais antiga;
drivers de hardware e suporte do compositor ainda determinam as capacidades de host. Hospedar com tela virtual no KDE exige
entradas de permissão do compositor. Pacotes nativos/Nix fornecem entradas fixas; os builds
AppImage e portáteis registram automaticamente o caminho atual do helper de tela
quando o compartilhamento é iniciado. A entrada no host ainda exige permissões de dispositivo. Apps móveis
não hospedam desktops.
Uma definição de código-fonte ou uma compilação bem-sucedida, por si só, não é uma plataforma suportada.

## Instalação

**macOS:** Abra o [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
arraste o DeskPort para Aplicativos e abra-o. Conceda as permissões de Gravação de Tela e
Acessibilidade para usar os recursos de host. Nix e Homebrew não são necessários.

**Linux:** Escolha o [pacote 0.7.3](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
para o seu sistema e siga o [guia de instalação](../LINUX_PACKAGES.md),
incluindo as permissões de entrada do host. Com Nix: `nix run github:keithxc/deskport/v0.7.3`.
O Flatpak oferece apenas visualização. Os arquivos da versão incluem [checksums SHA-256](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) e um [relatório de verificação](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · Até a conclusão do teste fechado do Google Play, o [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) está disponível temporariamente no GitHub. Os clientes móveis têm código fechado. Aqui é distribuído apenas o instalador.

A compilação 63 usa a mesma chave de assinatura do Google Play. Ao atualizar para uma versão mais recente da loja, os computadores pareados e as configurações são mantidos. Se você instalou a compilação 62 do GitHub, desinstale-a antes de instalar a 63. As configurações locais serão apagadas e será preciso parear os computadores novamente.

**iPhone / iPad:** Instale o [DeskPort pela App Store](https://apps.apple.com/us/app/deskport/id6812389978).
O app móvel é uma compra única paga; consulte sua loja para ver o preço local atual.
As versões para desktop continuam gratuitas e de código aberto. As compras apoiam a assinatura,
o hardware de teste e o desenvolvimento contínuo.

Inicie o Compartilhamento no host, selecione-o no cliente e aprove o dispositivo no
host. Ambos os dispositivos precisam de um caminho de rede funcional: use uma LAN ou sua própria VPN, como
o Tailscale. O DeskPort não oferece serviço de contas, desktops hospedados, relay
nem túnel de rede. O pareamento legado por PIN do Sunshine continua disponível.

**0.7.3 estável:** Esta versão traz um catálogo de interface compartilhado em 22 idiomas com troca de idioma no navegador e melhora o compartilhamento em sessões Windows com usuário conectado e o DPI das telas virtuais, o runtime Linux e a área de transferência Wayland, os espaços de trabalho Hyprland/X11 protegidos, o IME local opcional e os cartões quadrados de dispositivos. Consulte as mudanças e os limites de verificação nas [notas da versão 0.7.3](../releases/RELEASE_0.7.3.md).

**Escopo da verificação de 0.7.3:** No Windows, passaram a comparação exata do conteúdo, as verificações PE e de CLI nativa e a análise do Defender sem detecções; instalação, conexão gráfica e streaming não foram testados. O AppImage final passou na VM Bazzite com FUSE normal e GPU por software. Na VM Ubuntu 26.04, o DEB de 0.7.2 reproduziu a falha de VA; o DEB final de 0.7.3 corrigiu a falha e passou pela atualização. A reversão reproduziu a falha da versão anterior; a desinstalação posterior preservou a configuração sintética de teste. A captura e a recuperação no GNOME continuam sem aceitação. Os resultados de contêineres/Flatpak e os demais limites estão no [relatório de verificação](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt); isso não representa validação completa em hardware físico.

**Histórico: 0.7.2 estável:** A [versão para desktop](https://github.com/keithxc/deskport/releases/tag/v0.7.2) reúne melhorias de redimensionamento, streaming remoto, economia de dados e runtime Linux, e corrige problemas do AppImage no Bazzite/KDE Wayland. Veja as [notas da versão 0.7.2](../releases/RELEASE_0.7.2.md); os passos de instalação por distribuição Linux estão no [README em inglês](../../README.md#linux-installation-by-distribution).

**Acesso pelo navegador:** Os hosts de desktop servem uma área de trabalho remota HTTPS com um
código de login de 30 segundos, pareamento opcional do navegador por sete dias, dimensionamento adaptativo
e tela cheia. Veja as [notas da versão 0.7.1](../releases/RELEASE_0.7.1.md) e o
[guia do navegador](../BROWSER_ACCESS.md). O AppImage inclui um runtime glibc privado;
os pacotes nativos mantêm o requisito de glibc 2.39.

### Disponibilidade em repositórios Linux

| Canal | Status | Instalação |
| --- | --- | --- |
| Pacotes de versão no GitHub | 0.7.3 estável | Baixe acima o pacote para sua distribuição |
| Flake Nix upstream | 0.7.3 estável | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Receita testada; publicação bloqueada pela pausa no registro de novas contas | [Compile a receita localmente](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) enviado para revisão; não mesclado | Use o flake upstream até a inclusão |
| Repositórios oficiais do Arch / Flathub | Não listado | Use os downloads da versão; o Flatpak é somente visualizador |

O pacote Arch para download é instalado com `pacman -U`; ele não torna
`pacman -S deskport` disponível. Da mesma forma, o flake upstream não torna
`pkgs.deskport` disponível no Nixpkgs. Entrada do host, permissões de captura e configuração
de firewall são separadas da instalação do aplicativo; veja o
[guia de instalação para Linux](../LINUX_PACKAGES.md#nixos-and-nix).

## Comparação com produtos semelhantes

Esta é uma comparação de fluxo de trabalho e plataformas, conferida com a documentação oficial
vinculada em **2026-09-20**, e não um benchmark de latência ou qualidade de imagem. Recursos
dos produtos e limites de plataforma podem mudar.

| Aspecto | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Fluxo de trabalho principal | Espaço de trabalho remoto persistente, dimensionamento adaptativo e transferência entre dispositivos | Streaming de jogos/desktop com apps de cliente e host separados | Controle remoto geral e suporte | Área de trabalho remota interativa e colaboração |
| Plataformas de host desktop | macOS Apple Silicon, Linux x86_64 e Windows x64 | Windows, macOS, Linux e FreeBSD, sujeito aos limites de cada plataforma | Windows, macOS e Linux | Windows e macOS; Linux não pode hospedar |
| Clientes desktop | macOS Apple Silicon, Linux x86_64 e Windows x64 | Windows, macOS e Linux, entre outros | Windows, macOS e Linux | Windows, macOS, Linux e navegadores Chromium compatíveis |
| Clientes móveis | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS e Android | iOS/iPadOS e Android; iOS não pode hospedar | Android; sem cliente iOS/iPadOS |
| Modelo de configuração | Host integrado nos pacotes nativos de desktop; aprove dispositivos no DeskPort | Instalar e configurar o Sunshine separadamente e depois parear o Moonlight | Infraestrutura de servidores pública ou servidor auto-hospedado | Conta e aplicativo Parsec |
| Modelo de rede/serviço | Use sua própria conectividade LAN/VPN; sem relay do DeskPort | Streaming auto-hospedado; configurar a conectividade de rede | Infraestrutura pública ou servidor OSS/Pro auto-hospedado | Infraestrutura de conta/serviço Parsec |
| Código e distribuição | Desktop GPL, pacotes desktop gratuitos; app Apple pago distribuído separadamente | Clientes e host de código aberto | Cliente de código aberto e servidor OSS; opção comercial de servidor Pro | Aplicativo/serviço proprietário |

Fontes: [Moonlight](https://moonlight-stream.org/),
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk clients](https://rustdesk.com/docs/en/client/),
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/),
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
Os limites atuais de plataforma do DeskPort estão listados acima; esta tabela não implica
paridade de recursos em todos os sistemas operacionais nem vantagem de desempenho medida.

Veja as [notas da versão 0.7.3](../releases/RELEASE_0.7.3.md),
a [arquitetura](../ARCHITECTURE.md) e o [roadmap](../ROADMAP.md).

## Compilar e executar no Linux

Com Nix e flakes habilitados:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

As dependências de código de terceiros do visualizador estão incluídas no repositório (vendored); o núcleo compartilhado e
as entradas de build do Nix continuam sendo dependências externas fixadas. Builds nativos precisam inicializar
`shared/deskport-core`. Veja [docs/VENDORED.md](../VENDORED.md).
Para configuração de SSH, gerenciamento local do host, pré-requisitos de VPS e Omarchy/Hyprland,
veja o [guia da CLI](../CLI.md). `deskport doctor` verifica os pré-requisitos de hospedagem;
`deskport status --json` inspeciona a instância em execução sem abrir uma janela.
`nix run . -- --help` lista a interface de linha de comando. Inicie o Compartilhamento no host e vincule os dispositivos antes de conectar. O pareamento
legado por PIN do Sunshine também está disponível. Nenhuma credencial pessoal de host ou de pareamento
é incluída ou importada do Moonlight.
Novos endereços manuais usam por padrão a porta do DeskPort `48989`. Inclua a porta mostrada
na página de compartilhamento do host, se for diferente, ou use `host:47989` para conectar explicitamente
a uma instalação autônoma padrão do Sunshine. Endpoints salvos/descobertos mantêm
suas próprias portas.

Para um build nativo editável:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

Os nomes de arquivo do projeto upstream permanecem inalterados para manter o fork fácil de revisar.

## O que muda em relação ao Moonlight?

- Host Sunshine integrado com aprovação de dispositivos, tomada de controle e configurações por dispositivo.
- As sessões continuam conectadas enquanto o visualizador está oculto; chame-o de volta pela bandeja/barra de menus.
- Acesso pelo navegador via HTTPS servido pelos hosts de desktop.
- Executável `deskport` separado, namespace de configurações Qt `DeskPort` e
  ID de aplicativo Linux `io.github.keithxc.DeskPort`.
- Streaming em janela e controle por ponteiro absoluto por padrão.
- Silenciar ao perder o foco; otimização para jogos, mouse pelo gamepad, modo multicontrole,
  entrada de gamepad em segundo plano e presença no Discord desativados por padrão.
- Sem avisos de atualização do Moonlight upstream para este aplicativo independente.
- Um ambiente Nix travado e um fluxo de build para Linux.

A interface de desktop oferece páginas de dispositivos, compartilhamento e configurações, com seleção
de idioma e permissões de host separadas. Fechar o visualizador mantém a sessão disponível e abre a lista de dispositivos.
Um dispositivo ativo oferece Voltar ao desktop; os outros dispositivos mostram detalhes até que a
sessão atual seja desconectada. Fixe dispositivos usados com frequência e escolha uma lista compacta
ou cartões. A aparência segue o sistema, com opções para forçar o modo claro ou escuro.

## Escopo das plataformas

Veja a [tabela de plataformas](#plataformas-suportadas) acima para o status de cada
plataforma. As versões para desktop cobrem macOS Apple Silicon, Linux x86_64 e Windows x64
como visualizador e host; os apps móveis são apenas clientes.

O suporte a plataformas cliente e o suporte a host são separados: um host Mac não exige
um cliente DeskPort para Mac. Decodificação por hardware, entrada ao vivo, qualidade de imagem e latência de retorno exigem
testes em sessões reais; um build bem-sucedido não os comprova.

## Próximo marco

Manter uma sessão conectada ao longo de **50 ciclos de ocultar/mostrar**, mostrar a janela no
espaço de trabalho atual e devolver a entrada local de forma confiável. Medir a latência de quadros novos
e o uso de recursos em segundo plano separadamente do aparecimento da janela.

Veja [o roadmap](../ROADMAP.md) para critérios de aceitação e recursos adiados,
e as [notas upstream](../UPSTREAM.md) sobre procedência e limites de manutenção.

## Validação

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

O teste de fumaça usa diretórios temporários de configuração/cache XDG e uma plataforma Qt
fora da tela. Ele não pareia com um host, não inicia um stream nem injeta entrada.

## Controles diários do desktop

Fechar uma janela mantém o DeskPort em execução na bandeja/barra de menus. Use **Open DeskPort**
para chamá-lo de volta, **Disconnect viewer** para encerrar apenas a conexão atual ou
**Quit DeskPort** para sair do serviço. O compartilhamento local continua quando um visualizador é fechado.

O compartilhamento da área de transferência de texto simples e a captura de atalhos do sistema vêm ativados por padrão em
ambos os dispositivos vinculados. Mudanças de configuração valem após reconectar. Apenas cópias novas são
compartilhadas, até 1 MiB. Imagens e arquivos compatíveis são buscados sob demanda entre
pares de desktop compatíveis que optaram por isso; veja os [limites](../CLIPBOARD.md). No modo de ponteiro
de desktop, o roteamento do teclado segue o ponteiro dentro do vídeo em foco.
**Ctrl+Alt+Shift+Z** libera a entrada;
**Ctrl+Alt+Shift+Q** desconecta o visualizador. Clique dentro para recuperar a entrada após
a liberação explícita. Atalhos reservados pelo sistema dependem do compositor do desktop.

A inicialização no login e a recuperação exigem uma sessão gráfica ativa. Veja
[verificações de aceitação e limitações](../INPUT_SERVICE_ACCEPTANCE.md) antes de depender
de um computador para acesso não supervisionado.

## Licença e atribuição

O DeskPort é um derivado independente do [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
inicialmente baseado na v6.1.0. Não é uma versão oficial do Moonlight nem do Sunshine.
O Moonlight fornece a base de streaming; o [Sunshine](https://github.com/LizardByte/Sunshine)
vem incluído nos pacotes de host para macOS, Windows e Linux (exceto o Flatpak somente visualizador) e é fornecido pelo pacote Nix para Linux.
Serviços do Sunshine instalados separadamente permanecem independentes.

GPL-3.0-or-later; veja [LICENSE](../../LICENSE), os avisos de código-fonte mantidos e a
licença de cada dependência incluída listada em [docs/VENDORED.md](../VENDORED.md). A documentação original está preservada em
[README.upstream.md](README.upstream.md).

### Diagnóstico e feedback

Os logs de diagnóstico vêm ativados por padrão. Reproduza o problema,
depois crie um ZIP de logs e abra um rascunho de issue no GitHub. Revise o ZIP antes de anexá-lo:
as issues do GitHub são públicas, e o DeskPort não envia nem submete nada por
você. Veja [dados de diagnóstico e limites](../DIAGNOSTICS.md).
