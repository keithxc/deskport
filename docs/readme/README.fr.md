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
  <b>Français</b><br>
  <a href="README.pt-BR.md">Português (Brasil)</a> ·
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
  Un espace de travail de bureau à distance basé sur Moonlight et Sunshine.<br>
  Gardez votre bureau distant prêt en arrière-plan, amenez-le sur votre espace de travail
  actuel en une action, et rangez-le sans vous reconnecter.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a> <del>$4.99</del>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Fonctionnalités principales

- **Une seule application de bureau pour afficher et partager.** Sunshine intégré, approbation des appareils,
  identifiants et réglages indépendants ; aucune installation séparée de Sunshine pour les
  paquets hôtes macOS, Windows et Linux (hors Flatpak, limité à l'affichage).
- **Un espace de travail qui suit votre client.** Dimensionnement adaptatif sur les hôtes macOS
  et KDE compatibles, HiDPI, dispositions mobiles portrait/paysage et réglage de taille par appareil.
  Les changements de taille reconnectent brièvement la vidéo ; ce ne sont pas des changements d'encodeur transparents.
- **Choisissez la politique d'affichage de l'hôte.** Écran principal virtuel avec écrans en miroir,
  écran principal virtuel avec les autres écrans désactivés, ou espace de travail étendu sur les hôtes
  compatibles. DeskPort restaure la disposition enregistrée à la fin de la session.
- **Passez d'un appareil autorisé à l'autre.** Confirmez avant de reprendre une session
  active ; le client évincé reçoit une explication authentifiée de la reprise.
- **Gardez le travail de bureau à portée de main.** Masquez et rappelez un visualiseur connecté, libérez
  l'entrée distante lorsqu'il est masqué, et déconnectez l'affichage indépendamment du partage local.
- **Enregistrez les réglages par ordinateur.** Image, audio, entrée, réglage du bureau et modification
  d'adresse restent liés à l'appareil ; cartes, épinglage, thèmes et interfaces localisées.
- **Partagez le presse-papiers du bureau à la demande.** Le texte est mis à jour immédiatement ;
  les images et fichiers pris en charge sont récupérés sur demande, entre pairs de bureau liés
  ayant activé l'option. Voir [portée et limites du presse-papiers](../CLIPBOARD.md).
- **Contrôles mobiles natifs.** iPhone/iPad et le client Android offrent
  la saisie tactile/trackpad, des contrôles clavier et un accès direct au bureau distant.
- **Démarrage à l'ouverture de session et récupération macOS, en option.** Ils nécessitent une session graphique
  active et l'approbation du système ; ils ne déverrouillent pas FileVault et n'ouvrent pas de session à votre place.

## Aperçu de l'interface

Captures d'écran de l'interface actuelle avec des appareils de démonstration synthétiques. Cliquez sur un panneau
pour afficher l'image en taille réelle ; ces aperçus n'impliquent aucune disponibilité en boutique ou en version publiée.

| Appareils et groupes | Réglages d'écran par appareil | Manuel intégré |
| --- | --- | --- |
| [![Appareils et groupes](../media/devices.png)](../media/devices.png) | [![Réglages d'écran par appareil](../media/device-settings.png)](../media/device-settings.png) | [![Manuel intégré](../media/manual.png)](../media/manual.png) |

## Plateformes prises en charge

Les liens ci-dessous correspondent à la version officielle 0.7.3. Les vérifications des paquets et de leur exécution ont des périmètres distincts ; les résultats et limites figurent dans le [rapport de vérification](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

| Plateforme | Rôle | État de la version | Téléchargement |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Visualiseur + hôte + écran virtuel natif | **0.7.3 stable** ; signatures Developer ID, notarisation Apple et contrôle Gatekeeper après extraction du ZIP validés | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Visualiseur + hôte ; espace de travail virtuel adaptatif sur les hôtes KWin, Mutter et Hyprland compatibles | **0.7.3 stable** : DEB, RPM, Arch, AppImage, archive portable et Nix | [Version](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [guide d'installation](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Visualiseur uniquement | **0.7.3 stable**, Freedesktop Platform 25.08 | [Version](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Client uniquement | App Store ; les builds plus récents sont testés séparément dans TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Client uniquement | **2.7 (63)** · Test fermé Google Play en cours | [Télécharger l’APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Visualiseur + hôte + écran virtuel | **0.7.3 stable** ; installateur avec hôte ; ZIP portable réservé à la visualisation ; paquet et CLI vérifiés sans installation ; application et installateur non signés | [Installateur](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Mac Intel | Non qualifié | Aucun paquet de version vérifié | — |

> **Avertissement antivirus Windows :** Un antivirus peut signaler à tort les composants auxiliaires Windows de DeskPort (faux positif).
> Nous ne l'avons pas constaté lors de nos propres tests. Gardez Defender activé ; si un avertissement apparaît,
> consultez le rapport de vérification de la version et l'[historique des échantillons](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Les paquets Linux natifs nécessitent glibc 2.39+ : Ubuntu 24.04+/Debian 13+, Fedora 44,
Arch actuel. L'AppImage embarque un runtime privé pour les bureaux avec une glibc plus ancienne ;
les pilotes matériels et la prise en charge du compositeur déterminent toujours les capacités d'hôte. L'hébergement avec écran virtuel sous KDE nécessite
des entrées d'autorisation du compositeur. Les paquets natifs/Nix fournissent des entrées fixes ; les builds
AppImage et portables enregistrent automatiquement le chemin actuel du helper d'affichage
au démarrage du partage. L'entrée sur l'hôte nécessite toujours des autorisations sur les périphériques. Les applications mobiles
n'hébergent pas de bureaux.
Une définition de source ou une compilation réussie ne constitue pas à elle seule une plateforme prise en charge.

## Installation

**macOS :** Ouvrez le [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
faites glisser DeskPort dans Applications et ouvrez-le. Accordez les autorisations Enregistrement de l'écran et
Accessibilité pour utiliser les fonctions d'hôte. Nix et Homebrew ne sont pas nécessaires.

**Linux :** Choisissez le [paquet 0.7.3](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
adapté à votre système et suivez le [guide d'installation](../LINUX_PACKAGES.md),
y compris les autorisations d'entrée de l'hôte. Avec Nix : `nix run github:keithxc/deskport/v0.7.3`.
Flatpak permet uniquement l'affichage. Les fichiers de la version incluent des [sommes de contrôle SHA-256](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) et un [rapport de vérification](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · ~~$4.99~~ · En attendant la fin du test fermé sur Google Play, l’[APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) est disponible temporairement sur GitHub. Le code source des clients mobiles est privé. Seul le fichier d’installation est distribué ici.

La version 63 utilise la même clé de signature que Google Play. Une mise à jour vers une version ultérieure de Play conserve les ordinateurs associés et les réglages. Si vous avez installé la version 62 de GitHub, vous devez la désinstaller avant d’installer la 63. Les réglages locaux seront effacés et il faudra associer de nouveau vos ordinateurs.

**iPhone / iPad :** Installez [DeskPort depuis l'App Store](https://apps.apple.com/us/app/deskport/id6812389978).
L'application mobile est un achat unique payant ; consultez votre boutique pour le prix local actuel.
Les versions de bureau restent gratuites et open source. Les achats financent la signature,
le matériel de test et la poursuite du développement.

Lancez le partage sur l'hôte, sélectionnez-le sur le client et approuvez l'appareil sur
l'hôte. Les deux appareils ont besoin d'un chemin réseau fonctionnel : utilisez un réseau local ou votre propre VPN, comme
Tailscale. DeskPort ne fournit ni service de compte, ni bureaux hébergés, ni relais,
ni tunnel réseau. L'appairage historique par code PIN Sunshine reste disponible.

**0.7.3 stable :** Cette version apporte un catalogue d’interface partagé en 22 langues avec changement de langue dans le navigateur et améliore le partage dans les sessions Windows ouvertes et le DPI des écrans virtuels, le runtime Linux et le presse-papiers Wayland, les espaces de travail Hyprland/X11 sécurisés, l’IME local optionnel et les cartes carrées des appareils. Les changements et limites de vérification figurent dans les [notes de version 0.7.3](../releases/RELEASE_0.7.3.md).

**Périmètre de vérification de 0.7.3 :** Sous Windows, la comparaison exacte du contenu, les contrôles PE et CLI native ainsi que l’analyse Defender sans détection ont réussi ; l’installation, la connexion graphique et le streaming n’ont pas été testés. L’AppImage final a passé les contrôles dans une VM Bazzite avec FUSE normal et GPU logiciel. Dans la VM Ubuntu 26.04, le DEB de 0.7.2 reproduisait le plantage VA ; le DEB final de 0.7.3 le corrige et a passé la mise à niveau. Le retour à la version précédente a reproduit son plantage ; la désinstallation qui a suivi a conservé la configuration synthétique de test. La capture et la récupération GNOME restent non validées. Les résultats des conteneurs/Flatpak et les autres limites figurent dans le [rapport de vérification](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt) ; cela ne constitue pas une validation complète sur matériel physique.

**Historique : 0.7.2 stable :** La [version de bureau](https://github.com/keithxc/deskport/releases/tag/v0.7.2) regroupe des améliorations du redimensionnement, du streaming à distance, de l’économiseur de données et du runtime Linux, et corrige des problèmes de l’AppImage sous Bazzite/KDE Wayland. Voir les [notes de version 0.7.2](../releases/RELEASE_0.7.2.md) ; les étapes d’installation par distribution Linux figurent dans le [README anglais](../../README.md#linux-installation-by-distribution).

**Accès par navigateur :** Les hôtes de bureau servent un bureau distant HTTPS avec un
code de connexion de 30 secondes, un appairage facultatif du navigateur pendant sept jours, un dimensionnement adaptatif
et le plein écran. Voir les [notes de version 0.7.1](../releases/RELEASE_0.7.1.md) et le
[guide du navigateur](../BROWSER_ACCESS.md). L'AppImage inclut un runtime glibc privé ;
les paquets natifs conservent leur exigence de glibc 2.39.

### Disponibilité dans les dépôts Linux

| Canal | État | Installation |
| --- | --- | --- |
| Paquets de version GitHub | 0.7.3 stable | Téléchargez ci-dessus le paquet de votre distribution |
| Flake Nix upstream | 0.7.3 stable | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Recette testée ; publication bloquée par la suspension des inscriptions de nouveaux comptes | [Compiler la recette localement](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) soumise pour revue ; non fusionnée | Utilisez le flake upstream jusqu'à l'intégration |
| Dépôts officiels Arch / Flathub | Non référencé | Utilisez les téléchargements de version ; Flatpak est limité à l'affichage |

Le paquet Arch téléchargeable s'installe avec `pacman -U` ; il ne rend pas
`pacman -S deskport` disponible. De même, le flake upstream ne rend pas
`pkgs.deskport` disponible dans Nixpkgs. L'entrée de l'hôte, les autorisations de capture et la configuration
du pare-feu sont distinctes de l'installation de l'application ; voir le
[guide d'installation Linux](../LINUX_PACKAGES.md#nixos-and-nix).

## Comparaison avec des produits similaires

Il s'agit d'une comparaison des flux de travail et des plateformes, vérifiée par rapport à la documentation officielle
liée le **2026-09-20**, et non d'un banc d'essai de latence ou de qualité d'image. Les fonctionnalités
des produits et les limites des plateformes peuvent changer.

| Aspect | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Flux de travail principal | Espace de travail de bureau distant persistant, dimensionnement adaptatif et passage d'un appareil à l'autre | Streaming de jeux/bureau via des applications client et hôte séparées | Contrôle à distance général et assistance | Bureau à distance interactif et collaboration |
| Plateformes hôtes de bureau | macOS Apple Silicon, Linux x86_64 et Windows x64 | Windows, macOS, Linux et FreeBSD, selon les limites des plateformes | Windows, macOS et Linux | Windows et macOS ; Linux ne peut pas héberger |
| Clients de bureau | macOS Apple Silicon, Linux x86_64 et Windows x64 | Windows, macOS et Linux, entre autres | Windows, macOS et Linux | Windows, macOS, Linux et navigateurs Chromium pris en charge |
| Clients mobiles | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS et Android | iOS/iPadOS et Android ; iOS ne peut pas héberger | Android ; pas de client iOS/iPadOS |
| Modèle de configuration | Hôte intégré aux paquets de bureau natifs ; approbation des appareils dans DeskPort | Installer et configurer Sunshine séparément, puis appairer Moonlight | Infrastructure de serveurs publique ou serveur auto-hébergé | Compte et application Parsec |
| Modèle réseau/service | Accessibilité LAN/VPN à fournir vous-même ; pas de relais DeskPort | Streaming auto-hébergé ; configurer l'accessibilité réseau | Infrastructure publique ou serveur OSS/Pro auto-hébergé | Infrastructure de compte/service Parsec |
| Code source et distribution | Bureau sous GPL, paquets de bureau gratuits ; application Apple payante distribuée séparément | Clients et hôte open source | Client open source et serveur OSS ; option commerciale de serveur Pro | Application/service propriétaire |

Sources : [Moonlight](https://moonlight-stream.org/),
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk clients](https://rustdesk.com/docs/en/client/),
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/),
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
Les limites de plateforme actuelles de DeskPort sont indiquées ci-dessus ; ce tableau n'implique ni
une parité de fonctionnalités sur tous les systèmes, ni un avantage de performance mesuré.

Voir les [notes de version 0.7.3](../releases/RELEASE_0.7.3.md),
l'[architecture](../ARCHITECTURE.md) et la [feuille de route](../ROADMAP.md).

## Compiler et exécuter sous Linux

Avec Nix et les flakes activés :

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

Les dépendances sources tierces du visualiseur sont intégrées au dépôt (vendored) ; le cœur partagé et
les entrées de build Nix restent des dépendances externes épinglées. Les builds natifs doivent initialiser
`shared/deskport-core`. Voir [docs/VENDORED.md](../VENDORED.md).
Pour la configuration SSH, la gestion locale de l'hôte, les prérequis VPS et Omarchy/Hyprland,
voir le [guide CLI](../CLI.md). `deskport doctor` vérifie les prérequis d'hébergement ;
`deskport status --json` inspecte l'instance en cours sans ouvrir de fenêtre.
`nix run . -- --help` liste l'interface en ligne de commande. Lancez le partage sur l'hôte et liez les appareils avant de vous connecter. L'appairage
historique par code PIN Sunshine est également disponible. Aucun identifiant personnel d'hôte ou d'appairage
n'est inclus ni importé depuis Moonlight.
Les nouvelles adresses manuelles utilisent par défaut le port DeskPort `48989`. Indiquez le port affiché
sur la page de partage de l'hôte s'il est différent, ou utilisez `host:47989` pour vous connecter explicitement
à une installation Sunshine autonome par défaut. Les points de terminaison enregistrés/découverts conservent
leurs propres ports.

Pour un build natif modifiable :

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

Les noms de fichiers du projet upstream restent inchangés pour que le fork reste facile à relire.

## Qu'est-ce qui diffère de Moonlight ?

- Hôte Sunshine intégré avec approbation des appareils, reprise de session et réglages par appareil.
- Les sessions restent connectées lorsque le visualiseur est masqué ; rappelez-le depuis la zone de notification/barre des menus.
- Accès par navigateur en HTTPS servi par les hôtes de bureau.
- Exécutable `deskport` distinct, espace de noms de réglages Qt `DeskPort` et
  identifiant d'application Linux `io.github.keithxc.DeskPort`.
- Streaming fenêtré et contrôle par pointeur absolu par défaut.
- Coupure du son en cas de perte du focus ; optimisation pour les jeux, souris à la manette, mode multi-manettes,
  entrée manette en arrière-plan et présence Discord désactivés par défaut.
- Pas d'invites de mise à jour de Moonlight upstream pour cette application indépendante.
- Un environnement Nix verrouillé et un flux de build Linux.

L'interface de bureau propose des pages Appareils, Partage et Réglages, avec sélection de la langue
et des autorisations d'hôte distinctes. Fermer le visualiseur garde sa session disponible et ouvre la liste des appareils.
Un appareil actif propose Retour au bureau ; les autres appareils affichent leurs détails jusqu'à ce que la
session en cours soit déconnectée. Épinglez les appareils fréquemment utilisés et choisissez une liste compacte
ou des cartes. L'apparence suit le système, avec possibilité de forcer le mode clair ou sombre.

## Portée des plateformes

Voir le [tableau des plateformes](#plateformes-prises-en-charge) ci-dessus pour l'état de chaque
plateforme. Les versions de bureau couvrent macOS Apple Silicon, Linux x86_64 et Windows x64
à la fois comme visualiseur et comme hôte ; les applications mobiles sont uniquement des clients.

La prise en charge des plateformes clientes et celle des hôtes sont distinctes : un hôte Mac ne nécessite pas
de client DeskPort pour Mac. Le décodage matériel, l'entrée en direct, la qualité d'image et la latence de rappel nécessitent
des tests en session réelle ; une compilation réussie ne les établit pas.

## Prochain jalon

Garder une session connectée pendant **50 cycles masquer/afficher**, afficher la fenêtre sur
l'espace de travail actuel et rendre l'entrée locale de manière fiable. Mesurer la latence des nouvelles images
et la consommation de ressources en arrière-plan séparément de l'apparition de la fenêtre.

Voir [la feuille de route](../ROADMAP.md) pour les critères d'acceptation et les fonctionnalités reportées,
et les [notes upstream](../UPSTREAM.md) pour la provenance et les limites de maintenance.

## Validation

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

Le test de fumée utilise des répertoires temporaires de configuration/cache XDG et une plateforme Qt
hors écran. Il ne s'appaire avec aucun hôte, ne lance pas de flux et n'injecte aucune entrée.

## Commandes de bureau au quotidien

Fermer une fenêtre laisse DeskPort tourner dans la zone de notification/barre des menus. Utilisez **Open DeskPort**
pour le rappeler, **Disconnect viewer** pour mettre fin uniquement à la connexion en cours, ou
**Quit DeskPort** pour quitter le service. Le partage local continue lorsqu'un visualiseur est fermé.

Le partage du presse-papiers en texte brut et la capture des raccourcis système sont activés par défaut sur
les deux appareils liés. Les changements de réglages s'appliquent après reconnexion. Seules les nouvelles copies sont
partagées, jusqu'à 1 Mio. Les images et fichiers pris en charge sont récupérés à la demande entre
pairs de bureau compatibles ayant activé l'option ; voir les [limites](../CLIPBOARD.md). En mode pointeur
de bureau, le routage du clavier suit le pointeur dans la vidéo ayant le focus.
**Ctrl+Alt+Shift+Z** libère l'entrée ;
**Ctrl+Alt+Shift+Q** déconnecte le visualiseur. Cliquez à l'intérieur pour reprendre l'entrée après
une libération explicite. Les raccourcis réservés par le système dépendent du compositeur du bureau.

Le démarrage à l'ouverture de session et la récupération nécessitent une session graphique active. Voir
[vérifications d'acceptation et limites](../INPUT_SERVICE_ACCEPTANCE.md) avant de compter
sur un ordinateur pour un accès sans surveillance.

## Licence et attribution

DeskPort est un dérivé indépendant de [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
initialement basé sur la v6.1.0. Ce n'est pas une version officielle de Moonlight ou de Sunshine.
Moonlight fournit la base du streaming ; [Sunshine](https://github.com/LizardByte/Sunshine)
est intégré aux paquets hôtes macOS, Windows et Linux (hors Flatpak, limité à l'affichage) et fourni par le paquet Nix Linux.
Les services Sunshine installés séparément restent indépendants.

GPL-3.0-or-later ; voir [LICENSE](../../LICENSE), les mentions de source conservées et la
licence de chaque dépendance intégrée listée dans [docs/VENDORED.md](../VENDORED.md). La documentation d'origine est conservée dans
[README.upstream.md](README.upstream.md).

### Diagnostic et retours

Les journaux de diagnostic sont activés par défaut. Reproduisez le problème,
puis créez un ZIP des journaux et ouvrez un brouillon d'issue GitHub. Vérifiez le ZIP avant de le joindre :
les issues GitHub sont publiques, et DeskPort ne téléverse ni ne soumet rien à votre
place. Voir [données de diagnostic et limites](../DIAGNOSTICS.md).
