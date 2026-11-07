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
  <a href="README.nl.md">Nederlands</a> ·
  <b>Polski</b> ·
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
  Przestrzeń robocza zdalnego pulpitu zbudowana na Moonlight i Sunshine.<br>
  Trzymaj zdalny pulpit w gotowości w tle, przenoś go na bieżącą przestrzeń roboczą
  jednym ruchem i chowaj bez ponownego łączenia.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3_stable-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Główne funkcje

- **Jedna aplikacja do oglądania i udostępniania.** Wbudowany Sunshine, zatwierdzanie urządzeń,
  niezależne poświadczenia i ustawienia; pakiety hosta dla macOS, Windows i Linuksa nie wymagają
  osobnej instalacji Sunshine (nie dotyczy Flatpaka, który służy tylko do oglądania).
- **Przestrzeń robocza dopasowana do klienta.** Adaptacyjny rozmiar na zgodnych hostach macOS i KDE,
  HiDPI, pionowe/poziome układy mobilne i dostrajanie rozmiaru dla każdego urządzenia.
  Zmiana rozmiaru na chwilę ponownie łączy wideo; nie jest to płynna zmiana enkodera.
- **Wybór zasad wyświetlania hosta.** Wirtualny ekran główny z kopiami pozostałych ekranów,
  wirtualny ekran główny z wyłączonymi innymi ekranami lub rozszerzona przestrzeń na obsługiwanych
  hostach. Po zakończeniu sesji DeskPort przywraca zapisany układ.
- **Przechodzenie między autoryzowanymi urządzeniami.** Potwierdzenie przed przejęciem aktywnej sesji;
  wyparty klient otrzymuje uwierzytelnione wyjaśnienie przejęcia.
- **Praca na pulpicie zawsze pod ręką.** Ukrywaj i przywołuj połączoną przeglądarkę, zwalniaj zdalne
  sterowanie po ukryciu i rozłączaj oglądanie niezależnie od lokalnego udostępniania.
- **Ustawienia zapisywane dla każdego komputera.** Obraz, dźwięk, sterowanie, dostrajanie pulpitu i
  edycja adresu pozostają przy urządzeniu; karty, przypięcia, motywy i zlokalizowane interfejsy.
- **Udostępnianie schowka na żądanie.** Tekst aktualizuje się od razu; obsługiwane obrazy i pliki są
  pobierane na żądanie między powiązanymi komputerami, które wyraziły zgodę.
  Zobacz [zakres i ograniczenia schowka](../CLIPBOARD.md).
- **Natywne sterowanie mobilne.** iPhone/iPad oraz klient Android oferują dotyk/gładzik,
  sterowanie klawiaturą i bezpośrednie wejście do zdalnego pulpitu.
- **Opcjonalne uruchamianie po zalogowaniu i odzyskiwanie w macOS.** Wymagają aktywnego logowania
  graficznego i zgody systemu; nie odblokowują FileVault ani nie logują za ciebie.

## Podgląd interfejsu

Zrzuty ekranu bieżącego interfejsu z syntetycznymi urządzeniami demonstracyjnymi. Kliknij panel, aby
zobaczyć pełny obraz; podglądy nie oznaczają dostępności w sklepie ani w wydaniu.

| Urządzenia i grupy | Ustawienia ekranu dla urządzenia | Wbudowana instrukcja |
| --- | --- | --- |
| [![Urządzenia i grupy](../media/devices.png)](../media/devices.png) | [![Ustawienia ekranu dla urządzenia](../media/device-settings.png)](../media/device-settings.png) | [![Wbudowana instrukcja](../media/manual.png)](../media/manual.png) |

## Obsługiwane platformy

Zobacz [macierz implementacji i akceptacji na PC](../PC_SUPPORT_MATRIX.md), gdzie opisano bieżący
zakres kodu źródłowego i zainstalowanych pakietów, możliwości kompozytorów, wcześniejsze wyniki i pozostałe
testy natywne. Dostępność wydania poniżej nie oznacza certyfikacji każdej funkcji na każdym GPU czy sesji
systemu. Wydanie 0.7.3 zapisuje dokładne tożsamości pakietów i granice walidacji w raporcie weryfikacji.

