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
  <a href="README.pt-BR.md">Português (Brasil)</a> ·
  <a href="README.ru.md">Русский</a> ·
  <a href="README.it.md">Italiano</a> ·
  <b>Nederlands</b> ·
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
  Een werkruimte voor extern bureaublad, gebouwd op Moonlight en Sunshine.<br>
  Houd je externe bureaublad klaar op de achtergrond, haal het met één handeling naar je
  huidige werkruimte en berg het weer op zonder opnieuw te verbinden.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3_stable-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Belangrijkste functies

- **Eén desktop-app om te bekijken en te delen.** Ingebouwde Sunshine, apparaatgoedkeuring,
  eigen inloggegevens en instellingen; geen aparte Sunshine-installatie voor de hostpakketten
  voor macOS, Windows en Linux (niet de Flatpak, die alleen kan bekijken).
- **Een werkruimte die je client volgt.** Adaptieve grootte op compatibele macOS- en KDE-hosts,
  HiDPI, staande/liggende mobiele indelingen en grootte-afstemming per apparaat.
  Formaatwijzigingen verbinden de video kort opnieuw; het zijn geen naadloze encoderwissels.
- **Kies het beeldschermbeleid van de host.** Virtueel primair scherm met gespiegelde schermen,
  virtueel primair scherm met andere schermen uitgeschakeld, of een uitgebreide werkruimte op geschikte
  hosts. DeskPort herstelt de opgeslagen indeling wanneer de sessie eindigt.
- **Wissel tussen geautoriseerde apparaten.** Bevestig voordat je een actieve sessie overneemt;
  de verdrongen client krijgt een geauthenticeerde uitleg over de overname.
- **Houd je desktopwerk binnen handbereik.** Verberg en roep een verbonden viewer terug, geef
  externe invoer vrij wanneer die verborgen is en verbreek het bekijken los van lokaal delen.
- **Instellingen per computer.** Beeld, audio, invoer, desktopafstemming en adresbewerking blijven
  bij het apparaat; kaarten, vastgezette items, thema's en gelokaliseerde interfaces.
- **Deel klembordinhoud op verzoek.** Tekst wordt meteen bijgewerkt; ondersteunde afbeeldingen en
  bestanden worden op verzoek opgehaald tussen gekoppelde desktop-peers die hiervoor kozen.
  Zie [reikwijdte en grenzen van het klembord](../CLIPBOARD.md).
- **Native mobiele bediening.** iPhone/iPad en de Android-client bieden aanraak-/trackpadinvoer,
  toetsenbordbediening en directe toegang tot het externe bureaublad.
- **Optioneel starten bij aanmelden en herstel op macOS.** Dit vereist een actieve grafische
  aanmelding en systeemgoedkeuring; FileVault wordt niet ontgrendeld en er wordt niet voor je aangemeld.

## Voorbeeld van de interface

Schermafbeeldingen van de huidige interface met synthetische demo-apparaten. Klik op een paneel voor
de volledige afbeelding; deze voorbeelden zeggen niets over beschikbaarheid in winkels of releases.

| Apparaten en groepen | Scherminstellingen per apparaat | Ingebouwde handleiding |
| --- | --- | --- |
| [![Apparaten en groepen](../media/devices.png)](../media/devices.png) | [![Scherminstellingen per apparaat](../media/device-settings.png)](../media/device-settings.png) | [![Ingebouwde handleiding](../media/manual.png)](../media/manual.png) |

## Ondersteunde platforms

Zie de [pc-implementatie- en acceptatiematrix](../PC_SUPPORT_MATRIX.md) voor de huidige
reikwijdte van broncode tegenover geïnstalleerde pakketten, compositormogelijkheden, historisch bewijs
en resterende native controles. De releasebeschikbaarheid hieronder certificeert niet elke functie op
elke GPU of OS-sessie. De 0.7.3-release legt exacte pakketidentiteiten en validatiegrenzen vast in het
verificatierapport.