| Platforma | Rola | Stan wydania | Pobieranie |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Przeglądarka + host + natywny ekran wirtualny | **0.7.3 stabilna**; podpisana Developer ID i notaryzowana przez Apple | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Przeglądarka + host; adaptacyjna przestrzeń na obsługiwanych hostach KWin, Mutter i Hyprland | **0.7.3 stabilna**: DEB, RPM, Arch, AppImage, archiwum przenośne i Nix | [Wydanie](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [instrukcja instalacji](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Tylko przeglądarka | **0.7.3 stabilna**, Freedesktop Platform 25.08 | [Wydanie](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Tylko klient | App Store; nowsze kompilacje testowane osobno w TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Tylko klient | **2.7 (63)** · Trwają testy zamknięte w Google Play | [Pobierz APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Przeglądarka + host + ekran wirtualny | **0.7.3 stabilna**; pełny instalator umożliwia hostowanie, przenośny ZIP tylko oglądanie; aplikacja i instalator niepodpisane | [Instalator](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | Niekwalifikowane | Brak zweryfikowanego pakietu wydania | — |

> **Uwaga o antywirusach w Windows:** oprogramowanie antywirusowe może błędnie oznaczyć komponenty pomocnicze DeskPort dla Windows.
> W naszych testach nie spotkaliśmy się z tym. Nie wyłączaj Defendera; jeśli pojawi się ostrzeżenie,
> zajrzyj do raportu weryfikacji wydania i [historii próbek](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Natywne pakiety dla Linuksa wymagają glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
aktualny Arch. AppImage zawiera prywatne środowisko uruchomieniowe dla pulpitów ze starszym glibc;
możliwości hostowania nadal zależą od sterowników sprzętu i obsługi kompozytora. Hostowanie z ekranem
wirtualnym w KDE wymaga wpisów uprawnień kompozytora. Pakiety natywne/Nix dostarczają stałe wpisy;
AppImage i wersje przenośne automatycznie rejestrują bieżącą ścieżkę pomocnika wyświetlania po
rozpoczęciu udostępniania. Sterowanie hostem nadal wymaga uprawnień do urządzeń. Aplikacje mobilne nie
hostują pulpitów.
Sama definicja źródeł lub udana kompilacja nie czyni platformy obsługiwaną.

## Instalacja

**macOS:** otwórz [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
przeciągnij DeskPort do folderu Aplikacje i uruchom. Aby korzystać z funkcji hosta, nadaj uprawnienia
Nagrywanie ekranu i Dostępność. Nix ani Homebrew nie są potrzebne.

**Linux:** wybierz [pakiet 0.7.3](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
dla swojego systemu i wykonaj [kroki dla dystrybucji poniżej](#instalacja-w-linuksie-według-dystrybucji),
w tym [wymagania dotyczące sterowania hostem](../LINUX_PACKAGES.md#first-connection-and-hosting).
Przez Nix: `nix run github:keithxc/deskport/v0.7.3`.
Flatpak służy tylko do oglądania. Wydanie zawiera [sumy kontrolne SHA-256](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) i [raport weryfikacji](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · Do zakończenia testów zamkniętych w Google Play plik [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) jest tymczasowo dostępny na GitHubie. Kod źródłowy aplikacji mobilnych jest zamknięty. Udostępniamy tu tylko instalator.

Kompilacja 63 jest podpisana tym samym kluczem co aplikacja w Google Play. Aktualizacja do nowszej wersji ze sklepu zachowa sparowane komputery i ustawienia. Jeśli masz kompilację 62 z GitHuba, przed instalacją 63 musisz ją odinstalować. Spowoduje to usunięcie ustawień lokalnych i konieczność ponownego sparowania komputerów.

**iPhone / iPad:** zainstaluj [DeskPort z App Store](https://apps.apple.com/us/app/deskport/id6812389978).
Aplikacja mobilna to jednorazowy płatny zakup; aktualną cenę lokalną znajdziesz w swoim sklepie.
Wydania desktopowe pozostają darmowe i otwartoźródłowe. Zakupy wspierają podpisywanie,
sprzęt testowy i dalszy rozwój.

Uruchom udostępnianie na hoście, wybierz go w kliencie i zatwierdź urządzenie na hoście.
Oba urządzenia muszą się widzieć w sieci: użyj sieci LAN lub własnego VPN, np. Tailscale.
DeskPort nie zapewnia usługi kont, hostowanych pulpitów, przekaźnika ani tunelu sieciowego.
Klasyczne parowanie kodem PIN Sunshine nadal jest dostępne.

**0.7.3 stabilna:** ta aktualizacja wprowadza wspólny katalog interfejsu w 22 językach wraz z
dodatkowymi starszymi tłumaczeniami aplikacji desktopowej, przełączanie języka w przeglądarce,
udostępnianie w zalogowanych sesjach Windows bez powtarzanego UAC, systemowe skalowanie ekranu
wirtualnego, naprawy środowiska Linuksa, chronione przestrzenie Hyprland/X11, rozwiązania zapasowe
schowka Wayland, opcjonalny lokalny IME i kwadratowe karty urządzeń. Dokładny zakres i pozostałe testy
opisują [informacje o wydaniu 0.7.3](../releases/RELEASE_0.7.3.md). Testy dokładnych pakietów obejmują zaufanie
dystrybucji w macOS, zawartość pakietów Windows, natywne CLI i chronione skanowanie antywirusowe oraz
scenariusze środowiska/pakietów Linuksa. Wcześniejsze wyniki natywne i integracyjne zachowują pierwotne
tożsamości źródeł.

**0.7.2 stabilna (weryfikacja historyczna):** [wydanie desktopowe](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
łączy ulepszenia zmiany rozmiaru, zdalnego strumieniowania, oszczędzania danych i środowiska Linuksa.
Jego 21 zasobów obejmuje pakiety, odpowiadające im źródła, materiały do ponownego linkowania i zapisy
weryfikacji. Zobacz [informacje o wydaniu 0.7.2](../releases/RELEASE_0.7.2.md).
Końcowy AppImage przeszedł test w świeżej maszynie wirtualnej Bazzite KDE Wayland z normalnym startem
FUSE, zgodnymi środowiskami przeglądarki/pomocnika/odzyskiwania i zweryfikowanym sprzątaniem; akceptacja
na fizycznym GPU i z prawdziwym strumieniowaniem pozostaje osobna.

Instalator Windows przeszedł aktualizację z 0.7.1 z testami zachowania konfiguracji i parowań.
Lokalne testy GUI bez sieci objęły uruchomienie ze skrótu, Ustawienia/O programie, zamykanie do zasobnika,
przywoływanie i Zakończ. Odinstalowanie oraz planowana akceptacja prawdziwego strumieniowania/sterowania
nie zostały przetestowane. Wyścig fokusu w automatyzacji spowodował niezamierzone połączenie z już
sparowanym hostem; późniejsze testy potwierdziły zero aktywnych sesji, brak zarezerwowanej dzierżawy i
przywrócony pierwotny układ ekranów. Incydent nie stanowi akceptacji strumieniowania, a diagnostyka nie
może potwierdzić, że nie przesłano żadnego zdalnego sterowania. Dokładny zakres opisują informacje o wydaniu.

**Wcześniejsze kandydatki:** [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
wprowadziła szybsze ponowne łączenie przy zmianie rozmiaru, rozmyte przejścia i płynniejsze przeciąganie w macOS;
[0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
dodała poprawki AppImage dla Bazzite. Ich [notatki rc.1](../releases/RELEASE_0.7.2-rc.1.md)
i [notatki rc.2](../releases/RELEASE_0.7.2-rc.2.md) zachowują wcześniejszy zakres walidacji.

**Dostęp z przeglądarki:** hosty desktopowe udostępniają zdalny pulpit przez HTTPS z 30-sekundowym
kodem logowania, opcjonalnym parowaniem przeglądarki na siedem dni, adaptacyjnym rozmiarem i trybem
pełnoekranowym. Zobacz [informacje o wydaniu 0.7.1](../releases/RELEASE_0.7.1.md) i
[przewodnik po przeglądarce](../BROWSER_ACCESS.md). AppImage zawiera prywatne środowisko glibc;
pakiety natywne nadal wymagają glibc 2.39.

### Instalacja w Linuksie według dystrybucji

Te instrukcje dotyczą systemów desktopowych **x86_64** i plików wydania **0.7.3**.
Pakiety natywne wymagają glibc 2.39 lub nowszego. Są to pliki pobierane z projektu, a nie pakiety z
repozytorium dystrybucji DeskPort; kolejne wydania instaluj, ponownie pobierając i weryfikując nowy pakiet.

AppImage 0.7.3 zachowuje poprawki przenośnego środowiska, przywracania ekranów, kończenia procesów,
uprawnień i uruchamiania po zalogowaniu dla Bazzite/KDE Wayland
[issue #5](https://github.com/keithxc/deskport/issues/5), w tym mały backport Qt Wayland 6.4.2.
Backport dotyczy tylko AppImage; pakiety natywne i Flatpak zachowują bazowe środowisko Qt. Końcowy
AppImage **0.7.3** przeszedł normalny start FUSE oraz testy pomocnika/środowiska w świeżej maszynie
Bazzite KDE Wayland z SELinux Enforcing i grafiką programową. Nie weryfikuje to fizycznego GPU ani
prawdziwego strumieniowania; zobacz [zakres weryfikacji 0.7.3](../releases/RELEASE_0.7.3.md#validation-boundary).

Wyniki instalacji, aktualizacji, wycofania, usuwania i zachowania konfiguracji w pełnych maszynach
wirtualnych są śledzone w [macierzy cyklu życia na pulpicie](../LINUX_DESKTOP_ACCEPTANCE.md).
Test dymny GUI AppImage nie weryfikuje operacji na pakietach natywnych.
Sama instalacja pakietu i uruchomienie GUI nie weryfikują przechwytywania na żywo, sprzętowego
dekodowania wideo, zdalnego sterowania, jakości obrazu ani opóźnień.

Zachowana maszyna Ubuntu 26.04 GNOME odtwarza awarię przy starcie w starszej prywatnej bibliotece
VA-API Wayland opublikowanego pakietu DEB 0.7.2. Końcowy DEB 0.7.3 używa przypiętego libva 2.23.0 w obu
prywatnych drzewach środowiska i izoluje narzędzia systemowe od nadpisań bibliotek hosta. Przeszedł testy
GUI/API i aktualizacji w świeżej nakładce tej maszyny. Syntetyczna konfiguracja pozostała nienaruszona
podczas wycofania i odinstalowania; awarię starej wersji odtworzono przed aktualizacją i po wycofaniu.
Istniejące zasoby wydań zachowują pierwotne skróty. Start przechwytywania GNOME i wielokrotne tworzenie
wirtualnych wyjść pozostają osobnymi lukami akceptacji. KWin 6.3 w Debianie 13 nadal jest poniżej
minimum 6.6 wymaganego przez pomocnika wyświetlania hosta.

| Dystrybucja / pulpit | Sposób pobrania |
| --- | --- |
| Ubuntu 26.04 GNOME | DEB lub AppImage 0.7.3; regresja startu DEB zaliczona, luki w przechwytywaniu/odzyskiwaniu GNOME pozostają |
| Debian 13 KDE | DEB, instalowany przez APT |
| Fedora 44 KDE | RPM, instalowany przez DNF |
| Arch Linux KDE | Lokalny pakiet Arch, instalowany przez `pacman -U` |
| Bazzite KDE | AppImage 0.7.3; testy dokładnego pakietu w maszynie wirtualnej opisane wyżej |
| Omarchy / Hyprland | Lokalny pakiet Arch, instalowany przez `pacman -U` |

Otwórz terminal w zwykłej sesji pulpitu. Każda sekwencja poleceń przed instalacją sprawdza dokładną
nazwę pobranego pliku względem `SHA256SUMS.txt` danego wydania; suma kontrolna musi zgłosić ten plik
jako `OK`. Brak wpisu lub niezgodność przerywa sekwencję.
Po instalacji uruchom DeskPort jako zwykły użytkownik.
Przed aktualizacją zatrzymaj udostępnianie i całkowicie zamknij starą instancję DeskPort akcją
**Zakończ**, np. z menu zasobnika. Zamknięcie okna może je tylko ukryć.
Następnie uruchom nowo zainstalowaną wersję, aby działał nowy plik wykonywalny.

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — pobieranie, instalacja, aktualizacja i usuwanie</summary>

Pobierz [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb),
a następnie otwórz terminal w katalogu z tym plikiem:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT instaluje zależności ze skonfigurowanych repozytoriów Ubuntu. Początkowe `./` wskazuje pobrany plik.
Później uruchamiaj DeskPort z menu aplikacji. Aby zaktualizować, pobierz kolejny zgodny DEB, zweryfikuj
sumę kontrolną i powtórz `sudo apt install ./NEW_FILENAME.deb` z faktyczną nazwą pliku.
Usuń pakiet poleceniem `sudo apt remove deskport`.

Ubuntu 24.04 również spełnia minimalne wymaganie glibc pakietu natywnego. Ubuntu 22.04 wymaga AppImage,
Flatpaka (tylko oglądanie) lub osobno obsługiwanej kompilacji Nix/ze źródeł. Hostowanie w GNOME zależy od
zainstalowanych możliwości Mutter/przechwytywania; zobacz
[wymagania adaptacyjnego wyświetlania](../LINUX_ADAPTIVE_DISPLAY.md).

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — pobieranie, instalacja, aktualizacja i usuwanie</summary>

Pobierz [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb),
a następnie otwórz terminal w katalogu pobierania:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT rozwiązuje zależności pakietu. Później uruchamiaj DeskPort z menu aplikacji. Aktualizuj, pobierając
i weryfikując kolejny zgodny DEB, a następnie instalując go poleceniem `sudo apt install ./NEW_FILENAME.deb`.
Usuń poleceniem `sudo apt remove deskport`. Debian 12 nie spełnia minimalnego wymagania glibc pakietu
natywnego; użyj AppImage, Flatpaka (tylko oglądanie) lub obsługiwanej kompilacji Nix/ze źródeł.

Przed hostowaniem w KDE sprawdź [wymagania kompozytora i sterowania](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — pobieranie, instalacja, aktualizacja i usuwanie</summary>

Pobierz [deskport-0.7.3-1.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1.x86_64.rpm),
a następnie otwórz terminal w katalogu pobierania:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo dnf install ./deskport-0.7.3-1.x86_64.rpm &&
deskport
```

DNF rozwiązuje zależności z włączonych repozytoriów Fedory. Później uruchamiaj DeskPort z menu aplikacji.
Aktualizuj, pobierając i weryfikując kolejny zgodny RPM, a następnie instalując go poleceniem
`sudo dnf install ./NEW_FILENAME.rpm`. Usuń poleceniem `sudo dnf remove deskport`.
Ta ścieżka dotyczy zwykłej Fedory; dla Bazzite użyj instrukcji AppImage.
Przed hostowaniem w KDE sprawdź też [wymagania kompozytora i sterowania](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — pobieranie, instalacja, aktualizacja i usuwanie</summary>

Wykonaj zwykłą pełną aktualizację systemu Arch, a następnie pobierz
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Otwórz terminal w katalogu pobierania:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` instaluje lokalny pakiet i rozwiązuje zależności ze skonfigurowanych repozytoriów. Później
uruchamiaj DeskPort z menu aplikacji. Kolejne pobrane pakiety instaluj poleceniem
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` po zweryfikowaniu sum kontrolnych. Usuń poleceniem
`sudo pacman -R deskport`. To pobranie nie dodaje DeskPort do oficjalnych repozytoriów ani AUR.
Przed hostowaniem w KDE sprawdź też [wymagania kompozytora i sterowania](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, uruchamianie i usuwanie</summary>

Pobierz
[DeskPort-0.7.3-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage).
Zatrzymaj udostępnianie i użyj **Zakończ**, aby całkowicie zamknąć starszą instancję DeskPort; zamknięcie
okna może pozostawić ją uruchomioną. Przed włączeniem uruchamiania po zalogowaniu zapisz nowy AppImage w
stałym miejscu:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.3-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.3-x86_64.AppImage &&
./DeskPort-0.7.3-x86_64.AppImage
```

Uruchamiaj normalnie; nie trzeba usuwać ani przenosić dołączonych bibliotek. Jeśli uruchamianie po
zalogowaniu wskazywało starszy AppImage, otwórz nowy plik w jego docelowej lokalizacji i odśwież
uruchamianie po zalogowaniu. Aby użyć AppImage w innej wymienionej dystrybucji x86_64, wykonaj te kroki
zamiast instalować pakiet natywny.

[Przewodnik AppImage](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/) Bazzite
opisuje Gear Lever do integracji z menu aplikacji. Jeśli narzędzie integracji przeniesie plik, odśwież
potem uruchamianie DeskPort z docelowej lokalizacji. Aktualizacja wymaga pobrania i zweryfikowania nowego
AppImage. Aby usunąć, wyłącz uruchamianie DeskPort po zalogowaniu, zamknij DeskPort, usuń plik i usuń
wpis menu narzędziem, które go utworzyło. Do udostępniania pulpitu uruchamiaj AppImage na hoście Bazzite;
instalacja w Distrobox nie zapewnia integracji z pulpitem hosta. Flatpak poniżej służy tylko do oglądania.

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — lokalny pakiet Arch i wymagania hosta</summary>

Wykonaj zwykłą aktualizację systemu Omarchy, a następnie pobierz
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Otwórz terminal w katalogu pobierania:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

Później uruchamiaj aplikację z launchera. Aktualizuj, pobierając i weryfikując kolejny lokalny pakiet
Arch, a następnie instalując go poleceniem `sudo pacman -U ./NEW_FILENAME.pkg.tar.zst`.
Usuń poleceniem `sudo pacman -R deskport`. Menu pakietów repozytorium/AUR w Omarchy nie zmienia tego
pliku z GitHub w pakiet repozytorium.

Hostowanie w Hyprland używa dedykowanego wyjścia bezgłowego. Kod źródłowy 0.7.3 obsługuje zasady
kopiowania, wyłączania pozostałych i rozszerzania, gdy tożsamość monitorów i stan środowiska można
bezpiecznie przywrócić; zobacz [dzierżawy ekranów Hyprland](../HYPRLAND_DISPLAY.md).
Postępuj zgodnie z [przewodnikiem hosta Omarchy/Hyprland](../CLI.md#omarchy-and-hyprland)
w zakresie aktywnego kompozytora, uprawnień przechwytywania i usługi SSH.
Przed zastosowaniem składni uprawnień sprawdź zainstalowaną wersję Hyprland.
Sama instalacja pakietu nie konfiguruje nienadzorowanego logowania ani nie weryfikuje działającego strumienia.

</details>

<details>
<summary><strong>Alternatywa Flatpak</strong> — tylko oglądanie, w systemach ze skonfigurowanym Flatpakiem</summary>

Pobierz [DeskPort-0.7.3-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-client-x86_64.flatpak),
a następnie otwórz terminal w katalogu pobierania:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user ./DeskPort-0.7.3-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

Lokalny pakiet może pobrać środowisko Freedesktop Platform 25.08. Służy tylko do oglądania, bez
hostowania Sunshine ani integracji z usługą logowania pulpitu. Kolejne pobrane pakiety instaluj poleceniem
`flatpak install --user --or-update ./NEW_FILENAME.flatpak` po zweryfikowaniu sum kontrolnych. Usuń poleceniem
`flatpak uninstall --user io.github.keithxc.DeskPort`. To pakiet z wydania na GitHubie; instalacja nie
wymaga wpisu DeskPort na Flathubie.

</details>

Do hostowania używaj zwykłej zalogowanej sesji graficznej i skonfiguruj
[wymagania przechwytywania i sterowania](../LINUX_PACKAGES.md#first-connection-and-hosting).
Przed włączeniem opcjonalnego uruchamiania po zalogowaniu przetestuj udostępnianie z klientem w docelowej
sieci LAN lub VPN. Instalacja pakietu nie konfiguruje osiągalności sieciowej ani nie nadaje dostępu do
sterowania hostem. Przed usunięciem pakietu zatrzymaj udostępnianie i zamknij DeskPort; wyłącz
uruchamianie po zalogowaniu, jeśli było włączone. Usunięcie pakietu zachowuje ustawienia DeskPort w
katalogu domowym.

### Dostępność w repozytoriach Linuksa

| Kanał | Stan | Instalacja |
| --- | --- | --- |
| Pakiety z wydań GitHub | 0.7.3 stabilna | Pobierz pakiet dla swojej dystrybucji powyżej |
| Flake Nix projektu | 0.7.3 stabilna | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Przepis przetestowany; publikacja wstrzymana przez pauzę w rejestracji nowych kont | [Zbuduj przepis lokalnie](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) zgłoszony do przeglądu; niescalony | Do czasu włączenia używaj flake projektu |
| Oficjalne repozytoria Arch / Flathub | Brak | Używaj plików z wydań; Flatpak służy tylko do oglądania |

Pobierany pakiet Arch instaluje się przez `pacman -U`; nie udostępnia on
`pacman -S deskport`. Podobnie flake projektu nie udostępnia `pkgs.deskport` w Nixpkgs. Sterowanie hostem,
uprawnienia przechwytywania i konfiguracja zapory są niezależne od instalacji aplikacji; zobacz
[instrukcję instalacji w Linuksie](../LINUX_PACKAGES.md#nixos-and-nix).

## Porównanie z podobnymi produktami

To porównanie sposobu pracy i platform, sprawdzone z podlinkowaną oficjalną dokumentacją
**2026-09-20**, a nie test opóźnień czy jakości obrazu. Funkcje produktów i ograniczenia platform mogą
się zmienić.

| Aspekt | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Główny sposób pracy | Trwała przestrzeń zdalnego pulpitu, adaptacyjny rozmiar i przekazywanie między urządzeniami | Strumieniowanie gier/pulpitu przez osobne aplikacje klienta i hosta | Ogólne zdalne sterowanie i wsparcie | Interaktywny zdalny pulpit i współpraca |
| Platformy hosta | macOS Apple Silicon, Linux x86_64 i Windows x64 | Windows, macOS, Linux i FreeBSD, z ograniczeniami platform | Windows, macOS i Linux | Windows i macOS; Linux nie może być hostem |
| Klienci desktopowi | macOS Apple Silicon, Linux x86_64 i Windows x64 | Między innymi Windows, macOS i Linux | Windows, macOS i Linux | Windows, macOS, Linux i obsługiwane przeglądarki Chromium |
| Klienci mobilni | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS i Android | iOS/iPadOS i Android; iOS nie może być hostem | Android; brak klienta iOS/iPadOS |
| Model konfiguracji | Host wbudowany w natywne pakiety; zatwierdzanie urządzeń w DeskPort | Osobna instalacja i konfiguracja Sunshine, potem parowanie Moonlight | Publiczna infrastruktura serwerów lub własny serwer | Konto i aplikacja Parsec |
| Model sieci/usługi | Własna osiągalność LAN/VPN; brak przekaźnika DeskPort | Własne strumieniowanie; konfiguracja osiągalności sieci | Publiczna infrastruktura lub własny serwer OSS/Pro | Infrastruktura kont/usług Parsec |
| Kod i dystrybucja | Aplikacja desktopowa na GPL, darmowe pakiety; płatna aplikacja Apple dystrybuowana osobno | Otwartoźródłowi klienci i host | Otwartoźródłowy klient i serwer OSS; komercyjna opcja serwera Pro | Własnościowa aplikacja/usługa |

Źródła: [Moonlight](https://moonlight-stream.org/),
[instalacja i obsługiwane platformy Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[klienci RustDesk](https://rustdesk.com/docs/en/client/),
[własny serwer RustDesk](https://rustdesk.com/docs/en/self-host/),
[zgodność platform Parsec](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[konfiguracja Parsec w Linuksie](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
Bieżące ograniczenia platform DeskPort opisano powyżej; ta tabela nie oznacza równości funkcji na każdym
systemie ani zmierzonej przewagi wydajności.

Zobacz [informacje o wydaniu 0.7.3](../releases/RELEASE_0.7.3.md),
[architekturę](../ARCHITECTURE.md) i [plan rozwoju](../ROADMAP.md).

## Kompilacja i uruchamianie w Linuksie

Z włączonym Nix i flakes:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

Zewnętrzne zależności źródłowe przeglądarki są dołączone do repozytorium; wspólny rdzeń i wejścia
kompilacji Nix pozostają przypiętymi zależnościami zewnętrznymi. Kompilacje natywne muszą zainicjować
`shared/deskport-core`. Zobacz [docs/VENDORED.md](../VENDORED.md).
Konfigurację SSH, lokalne zarządzanie hostem, wymagania VPS i Omarchy/Hyprland opisuje
[przewodnik CLI](../CLI.md). `deskport doctor` sprawdza wymagania hostowania;
`deskport status --json` sprawdza działającą instancję bez otwierania okna.
`nix run . -- --help` wyświetla interfejs wiersza poleceń. Przed połączeniem uruchom udostępnianie na
hoście i powiąż urządzenia. Dostępne jest też klasyczne parowanie kodem PIN Sunshine. Żadne osobiste
dane hosta ani parowania nie są dołączone ani importowane z Moonlight.
Nowe ręcznie wpisane adresy domyślnie używają portu DeskPort `48989`. Jeśli port na stronie
udostępniania hosta jest inny, dopisz go, albo użyj `host:47989`, aby jawnie połączyć się z domyślną
samodzielną instalacją Sunshine. Zapisane/wykryte punkty końcowe zachowują własne porty.

Edytowalna kompilacja natywna:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

Nazwy plików projektu bazowego pozostają niezmienione, aby fork był łatwy do przeglądu.

## Czym różni się od Moonlight?

- Wbudowany host Sunshine z zatwierdzaniem urządzeń, przejmowaniem i ustawieniami dla każdego urządzenia.
- Sesje pozostają połączone, gdy przeglądarka jest ukryta; przywołaj ją z zasobnika/paska menu.
- Dostęp z przeglądarki przez HTTPS udostępniany przez hosty desktopowe.
- Osobny plik wykonywalny `deskport`, przestrzeń ustawień Qt `DeskPort` i
  identyfikator aplikacji Linux `io.github.keithxc.DeskPort`.
- Domyślnie strumieniowanie w oknie i sterowanie wskaźnikiem bezwzględnym.
- Wyciszanie po utracie fokusu; optymalizacja gier, mysz z gamepada, tryb wielu kontrolerów,
  sterowanie gamepadem w tle i obecność w Discordzie domyślnie wyłączone.
- Brak monitów o aktualizację Moonlight w tej niezależnej aplikacji.
- Zablokowane środowisko Nix i proces kompilacji dla Linuksa.

Interfejs desktopowy ma strony urządzeń, udostępniania i ustawień, z wyborem języka i osobnymi
uprawnieniami hosta. Zamknięcie przeglądarki zachowuje sesję i otwiera listę urządzeń. Aktywne urządzenie
oferuje Powrót do pulpitu; inne urządzenia pokazują szczegóły, dopóki bieżąca sesja nie zostanie
rozłączona. Przypinaj często używane urządzenia i wybieraj zwartą listę lub karty. Wygląd podąża za
systemem, z możliwością wymuszenia jasnego lub ciemnego.

## Zakres platform

Stan wydania dla każdej platformy znajdziesz w [tabeli platform](#obsługiwane-platformy) powyżej.
Wydania desktopowe obejmują macOS Apple Silicon, Linux x86_64 i Windows x64 jako przeglądarkę i host;
aplikacje mobilne są tylko klientami.

Obsługa klienta i hosta są niezależne: host Mac nie wymaga klienta DeskPort na Maca. Sprzętowe
dekodowanie, sterowanie na żywo, jakość obrazu i opóźnienie przywołania wymagają testów w prawdziwych
sesjach; udana kompilacja ich nie potwierdza.

## Następny kamień milowy

Utrzymać jedną sesję połączoną przez **50 cykli ukrywania/pokazywania**, pokazywać okno na bieżącej
przestrzeni roboczej i niezawodnie zwracać lokalne sterowanie. Mierzyć opóźnienie świeżej klatki i
zużycie zasobów w tle niezależnie od pojawienia się okna.

Kryteria akceptacji i odłożone funkcje opisuje [plan rozwoju](../ROADMAP.md),
a pochodzenie i granice utrzymania — [notatki o projekcie bazowym](../UPSTREAM.md).

## Walidacja

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

Test dymny używa tymczasowych katalogów konfiguracji/pamięci podręcznej XDG i platformy Qt offscreen.
Nie paruje się z hostem, nie uruchamia strumienia ani nie wstrzykuje sterowania.

## Codzienna obsługa na pulpicie

Zamknięcie okna pozostawia DeskPort działający w zasobniku/pasku menu. Użyj **Otwórz DeskPort**, aby go
przywołać, **Rozłącz przeglądarkę**, aby zakończyć tylko bieżące połączenie, lub **Zakończ DeskPort**,
aby zamknąć usługę. Lokalne udostępnianie trwa po zamknięciu przeglądarki.

Udostępnianie schowka z czystym tekstem i przechwytywanie skrótów systemowych są domyślnie włączone na
obu powiązanych urządzeniach. Zmiany ustawień obowiązują po ponownym połączeniu. Udostępniane są tylko
nowe kopie, do 1 MiB. Obsługiwane obrazy i pliki są pobierane na żądanie między zgodnymi komputerami,
które wyraziły zgodę; zobacz [ograniczenia](../CLIPBOARD.md). W trybie wskaźnika pulpitu klawiatura
podąża za wskaźnikiem wewnątrz aktywnego wideo.
**Ctrl+Alt+Shift+Z** zwalnia sterowanie;
**Ctrl+Alt+Shift+Q** rozłącza przeglądarkę. Po jawnym zwolnieniu kliknij wewnątrz, aby odzyskać
sterowanie. Skróty zarezerwowane przez system zależą od kompozytora pulpitu.

Uruchamianie po zalogowaniu i odzyskiwanie wymagają aktywnej graficznej sesji logowania. Zanim zaczniesz
polegać na komputerze przy dostępie nienadzorowanym, zobacz
[testy akceptacyjne i ograniczenia](../INPUT_SERVICE_ACCEPTANCE.md).

## Licencja i autorstwo

DeskPort jest niezależną pochodną [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
pierwotnie opartą na wersji v6.1.0. Nie jest oficjalnym wydaniem Moonlight ani Sunshine.
Moonlight zapewnia podstawę strumieniowania; [Sunshine](https://github.com/LizardByte/Sunshine)
jest dołączony do pakietów hosta dla macOS, Windows i Linuksa (nie do Flatpaka, który służy tylko do
oglądania) i dostarczany przez pakiet Nix dla Linuksa. Osobno zainstalowane usługi Sunshine pozostają
niezależne.

GPL-3.0-or-later; zobacz [LICENSE](../../LICENSE), zachowane informacje w źródłach oraz licencję każdej
dołączonej zależności wymienionej w [docs/VENDORED.md](../VENDORED.md). Oryginalna dokumentacja jest
zachowana w [README.upstream.md](README.upstream.md).

### Diagnostyka i opinie

Dzienniki diagnostyczne są domyślnie włączone. Odtwórz problem, utwórz plik ZIP z dziennikami i otwórz
szkic zgłoszenia na GitHubie. Przejrzyj ZIP przed dołączeniem: zgłoszenia na GitHubie są publiczne, a
DeskPort niczego za ciebie nie przesyła ani nie wysyła. Zobacz
[dane diagnostyczne i ograniczenia](../DIAGNOSTICS.md).