| Platform | Rol | Releasestatus | Download |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Viewer + host + native virtueel scherm | **0.7.3 stabiel**; ondertekend met Developer ID en genotariseerd door Apple | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Viewer + host; adaptieve werkruimte op geschikte KWin-, Mutter- en Hyprland-hosts | **0.7.3 stabiel**: DEB, RPM, Arch, AppImage, draagbaar archief en Nix | [Release](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [installatiegids](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Alleen viewer | **0.7.3 stabiel**, Freedesktop Platform 25.08 | [Release](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Alleen client | App Store; nieuwere builds apart getest in TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Alleen client | **2.7 (63)** · Gesloten test op Google Play loopt | [APK downloaden](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Viewer + host + virtueel scherm | **0.7.3 stabiel**; volledig installatieprogramma kan hosten, draagbare ZIP alleen bekijken; applicatie en installatieprogramma niet ondertekend | [Installatieprogramma](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | Niet gekwalificeerd | Geen geverifieerd releasepakket | — |

> **Antivirusmelding voor Windows:** antivirussoftware kan de Windows-hulpcomponenten van DeskPort ten onrechte markeren.
> Wij zijn dit in onze eigen tests niet tegengekomen. Laat Defender ingeschakeld; verschijnt er een waarschuwing,
> bekijk dan het verificatierapport van de release en de [voorbeeldgeschiedenis](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Native Linux-pakketten vereisen glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
actuele Arch. De AppImage bevat een eigen runtime voor desktops met een oudere glibc;
hardwarestuurprogramma's en compositorondersteuning bepalen nog steeds de hostmogelijkheden. Hosten met
een virtueel scherm op KDE vereist compositor-machtigingen. Native/Nix-pakketten leveren vaste vermeldingen;
AppImage- en draagbare builds registreren het huidige pad van de schermhulp automatisch wanneer het
delen start. Hostinvoer vereist nog steeds apparaatmachtigingen. Mobiele apps kunnen geen desktops hosten.
Een brondefinitie of geslaagde compilatie alleen maakt een platform niet ondersteund.

## Installatie

**macOS:** open de [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
sleep DeskPort naar Programma's en open het. Geef machtigingen voor Schermopname en
Toegankelijkheid om hostfuncties te gebruiken. Nix en Homebrew zijn niet nodig.

**Linux:** kies het [0.7.3-pakket](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
voor je systeem en volg de [stappen per distributie hieronder](#linux-installatie-per-distributie),
inclusief de [vereisten voor hostinvoer](../LINUX_PACKAGES.md#first-connection-and-hosting).
Met Nix: `nix run github:keithxc/deskport/v0.7.3`.
De Flatpak biedt alleen bekijken. De release bevat [SHA-256-controlesommen](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) en een [verificatierapport](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · Tot de gesloten test op Google Play is afgerond, is de [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) tijdelijk beschikbaar op GitHub. De broncode van de mobiele apps is gesloten. Hier wordt alleen het installatiebestand aangeboden.

Build 63 gebruikt dezelfde ondertekeningssleutel als Google Play. Bij een update naar een nieuwere Play-versie blijven gekoppelde computers en instellingen behouden. Heb je build 62 van GitHub geïnstalleerd, verwijder deze dan voordat je build 63 installeert. Je lokale instellingen worden gewist en je moet je computers opnieuw koppelen.

**iPhone / iPad:** installeer [DeskPort uit de App Store](https://apps.apple.com/us/app/deskport/id6812389978).
De mobiele app is een eenmalige betaalde aankoop; zie je winkel voor de actuele lokale prijs.
Desktopreleases blijven gratis en open source. Aankopen ondersteunen ondertekening,
testhardware en verdere ontwikkeling.

Start Delen op de host, selecteer hem op de client en keur het apparaat goed op de host.
Beide apparaten moeten elkaar via het netwerk kunnen bereiken: gebruik een LAN of je eigen VPN,
zoals Tailscale. DeskPort biedt geen accountdienst, gehoste desktops, relay of netwerktunnel.
Klassiek koppelen via Sunshine-pincode blijft beschikbaar.

**0.7.3 stabiel:** deze desktopupdate brengt een gedeelde UI-catalogus in 22 talen samen met
extra klassieke desktopvertalingen, taalwissel in de browser, delen in aangemelde Windows-sessies
zonder herhaalde UAC, systeemschaling voor virtuele schermen, reparaties aan de Linux-runtime,
beveiligde Hyprland/X11-werkruimten, terugvaloplossingen voor het Wayland-klembord, optionele lokale
IME en vierkante apparaatkaarten. Zie de [releasenotes van 0.7.3](../releases/RELEASE_0.7.3.md) voor de exacte
reikwijdte en resterende controles. Controles op de exacte pakketten omvatten het distributievertrouwen
op macOS, Windows-payloads, de native CLI en beschermde antivirusscans, en Linux-runtime/pakketscenario's.
Eerdere native en integratieresultaten behouden hun oorspronkelijke bronidentiteit.

**0.7.2 stabiel (historische verificatie):** de [desktoprelease](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
combineert verbeteringen aan formaatwijziging, extern streamen, databesparing en Linux-runtime.
De 21 assets omvatten pakketten, bijbehorende broncode, materiaal voor opnieuw linken en
verificatiegegevens. Zie de [releasenotes van 0.7.2](../releases/RELEASE_0.7.2.md).
De uiteindelijke AppImage doorstond een controle in een schone Bazzite KDE Wayland-VM met normale
FUSE-start, overeenkomende viewer/helper/herstel-runtimes en geverifieerde opruiming; acceptatie op
fysieke GPU en met echt streamen blijft apart.

Windows Setup doorstond een upgrade vanaf 0.7.1 met controles op behoud van configuratie en koppelingen.
Lokale GUI-controles zonder netwerk omvatten starten via snelkoppeling, Instellingen/Over, sluiten naar
het systeemvak, terugroepen en Afsluiten. Verwijderen en geplande acceptatie van echt streamen/invoer zijn
nog niet getest. Een focus-race in de automatisering veroorzaakte een onbedoelde verbinding met een al
gekoppelde host; latere controles bevestigden nul actieve sessies, geen gereserveerde lease en een hersteld
oorspronkelijk schermindeling. Het incident is geen streamingacceptatie, en diagnostiek kan niet vaststellen
dat er geen externe invoer is verzonden. Zie de releasenotes voor de exacte reikwijdte.

**Eerdere kandidaten:** [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
introduceerde snellere herverbinding bij formaatwijziging, vervaagde overgangen en soepeler slepen op macOS;
[0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
voegde de Bazzite AppImage-fixes toe. Hun [rc.1-notes](../releases/RELEASE_0.7.2-rc.1.md)
en [rc.2-notes](../releases/RELEASE_0.7.2-rc.2.md) behouden de eerdere validatiereikwijdte.

**Browsertoegang:** desktophosts bieden een HTTPS-extern bureaublad met een aanmeldcode van
30 seconden, optionele browserkoppeling voor zeven dagen, adaptieve grootte en volledig scherm.
Zie de [releasenotes van 0.7.1](../releases/RELEASE_0.7.1.md) en de [browsergids](../BROWSER_ACCESS.md).
De AppImage bevat een eigen glibc-runtime; native pakketten behouden hun vereiste van glibc 2.39.

### Linux-installatie per distributie

Deze instructies zijn bedoeld voor **x86_64**-desktopsystemen en de downloads van release **0.7.3**.
Native pakketten vereisen glibc 2.39 of nieuwer. Het zijn upstream-downloads, geen pakketten uit een
DeskPort-distributierepository; installeer toekomstige releases door het nieuwe pakket opnieuw te
downloaden en te verifiëren.

De 0.7.3-AppImage behoudt de fixes voor draagbare runtime, schermherstel, procesafsluiting,
machtigingen en starten bij aanmelden voor Bazzite/KDE Wayland
[issue #5](https://github.com/keithxc/deskport/issues/5), inclusief de kleine backport van Qt
Wayland 6.4.2. Die backport geldt alleen voor de AppImage; native pakketten en de Flatpak behouden hun
basis-Qt-runtime. De uiteindelijke **0.7.3**-AppImage doorstond normale FUSE-start en helper/runtime-controles
in een schone Bazzite KDE Wayland-VM met SELinux Enforcing en softwarematige graphics. Dit verifieert geen
fysieke GPU of echt streamen; zie de [verificatiereikwijdte van 0.7.3](../releases/RELEASE_0.7.3.md#validation-boundary).

Resultaten van installatie, upgrade, terugdraaien, verwijderen en behoud van configuratie in volledige
VM's staan in de [levenscyclusmatrix voor desktops](../LINUX_DESKTOP_ACCEPTANCE.md).
Een GUI-smoketest van de AppImage verifieert geen bewerkingen met native pakketten.
Pakketinstallatie en het starten van de GUI alleen verifiëren geen live-opname, hardwarematige
videodecodering, externe invoer, beeldkwaliteit of latentie.

De bewaarde Ubuntu 26.04 GNOME-VM reproduceert een crash bij het starten in de oudere eigen
VA-API Wayland-bibliotheek van de gepubliceerde 0.7.2-DEB. De uiteindelijke 0.7.3-DEB gebruikt de
vastgezette libva 2.23.0 in beide eigen runtime-bomen en isoleert systeemhulpmiddelen van
bibliotheek-overrides van de host. Hij doorstond GUI/API- en upgradecontroles in een schone overlay van
die VM. Synthetische configuratie bleef intact bij terugdraaien en verwijderen; de crash van de oude versie
werd vóór de upgrade en na het terugdraaien gereproduceerd. Bestaande release-assets behouden hun
oorspronkelijke hashes. Het starten van GNOME-opname en herhaald aanmaken van virtuele uitvoer blijven
aparte acceptatiehiaten. KWin 6.3 van Debian 13 blijft bovendien onder het minimum van 6.6 voor de
schermhulp van de host.

| Distributie / desktop | Downloadroute |
| --- | --- |
| Ubuntu 26.04 GNOME | 0.7.3-DEB of AppImage; DEB-startregressie geslaagd, hiaten in GNOME-opname/herstel blijven |
| Debian 13 KDE | DEB, geïnstalleerd met APT |
| Fedora 44 KDE | RPM, geïnstalleerd met DNF |
| Arch Linux KDE | Lokaal Arch-pakket, geïnstalleerd met `pacman -U` |
| Bazzite KDE | 0.7.3-AppImage; VM-controles op het exacte pakket hierboven |
| Omarchy / Hyprland | Lokaal Arch-pakket, geïnstalleerd met `pacman -U` |

Open een terminal in je normale desktopsessie. Elke opdrachtreeks controleert de exacte gedownloade
bestandsnaam tegen het `SHA256SUMS.txt` van die release voordat er iets wordt geïnstalleerd; de controle
moet die bestandsnaam als `OK` melden. Een ontbrekende vermelding of afwijking stopt de reeks.
Start DeskPort na de installatie als je gewone gebruiker.
Stop vóór een upgrade het delen en sluit de oude DeskPort-instantie volledig af met de actie
**Afsluiten**, bijvoorbeeld via het systeemvakmenu. Het venster sluiten verbergt het mogelijk alleen.
Start daarna de nieuw geïnstalleerde versie zodat het nieuwe programma draait.

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — downloaden, installeren, bijwerken en verwijderen</summary>

Download [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb)
en open een terminal in de map met dat bestand:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT installeert afhankelijkheden uit je geconfigureerde Ubuntu-repository's. De voorloop `./`
selecteert het gedownloade bestand. Open DeskPort later via het toepassingsmenu. Download voor een
upgrade de volgende compatibele DEB, verifieer de controlesom en herhaal
`sudo apt install ./NEW_FILENAME.deb` met de werkelijke bestandsnaam.
Verwijder het pakket met `sudo apt remove deskport`.

Ubuntu 24.04 voldoet ook aan het glibc-minimum van het native pakket. Ubuntu 22.04 heeft de AppImage,
de Flatpak (alleen bekijken) of een apart ondersteunde Nix-/bronbuild nodig. Hosten op GNOME hangt af
van de geïnstalleerde Mutter-/opnamemogelijkheden; zie de
[vereisten voor adaptief scherm](../LINUX_ADAPTIVE_DISPLAY.md).

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — downloaden, installeren, bijwerken en verwijderen</summary>

Download [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb)
en open een terminal in de downloadmap:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT lost de afhankelijkheden van het pakket op. Open DeskPort later via het toepassingsmenu.
Upgrade door de volgende compatibele DEB te downloaden en te verifiëren en die te installeren met
`sudo apt install ./NEW_FILENAME.deb`. Verwijder met `sudo apt remove deskport`. Debian 12 voldoet niet
aan het glibc-minimum van het native pakket; gebruik de AppImage, de Flatpak (alleen bekijken) of een
ondersteunde Nix-/bronbuild.

Controleer voor hosten op KDE de [compositor- en invoervereisten](../LINUX_PACKAGES.md#first-connection-and-hosting)
voordat je gaat delen.

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — downloaden, installeren, bijwerken en verwijderen</summary>

Download [deskport-0.7.3-1.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1.x86_64.rpm)
en open een terminal in de downloadmap:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo dnf install ./deskport-0.7.3-1.x86_64.rpm &&
deskport
```

DNF lost afhankelijkheden op uit ingeschakelde Fedora-repository's. Open DeskPort later via het
toepassingsmenu. Upgrade door de volgende compatibele RPM te downloaden en te verifiëren en die te
installeren met `sudo dnf install ./NEW_FILENAME.rpm`. Verwijder met `sudo dnf remove deskport`.
Deze route is voor gewone Fedora; gebruik voor Bazzite de AppImage-instructies.
Controleer voor hosten op KDE ook de [compositor- en invoervereisten](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — downloaden, installeren, bijwerken en verwijderen</summary>

Voer je gebruikelijke volledige Arch-systeemupdate uit en download dan
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Open een terminal in de downloadmap:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` installeert het lokale pakket en lost afhankelijkheden op uit de geconfigureerde
repository's. Open DeskPort later via het toepassingsmenu. Installeer later gedownloade pakketten met
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` nadat je de controlesommen hebt geverifieerd. Verwijder met
`sudo pacman -R deskport`. Deze download voegt DeskPort niet toe aan de officiële repository's of AUR.
Controleer voor hosten op KDE ook de [compositor- en invoervereisten](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, starten en verwijderen</summary>

Download
[DeskPort-0.7.3-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage).
Stop het delen en gebruik **Afsluiten** om een oudere DeskPort-instantie volledig af te sluiten; het
venster sluiten kan hem laten doordraaien. Bewaar de nieuwe AppImage op een vaste locatie voordat je
starten bij aanmelden inschakelt:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.3-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.3-x86_64.AppImage &&
./DeskPort-0.7.3-x86_64.AppImage
```

Start normaal; meegeleverde bibliotheken hoeven niet verwijderd of verplaatst te worden. Als starten bij
aanmelden naar een oudere AppImage verwees, open dan het nieuwe bestand op zijn definitieve pad en vernieuw
starten bij aanmelden. Wil je de AppImage op een andere vermelde x86_64-distributie gebruiken, volg dan
deze stappen in plaats van het native pakket te installeren.

De [AppImage-gids](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/) van Bazzite
beschrijft Gear Lever voor integratie in het toepassingsmenu. Als een integratiehulpmiddel het bestand
verplaatst, vernieuw dan daarna het starten van DeskPort vanaf de definitieve locatie.
Upgrades vereisen het downloaden en verifiëren van de nieuwe AppImage. Om te verwijderen: schakel
starten bij aanmelden uit, sluit DeskPort af, verwijder het bestand en verwijder eventuele menu-items via
het hulpmiddel dat ze heeft gemaakt. Voer de AppImage uit op de Bazzite-host om het bureaublad te delen;
een installatie in Distrobox geeft geen integratie met het hostbureaublad. De Flatpak hieronder biedt
alleen bekijken.

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — lokaal Arch-pakket en hostvereisten</summary>

Voer de normale systeemupdate van Omarchy uit en download dan
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Open een terminal in de downloadmap:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

Gebruik de toepassingsstarter voor latere starts. Upgrade door het volgende lokale Arch-pakket te
downloaden en te verifiëren en het te installeren met
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst`. Verwijder met `sudo pacman -R deskport`.
Het repository-/AUR-pakketmenu van Omarchy maakt van deze GitHub-download geen repositorypakket.

Hosten op Hyprland gebruikt een speciale headless-uitvoer. De 0.7.3-broncode ondersteunt het beleid
spiegelen, andere uitschakelen en uitbreiden wanneer monitoridentiteit en runtimestatus veilig hersteld
kunnen worden; zie [Hyprland-schermleases](../HYPRLAND_DISPLAY.md).
Volg de [Omarchy/Hyprland-hostgids](../CLI.md#omarchy-and-hyprland)
voor de vereisten voor actieve compositor, opnamemachtiging en SSH-dienst.
Controleer de geïnstalleerde Hyprland-versie voordat je machtigingssyntaxis toepast.
Pakketinstallatie alleen configureert geen onbeheerde desktopaanmelding en verifieert geen werkende stream.

</details>

<details>
<summary><strong>Flatpak-alternatief</strong> — alleen bekijken, op systemen met Flatpak</summary>

Download [DeskPort-0.7.3-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-client-x86_64.flatpak)
en open een terminal in de downloadmap:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user ./DeskPort-0.7.3-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

De lokale bundel kan de runtime Freedesktop Platform 25.08 downloaden. Hij biedt alleen bekijken,
zonder Sunshine-hosting of integratie met de aanmelddienst van de desktop.
Installeer later gedownloade bundels met `flatpak install --user --or-update ./NEW_FILENAME.flatpak`
nadat je de controlesommen hebt geverifieerd. Verwijder met
`flatpak uninstall --user io.github.keithxc.DeskPort`. Dit is een bundel uit de GitHub-release;
de installatie vereist geen DeskPort-vermelding op Flathub.

</details>

Gebruik voor hosten de normale aangemelde grafische sessie en configureer de
[opname- en invoervereisten](../LINUX_PACKAGES.md#first-connection-and-hosting).
Test het delen met een client op je beoogde LAN of VPN voordat je optioneel starten bij aanmelden
inschakelt. Pakketinstallatie configureert geen netwerkbereikbaarheid en geeft geen toegang tot hostinvoer.
Stop het delen en sluit DeskPort af voordat je het pakket verwijdert; schakel starten bij aanmelden uit
als je dat had ingeschakeld. Bij het verwijderen van het pakket blijven de DeskPort-instellingen in je
thuismap behouden.

### Beschikbaarheid in Linux-repository's

| Kanaal | Status | Installatie |
| --- | --- | --- |
| GitHub-releasepakketten | 0.7.3 stabiel | Download het pakket voor je distributie hierboven |
| Upstream Nix-flake | 0.7.3 stabiel | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Recept getest; publicatie geblokkeerd door de pauze in registratie van nieuwe accounts | [Bouw het recept lokaal](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) ingediend ter beoordeling; niet samengevoegd | Gebruik de upstream-flake tot opname |
| Officiële Arch-repository's / Flathub | Niet vermeld | Gebruik de releasedownloads; de Flatpak kan alleen bekijken |

Het downloadbare Arch-pakket wordt geïnstalleerd met `pacman -U`; het maakt
`pacman -S deskport` niet beschikbaar. Evenzo maakt de upstream-flake `pkgs.deskport` niet beschikbaar
in Nixpkgs. Hostinvoer, opnamemachtigingen en firewallinstellingen staan los van het installeren van de
applicatie; zie de [Linux-installatiegids](../LINUX_PACKAGES.md#nixos-and-nix).

## Vergelijking met vergelijkbare producten

Dit is een vergelijking van werkwijze en platforms, gecontroleerd aan de hand van de gelinkte officiële
documentatie op **2026-09-20**, geen benchmark van latentie of beeldkwaliteit. Productfuncties en
platformgrenzen kunnen veranderen.

| Aspect | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Hoofdwerkwijze | Blijvende externe bureaubladwerkruimte, adaptieve grootte en apparaatoverdracht | Game-/desktopstreaming via aparte client- en host-apps | Algemene externe bediening en ondersteuning | Interactief extern bureaublad en samenwerking |
| Desktophostplatforms | macOS Apple Silicon, Linux x86_64 en Windows x64 | Windows, macOS, Linux en FreeBSD, met platformgrenzen | Windows, macOS en Linux | Windows en macOS; Linux kan niet hosten |
| Desktopclients | macOS Apple Silicon, Linux x86_64 en Windows x64 | Onder andere Windows, macOS en Linux | Windows, macOS en Linux | Windows, macOS, Linux en ondersteunde Chromium-browsers |
| Mobiele clients | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS en Android | iOS/iPadOS en Android; iOS kan niet hosten | Android; geen iOS/iPadOS-client |
| Installatiemodel | Ingebouwde host in native desktoppakketten; apparaten goedkeuren in DeskPort | Sunshine apart installeren en configureren, daarna Moonlight koppelen | Openbare serverinfrastructuur of een zelfgehoste server | Parsec-account en -applicatie |
| Netwerk-/dienstmodel | Eigen LAN/VPN-bereikbaarheid; geen DeskPort-relay | Zelfgehoste streaming; netwerkbereikbaarheid zelf instellen | Openbare infrastructuur of zelfgehoste OSS/Pro-server | Parsec-account-/dienstinfrastructuur |
| Broncode en distributie | GPL-desktop, gratis desktoppakketten; betaalde Apple-app apart verspreid | Open-source clients en host | Open-source client en OSS-server; commerciële Pro-serveroptie | Propriëtaire applicatie/dienst |

Bronnen: [Moonlight](https://moonlight-stream.org/),
[installatie en platformondersteuning van Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk-clients](https://rustdesk.com/docs/en/client/),
[RustDesk zelf hosten](https://rustdesk.com/docs/en/self-host/),
[platformcompatibiliteit van Parsec](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec op Linux instellen](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
De huidige platformgrenzen van DeskPort staan hierboven; deze tabel impliceert geen gelijke functies op
elk besturingssysteem of een gemeten prestatievoordeel.

Zie de [releasenotes van 0.7.3](../releases/RELEASE_0.7.3.md),
de [architectuur](../ARCHITECTURE.md) en de [roadmap](../ROADMAP.md).

## Bouwen en uitvoeren op Linux

Met Nix en flakes ingeschakeld:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

De broncode-afhankelijkheden van derden voor de viewer zijn meegeleverd; de gedeelde core en de
Nix-buildinvoer blijven vastgezette externe afhankelijkheden. Native builds moeten
`shared/deskport-core` initialiseren. Zie [docs/VENDORED.md](../VENDORED.md).
Voor SSH-configuratie, lokaal hostbeheer, VPS-vereisten en Omarchy/Hyprland,
zie de [CLI-gids](../CLI.md). `deskport doctor` controleert de hostvereisten;
`deskport status --json` inspecteert de draaiende instantie zonder een venster te openen.
`nix run . -- --help` toont de opdrachtregelinterface. Start Delen op de host en koppel de apparaten
voordat je verbindt. Klassiek koppelen via Sunshine-pincode is ook beschikbaar. Er worden geen
persoonlijke host- of koppelgegevens meegeleverd of uit Moonlight geïmporteerd.
Nieuwe handmatige adressen gebruiken standaard DeskPort-poort `48989`. Voeg de poort toe die op de
deelpagina van de host staat als die afwijkt, of gebruik `host:47989` om expliciet te verbinden met een
standaard zelfstandige Sunshine-installatie. Opgeslagen/gevonden eindpunten behouden hun eigen poorten.

Voor een bewerkbare native build:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

De bestandsnamen van het upstream-project blijven ongewijzigd zodat de fork goed te beoordelen blijft.

## Wat is er anders dan bij Moonlight?

- Ingebouwde Sunshine-host met apparaatgoedkeuring, overname en instellingen per apparaat.
- Sessies blijven verbonden terwijl de viewer verborgen is; roep hem terug via het systeemvak/de menubalk.
- HTTPS-browsertoegang aangeboden door desktophosts.
- Aparte `deskport`-executable, Qt-instellingennaamruimte `DeskPort` en
  Linux-applicatie-ID `io.github.keithxc.DeskPort`.
- Standaard streamen in een venster en bediening met absolute aanwijzer.
- Dempen bij focusverlies; game-optimalisatie, gamepadmuis, multicontrollermodus,
  gamepadinvoer op de achtergrond en Discord-aanwezigheid standaard uitgeschakeld.
- Geen upstream-updatemeldingen van Moonlight voor deze onafhankelijke applicatie.
- Een vastgezette Nix-omgeving en een Linux-buildworkflow.

De desktopinterface biedt pagina's voor apparaten, delen en instellingen, met taalkeuze en aparte
hostmachtigingen. Het sluiten van de viewer houdt de sessie beschikbaar en opent de apparatenlijst.
Een actief apparaat biedt Terug naar bureaublad; andere apparaten tonen details tot de huidige sessie
is verbroken. Zet veelgebruikte apparaten vast en kies een compacte lijst of kaarten. Het uiterlijk volgt
het systeem, met licht en donker als overschrijving.

## Platformreikwijdte

Zie de [platformtabel](#ondersteunde-platforms) hierboven voor de releasestatus per platform.
Desktopreleases dekken macOS Apple Silicon, Linux x86_64 en Windows x64 als viewer en host;
mobiele apps zijn alleen clients.

Client- en hostondersteuning staan los van elkaar: een Mac-host vereist geen DeskPort-client voor Mac.
Hardwaredecodering, live-invoer, beeldkwaliteit en terugroeplatentie vereisen tests in echte sessies;
een geslaagde build stelt die niet vast.

## Volgende mijlpaal

Eén sessie verbonden houden gedurende **50 cycli van verbergen/tonen**, het venster op de huidige
werkruimte tonen en lokale invoer betrouwbaar teruggeven. Latentie van verse frames en resourcegebruik
op de achtergrond los van de weergave van het venster meten.

Zie [de roadmap](../ROADMAP.md) voor acceptatiecriteria en uitgestelde functies,
en de [upstream-notities](../UPSTREAM.md) voor herkomst en onderhoudsgrenzen.

## Validatie

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

De smoketest gebruikt tijdelijke XDG-configuratie-/cachemappen en een offscreen Qt-platform.
Er wordt niet gekoppeld met een host, geen stream gestart en geen invoer geïnjecteerd.

## Dagelijkse desktopbediening

Een venster sluiten houdt DeskPort actief in het systeemvak/de menubalk. Gebruik **DeskPort openen**
om het terug te roepen, **Viewer verbreken** om alleen de huidige verbinding te beëindigen, of
**DeskPort afsluiten** om de dienst te stoppen. Lokaal delen gaat door wanneer een viewer sluit.

Klembord delen in platte tekst en het vastleggen van systeemsneltoetsen zijn standaard ingeschakeld op
beide gekoppelde apparaten. Wijzigingen in instellingen gelden na opnieuw verbinden. Alleen nieuwe
kopieën worden gedeeld, tot 1 MiB. Ondersteunde afbeeldingen en bestanden worden op verzoek opgehaald
tussen compatibele desktop-peers die hiervoor kozen; zie [grenzen](../CLIPBOARD.md). In de
desktopaanwijzermodus volgt de toetsenbordroutering de aanwijzer binnen de gefocuste video.
**Ctrl+Alt+Shift+Z** geeft invoer vrij;
**Ctrl+Alt+Shift+Q** verbreekt de viewer. Klik erin om na expliciete vrijgave weer invoer te krijgen.
Door het OS gereserveerde sneltoetsen hangen af van de desktopcompositor.

Starten bij aanmelden en herstel vereisen een actieve grafische aanmeldsessie. Zie de
[acceptatiecontroles en beperkingen](../INPUT_SERVICE_ACCEPTANCE.md) voordat je op een computer
vertrouwt voor onbeheerde toegang.

## Licentie en naamsvermelding

DeskPort is een onafhankelijke afgeleide van [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
oorspronkelijk gebaseerd op v6.1.0. Het is geen officiële release van Moonlight of Sunshine.
Moonlight levert de streamingbasis; [Sunshine](https://github.com/LizardByte/Sunshine)
is meegeleverd in de hostpakketten voor macOS, Windows en Linux (niet de Flatpak, die alleen kan bekijken)
en wordt geleverd door het Linux-Nix-pakket. Apart geïnstalleerde Sunshine-diensten blijven onafhankelijk.

GPL-3.0-or-later; zie [LICENSE](../../LICENSE), behouden bronvermeldingen en de licentie van elke
meegeleverde afhankelijkheid in [docs/VENDORED.md](../VENDORED.md). De oorspronkelijke documentatie is
bewaard in [README.upstream.md](README.upstream.md).

### Diagnostiek en feedback

Diagnostische logboeken zijn standaard ingeschakeld. Reproduceer het probleem,
maak dan een ZIP met logboeken en open een concept-issue op GitHub. Controleer de ZIP voordat je hem
bijvoegt: GitHub-issues zijn openbaar en DeskPort uploadt of verstuurt niets voor je.
Zie [diagnostische gegevens en grenzen](../DIAGNOSTICS.md).
