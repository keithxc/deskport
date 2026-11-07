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
  <b>Italiano</b> ·
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
  Uno spazio di lavoro per il desktop remoto basato su Moonlight e Sunshine.<br>
  Tieni il desktop remoto pronto in background, portalo nello spazio di lavoro
  corrente con un solo gesto e mettilo da parte senza riconnetterti.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3_stable-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a> <del>$4.99</del>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Funzionalità principali

- **Un'unica app desktop per visualizzare e condividere.** Sunshine integrato, approvazione dei dispositivi,
  credenziali e impostazioni indipendenti; nessuna installazione separata di Sunshine per i pacchetti host
  macOS, Windows e Linux (escluso il Flatpak solo visualizzatore).
- **Uno spazio di lavoro che segue il client.** Dimensionamento adattivo sugli host macOS e KDE compatibili,
  HiDPI, layout mobili verticali/orizzontali e regolazione delle dimensioni per dispositivo.
  I cambi di dimensione riconnettono brevemente il video; non sono cambi trasparenti dell'encoder.
- **Scegli la politica dei display dell'host.** Primario virtuale con schermi duplicati,
  primario virtuale con gli altri schermi disattivati, oppure uno spazio di lavoro esteso sugli host
  compatibili. DeskPort ripristina il layout salvato al termine della sessione.
- **Spostati tra dispositivi autorizzati.** Conferma prima di subentrare in una sessione attiva;
  il client sostituito riceve una spiegazione autenticata del subentro.
- **Tieni il lavoro desktop a portata di mano.** Nascondi e richiama un visualizzatore connesso, rilascia
  l'input remoto quando è nascosto e disconnetti la visualizzazione indipendentemente dalla condivisione locale.
- **Impostazioni salvate per computer.** Immagine, audio, input, regolazioni del desktop e modifica
  dell'indirizzo restano legate al dispositivo; schede, elementi fissati, temi e interfacce localizzate.
- **Condividi gli appunti del desktop su richiesta.** Il testo si aggiorna subito; immagini e file
  supportati vengono scaricati su richiesta tra peer desktop associati che hanno aderito.
  Vedi [ambito e limiti degli appunti](../CLIPBOARD.md).
- **Controlli mobili nativi.** iPhone/iPad e il client Android offrono input touch/trackpad,
  controlli da tastiera e accesso diretto al desktop remoto.
- **Avvio all'accesso e ripristino su macOS opzionali.** Richiedono un accesso grafico attivo e
  l'approvazione del sistema; non sbloccano FileVault né effettuano l'accesso al posto tuo.

## Anteprima dell'interfaccia

Schermate dell'interfaccia attuale con dispositivi demo sintetici. Fai clic su un riquadro
per l'immagine a grandezza piena; queste anteprime non implicano disponibilità negli store o nelle release.

| Dispositivi e gruppi | Impostazioni dello schermo per dispositivo | Manuale integrato |
| --- | --- | --- |
| [![Dispositivi e gruppi](../media/devices.png)](../media/devices.png) | [![Impostazioni dello schermo per dispositivo](../media/device-settings.png)](../media/device-settings.png) | [![Manuale integrato](../media/manual.png)](../media/manual.png) |

## Piattaforme supportate

Consulta la [matrice di implementazione e accettazione PC](../PC_SUPPORT_MATRIX.md)
per l'ambito attuale tra sorgente e pacchetti installati, le capacità dei compositor,
le prove storiche e i controlli nativi rimanenti. La disponibilità delle release qui sotto
non certifica ogni funzione su ogni GPU o sessione del sistema operativo. La release 0.7.3
registra le identità esatte dei pacchetti e i limiti di validazione nel suo rapporto di verifica.

| Piattaforma | Ruolo | Stato della release | Download |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Visualizzatore + host + display virtuale nativo | **0.7.3 stabile**; firmata Developer ID e notarizzata da Apple | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Visualizzatore + host; spazio di lavoro adattivo sugli host KWin, Mutter e Hyprland compatibili | **0.7.3 stabile**: DEB, RPM, Arch, AppImage, archivio portatile e Nix | [Release](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [guida all'installazione](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Solo visualizzatore | **0.7.3 stabile**, Freedesktop Platform 25.08 | [Release](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Solo client | App Store; build più recenti testate separatamente su TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Solo client | **2.7 (63)** · Test chiuso di Google Play in corso | [Scarica l’APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Visualizzatore + host + display virtuale | **0.7.3 stabile**; l'installer completo fa da host, lo ZIP portatile solo visualizzatore; applicazione e installer non firmati | [Installer](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Mac Intel | Non qualificato | Nessun pacchetto di release verificato | — |

> **Avviso antivirus per Windows:** i software antivirus potrebbero segnalare i componenti ausiliari Windows di DeskPort come falso positivo.
> Non lo abbiamo riscontrato nei nostri test. Mantieni Defender attivo; se compare un avviso,
> consulta il rapporto di verifica della release e la [cronologia dei campioni](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

I pacchetti Linux nativi richiedono glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
Arch aggiornato. L'AppImage include un runtime privato per i desktop con glibc meno recente;
driver hardware e supporto del compositor determinano comunque le capacità di host. L'host con display
virtuale su KDE richiede voci di autorizzazione del compositor. I pacchetti nativi/Nix forniscono voci fisse;
AppImage e build portatili registrano automaticamente il percorso attuale dell'helper di display
all'avvio della condivisione. L'input dell'host richiede comunque permessi sui dispositivi. Le app mobili
non fanno da host per i desktop.
Una definizione di sorgente o una compilazione riuscita da sole non rendono una piattaforma supportata.

## Installazione

**macOS:** apri il [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
trascina DeskPort in Applicazioni e aprilo. Concedi i permessi Registrazione schermo e
Accessibilità per usare le funzioni host. Nix e Homebrew non sono necessari.

**Linux:** scegli il [pacchetto 0.7.3](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
per il tuo sistema e segui i [passaggi per distribuzione qui sotto](#installazione-linux-per-distribuzione),
inclusi i [requisiti di input dell'host](../LINUX_PACKAGES.md#first-connection-and-hosting).
Con Nix: `nix run github:keithxc/deskport/v0.7.3`.
Il Flatpak offre solo la visualizzazione. La release include [checksum SHA-256](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) e un [rapporto di verifica](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · ~~$4.99~~ · Fino al termine del test chiuso di Google Play, l’[APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) è disponibile temporaneamente su GitHub. Il codice sorgente dei client mobili è privato. Qui viene distribuito solo il pacchetto di installazione.

La build 63 usa la stessa chiave di firma di Google Play. L’aggiornamento a una versione Play successiva conserva i computer abbinati e le impostazioni. Se hai installato la build 62 da GitHub, devi disinstallarla prima di installare la 63. Le impostazioni locali verranno cancellate e dovrai abbinare di nuovo i computer.

**iPhone / iPad:** installa [DeskPort dall'App Store](https://apps.apple.com/us/app/deskport/id6812389978).
L'app mobile è un acquisto unico a pagamento; il prezzo locale è indicato nel tuo store.
Le release desktop restano gratuite e open source. Gli acquisti sostengono la firma del codice,
l'hardware di test e lo sviluppo continuo.

Avvia la condivisione sull'host, selezionalo sul client e approva il dispositivo sull'host.
Entrambi i dispositivi devono potersi raggiungere in rete: usa una LAN o la tua VPN, ad esempio
Tailscale. DeskPort non fornisce un servizio di account, desktop ospitati, relay
né tunnel di rete. L'abbinamento tramite PIN Sunshine tradizionale resta disponibile.

**0.7.3 stabile:** questo aggiornamento desktop porta un catalogo UI condiviso in 22 lingue
insieme a ulteriori traduzioni desktop legacy, cambio della lingua nel browser,
condivisione nelle sessioni Windows con utente connesso senza UAC ripetuti,
scalabilità di sistema del display virtuale, riparazioni del runtime Linux, spazi di lavoro
Hyprland/X11 protetti, fallback degli appunti Wayland, IME locale opzionale e schede dispositivo quadrate.
Vedi le [note di rilascio 0.7.3](../releases/RELEASE_0.7.3.md) per l'ambito esatto e i controlli rimanenti.
I controlli sui pacchetti esatti coprono l'attendibilità della distribuzione macOS, i payload Windows,
la CLI nativa e la scansione antivirus protetta, oltre a scenari runtime/pacchetto Linux.
I risultati nativi e di integrazione precedenti mantengono le loro identità di sorgente originali.

**0.7.2 stabile (verifica storica):** la [release desktop](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
riunisce miglioramenti a ridimensionamento, streaming remoto, risparmio dati e runtime Linux.
I suoi 21 asset includono pacchetti, sorgenti corrispondenti, materiali di ricollegamento e
registri di verifica. Vedi le [note di rilascio 0.7.2](../releases/RELEASE_0.7.2.md).
L'AppImage finale ha superato un controllo in una VM Bazzite KDE Wayland pulita con avvio FUSE
normale, runtime viewer/helper/ripristino coerenti e pulizia verificata; l'accettazione con GPU
fisica e streaming reale resta separata.

Il setup di Windows ha superato un aggiornamento dalla 0.7.1 con controlli di conservazione della
configurazione e degli abbinamenti. I controlli GUI locali a rete scollegata hanno coperto avvio dal
collegamento, Impostazioni/Informazioni, chiusura nella barra, richiamo e Esci. Disinstallazione e
accettazione pianificata di streaming/input reali restano da testare. Una race di focus
nell'automazione ha causato una connessione involontaria a un host già abbinato; i controlli successivi
hanno confermato zero sessioni attive, nessun lease riservato e il layout originale dei display ripristinato.
L'incidente non costituisce accettazione dello streaming e la diagnostica non può stabilire che
non sia stato trasmesso alcun input remoto. Vedi le note di rilascio per l'ambito esatto.

**Candidati precedenti:** la [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
ha introdotto riconnessioni più rapide al ridimensionamento, transizioni sfocate e trascinamenti più fluidi su macOS;
la [0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
ha aggiunto le correzioni AppImage per Bazzite. Le loro [note rc.1](../releases/RELEASE_0.7.2-rc.1.md)
e [note rc.2](../releases/RELEASE_0.7.2-rc.2.md) conservano l'ambito di validazione precedente.

**Accesso dal browser:** gli host desktop offrono un desktop remoto HTTPS con un
codice di accesso da 30 secondi, abbinamento opzionale del browser per sette giorni, dimensionamento
adattivo e schermo intero. Vedi le [note di rilascio 0.7.1](../releases/RELEASE_0.7.1.md) e la
[guida al browser](../BROWSER_ACCESS.md). L'AppImage include un runtime glibc privato;
i pacchetti nativi mantengono il requisito di glibc 2.39.

### Installazione Linux per distribuzione

Queste istruzioni riguardano sistemi desktop **x86_64** e i download della release **0.7.3**.
I pacchetti nativi richiedono glibc 2.39 o successiva. Sono download upstream e non
pacchetti di un repository di distribuzione DeskPort; installa le release future
scaricando e verificando di nuovo il nuovo pacchetto.

L'AppImage 0.7.3 mantiene le correzioni per runtime portatile, ripristino dei display,
chiusura dei processi, permessi e avvio all'accesso relative al
[problema #5](https://github.com/keithxc/deskport/issues/5) di Bazzite/KDE Wayland, incluso il piccolo
backport di Qt Wayland 6.4.2. Tale backport è specifico dell'AppImage; i pacchetti nativi e il
Flatpak mantengono il runtime Qt di base. L'AppImage finale **0.7.3** ha superato l'avvio FUSE
normale e i controlli helper/runtime in una VM Bazzite KDE Wayland pulita con
SELinux Enforcing e grafica software. Ciò non verifica una GPU fisica né lo streaming reale;
vedi l'[ambito di verifica 0.7.3](../releases/RELEASE_0.7.3.md#validation-boundary).

I risultati di installazione, aggiornamento, rollback, rimozione e conservazione della configurazione
in VM complete sono riportati nella [matrice del ciclo di vita desktop](../LINUX_DESKTOP_ACCEPTANCE.md).
Uno smoke test GUI dell'AppImage non verifica le operazioni dei pacchetti nativi.
L'installazione del pacchetto e l'avvio della GUI da soli non verificano cattura dal vivo, decodifica
video hardware, input remoto, qualità visiva o latenza.

La VM Ubuntu 26.04 GNOME conservata riproduce un crash all'avvio nella vecchia libreria VA-API Wayland
privata del DEB 0.7.2 pubblicato. Il DEB finale 0.7.3 riutilizza libva 2.23.0 bloccata
in entrambi gli alberi di runtime privati e isola le utilità di sistema dalle sostituzioni di librerie
dell'host. Ha superato i controlli GUI/API e di aggiornamento in un overlay pulito di quella VM.
La configurazione sintetica è rimasta intatta durante rollback e disinstallazione; il crash della vecchia
versione è stato riprodotto prima dell'aggiornamento e dopo il rollback. Gli asset di release esistenti
mantengono i loro hash originali. L'avvio della cattura GNOME e la creazione ripetuta di output virtuali
restano lacune di accettazione separate. Anche KWin 6.3 di Debian 13 resta sotto il minimo 6.6
richiesto dall'helper di display dell'host.

| Distribuzione / desktop | Modalità di download |
| --- | --- |
| Ubuntu 26.04 GNOME | DEB o AppImage 0.7.3; regressione di avvio del DEB superata, restano lacune di cattura/ripristino GNOME |
| Debian 13 KDE | DEB, installato con APT |
| Fedora 44 KDE | RPM, installato con DNF |
| Arch Linux KDE | Pacchetto Arch locale, installato con `pacman -U` |
| Bazzite KDE | AppImage 0.7.3; controlli VM sul pacchetto esatto descritti sopra |
| Omarchy / Hyprland | Pacchetto Arch locale, installato con `pacman -U` |

Apri un terminale nella normale sessione desktop. Ogni sequenza di comandi verifica il nome esatto
del file scaricato rispetto al `SHA256SUMS.txt` della release prima di installarlo; il checksum deve
indicare quel file come `OK`. Una voce mancante o una mancata corrispondenza interrompe la sequenza.
Dopo l'installazione avvia DeskPort come utente normale.
Prima di un aggiornamento, interrompi la condivisione e chiudi completamente la vecchia istanza di DeskPort
con l'azione **Esci**, ad esempio dal menu della barra. Chiudere la finestra potrebbe solo nasconderla.
Avvia poi la versione appena installata in modo che esegua il nuovo binario.

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — download, installazione, aggiornamento e rimozione</summary>

Scarica [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb),
poi apri un terminale nella cartella che contiene il file:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT installa le dipendenze dai repository Ubuntu configurati. Il prefisso `./`
seleziona il file scaricato. Per gli avvii successivi apri DeskPort dal menu delle applicazioni.
Per aggiornare, scarica il DEB compatibile successivo, verificane il checksum e ripeti
`sudo apt install ./NEW_FILENAME.deb` con il nome effettivo del file.
Rimuovi il pacchetto installato con `sudo apt remove deskport`.

Anche Ubuntu 24.04 soddisfa il minimo glibc del pacchetto nativo. Ubuntu 22.04 richiede
l'AppImage, il Flatpak solo visualizzatore o una build Nix/da sorgente supportata separatamente.
L'host su GNOME dipende dalle capacità Mutter/cattura installate; vedi i
[requisiti del display adattivo](../LINUX_ADAPTIVE_DISPLAY.md).

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — download, installazione, aggiornamento e rimozione</summary>

Scarica [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb),
poi apri un terminale nella cartella di download:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT risolve le dipendenze del pacchetto. Per gli avvii successivi apri DeskPort dal menu delle
applicazioni. Aggiorna scaricando e verificando il DEB compatibile successivo, quindi installalo
con `sudo apt install ./NEW_FILENAME.deb`. Rimuovi con `sudo apt remove deskport`.
Debian 12 non soddisfa il minimo glibc del pacchetto nativo; usa l'AppImage, il Flatpak solo
visualizzatore o una build Nix/da sorgente supportata.

Per l'host su KDE, controlla i [requisiti di compositor e input](../LINUX_PACKAGES.md#first-connection-and-hosting)
prima di avviare la condivisione.

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — download, installazione, aggiornamento e rimozione</summary>

Scarica [deskport-0.7.3-1.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1.x86_64.rpm),
poi apri un terminale nella cartella di download:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo dnf install ./deskport-0.7.3-1.x86_64.rpm &&
deskport
```

DNF risolve le dipendenze dai repository Fedora abilitati. Per gli avvii successivi apri DeskPort
dal menu delle applicazioni. Aggiorna scaricando e verificando l'RPM compatibile successivo, quindi
installalo con `sudo dnf install ./NEW_FILENAME.rpm`. Rimuovi con `sudo dnf remove deskport`.
Questo percorso è per Fedora standard; per Bazzite usa le istruzioni dell'AppImage.
Per l'host su KDE, controlla anche i [requisiti di compositor e input](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — download, installazione, aggiornamento e rimozione</summary>

Esegui il consueto aggiornamento completo del sistema Arch, poi scarica
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Apri un terminale nella cartella di download:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` installa il pacchetto locale e risolve le dipendenze dai repository configurati.
Per gli avvii successivi apri DeskPort dal menu delle applicazioni. Installa i pacchetti scaricati in
futuro con `sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` dopo averne verificato il checksum.
Rimuovi con `sudo pacman -R deskport`. Questo download non aggiunge DeskPort ai repository ufficiali né all'AUR.
Per l'host su KDE, controlla anche i [requisiti di compositor e input](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, avvio e rimozione</summary>

Scarica
[DeskPort-0.7.3-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage).
Interrompi la condivisione e usa **Esci** per chiudere completamente qualsiasi istanza precedente
di DeskPort; chiudere la finestra potrebbe lasciarla in esecuzione. Salva la nuova AppImage in una
posizione permanente prima di abilitare l'avvio all'accesso:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.3-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.3-x86_64.AppImage &&
./DeskPort-0.7.3-x86_64.AppImage
```

Avviala normalmente; non è necessario rimuovere o spostare librerie incluse. Se l'avvio all'accesso
puntava a un'AppImage precedente, apri il nuovo file nella sua posizione definitiva e aggiorna l'avvio
all'accesso. Per usare l'AppImage su un'altra distribuzione x86_64 elencata, segui questi passaggi
invece di installarne il pacchetto nativo.

La [guida AppImage](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/) di Bazzite
descrive Gear Lever per l'integrazione nel menu delle applicazioni. Se uno strumento di integrazione
sposta il file, aggiorna poi l'avvio di DeskPort dalla sua posizione definitiva.
Gli aggiornamenti richiedono di scaricare e verificare la nuova AppImage. Per rimuoverla,
disabilita l'avvio all'accesso di DeskPort, chiudi DeskPort, elimina il file e rimuovi eventuali voci
di menu tramite lo strumento di integrazione che le ha create. Esegui l'AppImage sull'host Bazzite per
condividere il desktop; un'installazione in Distrobox non stabilisce l'integrazione con il desktop
dell'host. Il Flatpak qui sotto offre solo la visualizzazione.

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — pacchetto Arch locale e requisiti per l'host</summary>

Esegui il normale aggiornamento di sistema di Omarchy, poi scarica
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Apri un terminale nella cartella di download:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

Usa il launcher delle applicazioni per gli avvii successivi. Aggiorna scaricando e verificando il
pacchetto Arch locale successivo, quindi installalo con
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst`. Rimuovi con `sudo pacman -R deskport`.
Il menu pacchetti repository/AUR di Omarchy non rende questo download da GitHub un pacchetto di repository.

L'host su Hyprland usa un output headless dedicato. Il sorgente 0.7.3 supporta le politiche
duplica, spegni gli altri ed estendi quando identità dei monitor e stato di runtime possono essere
ripristinati in sicurezza; vedi [lease dei display Hyprland](../HYPRLAND_DISPLAY.md).
Segui la [guida host Omarchy/Hyprland](../CLI.md#omarchy-and-hyprland)
per i requisiti di compositor attivo, permessi di cattura e servizio SSH.
Controlla la versione di Hyprland installata prima di applicare la sintassi dei permessi.
L'installazione del pacchetto da sola non configura l'accesso al desktop non presidiato né verifica
uno streaming funzionante.

</details>

<details>
<summary><strong>Alternativa Flatpak</strong> — solo visualizzazione, sui sistemi con Flatpak configurato</summary>

Scarica [DeskPort-0.7.3-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-client-x86_64.flatpak),
poi apri un terminale nella cartella di download:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user ./DeskPort-0.7.3-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

Il bundle locale potrebbe scaricare il runtime Freedesktop Platform 25.08. Offre solo la
visualizzazione, senza host Sunshine né integrazione con il servizio di accesso al desktop.
Installa i bundle scaricati in futuro con `flatpak install --user --or-update ./NEW_FILENAME.flatpak`
dopo averne verificato il checksum. Rimuovi con
`flatpak uninstall --user io.github.keithxc.DeskPort`. Si tratta di un bundle della release GitHub;
l'installazione non richiede una scheda DeskPort su Flathub.

</details>

Per fare da host, usa la normale sessione grafica con accesso effettuato e configura i
[prerequisiti di cattura e input](../LINUX_PACKAGES.md#first-connection-and-hosting).
Prova la condivisione con un client sulla LAN o VPN prevista prima di abilitare l'avvio all'accesso
opzionale. L'installazione del pacchetto non configura la raggiungibilità di rete né concede l'accesso
all'input dell'host. Interrompi la condivisione e chiudi DeskPort prima di rimuovere il pacchetto;
disabilita l'eventuale avvio all'accesso abilitato. La rimozione del pacchetto conserva le impostazioni
di DeskPort nella tua cartella home.

### Disponibilità nei repository Linux

| Canale | Stato | Installazione |
| --- | --- | --- |
| Pacchetti delle release GitHub | 0.7.3 stabile | Scarica il pacchetto per la tua distribuzione qui sopra |
| Flake Nix upstream | 0.7.3 stabile | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Ricetta testata; pubblicazione bloccata dalla sospensione delle registrazioni di nuovi account | [Compila la ricetta in locale](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) inviata per revisione; non unita | Usa il flake upstream fino all'inclusione |
| Repository ufficiali Arch / Flathub | Non presente | Usa i download delle release; il Flatpak è solo visualizzatore |

Il pacchetto Arch scaricabile si installa con `pacman -U`; non rende disponibile
`pacman -S deskport`. Allo stesso modo, il flake upstream non rende disponibile
`pkgs.deskport` in Nixpkgs. Input dell'host, permessi di cattura e configurazione del firewall
sono separati dall'installazione dell'applicazione; vedi la
[guida all'installazione Linux](../LINUX_PACKAGES.md#nixos-and-nix).

## Confronto con prodotti simili

Questo è un confronto di flussi di lavoro e piattaforme, verificato con la documentazione ufficiale
collegata il **2026-09-20**, non un benchmark di latenza o qualità d'immagine. Funzionalità e
limiti delle piattaforme dei prodotti possono cambiare.

| Aspetto | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Flusso principale | Spazio di lavoro remoto persistente, dimensionamento adattivo e passaggio tra dispositivi | Streaming di giochi/desktop tramite app client e host separate | Controllo remoto e assistenza generici | Desktop remoto interattivo e collaborazione |
| Piattaforme host desktop | macOS Apple Silicon, Linux x86_64 e Windows x64 | Windows, macOS, Linux e FreeBSD, con limiti per piattaforma | Windows, macOS e Linux | Windows e macOS; Linux non può fare da host |
| Client desktop | macOS Apple Silicon, Linux x86_64 e Windows x64 | Windows, macOS e Linux, tra gli altri | Windows, macOS e Linux | Windows, macOS, Linux e browser Chromium supportati |
| Client mobili | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS e Android | iOS/iPadOS e Android; iOS non può fare da host | Android; nessun client iOS/iPadOS |
| Modello di configurazione | Host integrato nei pacchetti desktop nativi; approvi i dispositivi in DeskPort | Installa e configura Sunshine separatamente, poi abbina Moonlight | Infrastruttura server pubblica o server self-hosted | Account e applicazione Parsec |
| Modello di rete/servizio | Raggiungibilità tramite la tua LAN/VPN; nessun relay DeskPort | Streaming self-hosted; configura la raggiungibilità di rete | Infrastruttura pubblica o server OSS/Pro self-hosted | Infrastruttura di account/servizio Parsec |
| Sorgente e distribuzione | Desktop GPL, pacchetti desktop gratuiti; app Apple a pagamento distribuita separatamente | Client e host open source | Client e server OSS open source; opzione server Pro commerciale | Applicazione/servizio proprietari |

Fonti: [Moonlight](https://moonlight-stream.org/),
[installazione e supporto piattaforme di Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[client RustDesk](https://rustdesk.com/docs/en/client/),
[self-hosting RustDesk](https://rustdesk.com/docs/en/self-host/),
[compatibilità piattaforme Parsec](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[configurazione di Parsec su Linux](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
I limiti di piattaforma attuali di DeskPort sono elencati sopra; questa tabella non implica
parità di funzionalità su ogni sistema operativo né un vantaggio prestazionale misurato.

Vedi le [note di rilascio 0.7.3](../releases/RELEASE_0.7.3.md),
l'[architettura](../ARCHITECTURE.md) e la [roadmap](../ROADMAP.md).

## Compilare ed eseguire su Linux

Con Nix e i flake abilitati:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

Le dipendenze di sorgente di terze parti del visualizzatore sono incluse nel repository; il core condiviso
e gli input di build Nix restano dipendenze esterne bloccate. Le build native devono inizializzare
`shared/deskport-core`. Vedi [docs/VENDORED.md](../VENDORED.md).
Per configurazione SSH, gestione dell'host locale, prerequisiti VPS e Omarchy/Hyprland,
vedi la [guida CLI](../CLI.md). `deskport doctor` controlla i prerequisiti per l'host;
`deskport status --json` ispeziona l'istanza in esecuzione senza aprire una finestra.
`nix run . -- --help` elenca l'interfaccia a riga di comando. Avvia la condivisione sull'host e
associa i dispositivi prima di connetterti. È disponibile anche l'abbinamento tramite PIN Sunshine
tradizionale. Nessun host personale o credenziale di abbinamento è incluso o importato da Moonlight.
I nuovi indirizzi manuali usano per impostazione predefinita la porta DeskPort `48989`. Includi la porta
mostrata nella pagina di condivisione dell'host se diversa, oppure usa `host:47989` per connetterti
esplicitamente a un'installazione Sunshine autonoma predefinita. Gli endpoint salvati/rilevati
mantengono le proprie porte.

Per una build nativa modificabile:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

I nomi dei file del progetto upstream restano invariati per mantenere il fork facilmente revisionabile.

## Cosa cambia rispetto a Moonlight?

- Host Sunshine integrato con approvazione dei dispositivi, subentro e impostazioni per dispositivo.
- Le sessioni restano connesse mentre il visualizzatore è nascosto; richiamalo dalla barra/menu.
- Accesso HTTPS dal browser servito dagli host desktop.
- Eseguibile `deskport` separato, namespace di impostazioni Qt `DeskPort` e
  ID applicazione Linux `io.github.keithxc.DeskPort`.
- Streaming in finestra e controllo con puntatore assoluto per impostazione predefinita.
- Audio disattivato alla perdita del focus; ottimizzazione giochi, mouse da gamepad, modalità
  multi-controller, input gamepad in background e presenza Discord disattivati per impostazione predefinita.
- Nessuna richiesta di aggiornamento di Moonlight upstream per questa applicazione indipendente.
- Un ambiente Nix bloccato e un flusso di build per Linux.

L'interfaccia desktop offre pagine per dispositivi, condivisione e impostazioni, con selezione della
lingua e permessi host separati. Chiudere il visualizzatore mantiene disponibile la sessione e apre
l'elenco dei dispositivi. Un dispositivo attivo offre Torna al desktop; gli altri dispositivi mostrano
i dettagli finché la sessione corrente non viene disconnessa. Fissa i dispositivi usati spesso e scegli
un elenco compatto o le schede. L'aspetto segue il sistema, con modalità chiara e scura forzabili.

## Ambito delle piattaforme

Consulta la [tabella delle piattaforme](#piattaforme-supportate) qui sopra per lo stato delle release di ogni
piattaforma. Le release desktop coprono macOS Apple Silicon, Linux x86_64 e Windows x64
sia come visualizzatore sia come host; le app mobili sono solo client.

Il supporto come client e quello come host sono separati: un host Mac non richiede un client
DeskPort per Mac. Decodifica hardware, input dal vivo, qualità d'immagine e latenza di richiamo
richiedono test in sessioni reali; una build riuscita non li garantisce.

## Prossimo traguardo

Mantenere una sessione connessa per **50 cicli di nascondi/mostra**, mostrare la finestra nello
spazio di lavoro corrente e restituire in modo affidabile l'input locale. Misurare la latenza del
fotogramma aggiornato e l'uso di risorse in background separatamente dalla comparsa della finestra.

Vedi [la roadmap](../ROADMAP.md) per i criteri di accettazione e le funzioni rinviate,
e le [note upstream](../UPSTREAM.md) per provenienza e limiti di manutenzione.

## Validazione

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

Lo smoke test usa directory di configurazione/cache XDG temporanee e una piattaforma Qt offscreen.
Non si abbina a un host, non avvia uno streaming e non inietta input.

## Controlli desktop quotidiani

Chiudere una finestra lascia DeskPort in esecuzione nella barra/menu. Usa **Apri DeskPort**
per richiamarlo, **Disconnetti visualizzatore** per terminare solo la connessione corrente, oppure
**Esci da DeskPort** per chiudere il servizio. La condivisione locale continua quando un visualizzatore si chiude.

La condivisione degli appunti in testo semplice e la cattura delle scorciatoie di sistema sono attive per
impostazione predefinita su entrambi i dispositivi associati. Le modifiche alle impostazioni si applicano
dopo la riconnessione. Vengono condivise solo le nuove copie, fino a 1 MiB. Immagini e file supportati
vengono scaricati su richiesta tra peer desktop compatibili che hanno aderito; vedi i [limiti](../CLIPBOARD.md).
In modalità puntatore desktop, l'instradamento della tastiera segue il puntatore all'interno del video attivo.
**Ctrl+Alt+Shift+Z** rilascia l'input;
**Ctrl+Alt+Shift+Q** disconnette il visualizzatore. Fai clic all'interno per riprendere l'input dopo
un rilascio esplicito. Le scorciatoie riservate dal sistema dipendono dal compositor del desktop.

Avvio all'accesso e ripristino richiedono una sessione di accesso grafica attiva. Consulta i
[controlli di accettazione e limitazioni](../INPUT_SERVICE_ACCEPTANCE.md) prima di affidarti a un
computer per l'accesso non presidiato.

## Licenza e attribuzione

DeskPort è un derivato indipendente di [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
inizialmente basato sulla v6.1.0. Non è una release ufficiale di Moonlight o Sunshine.
Moonlight fornisce le basi dello streaming; [Sunshine](https://github.com/LizardByte/Sunshine)
è incluso nei pacchetti host per macOS, Windows e Linux (escluso il Flatpak solo visualizzatore) e
fornito dal pacchetto Nix per Linux. I servizi Sunshine installati separatamente restano indipendenti.

GPL-3.0-or-later; vedi [LICENSE](../../LICENSE), le note sul sorgente conservate e la licenza di ogni
dipendenza inclusa elencata in [docs/VENDORED.md](../VENDORED.md). La documentazione originale è conservata in
[README.upstream.md](README.upstream.md).

### Diagnostica e feedback

I log diagnostici sono attivi per impostazione predefinita. Riproduci il problema,
poi crea un file ZIP dei log e apri una bozza di issue su GitHub. Controlla lo ZIP prima di allegarlo:
le issue di GitHub sono pubbliche e DeskPort non carica né invia nulla al posto tuo.
Vedi [dati diagnostici e limiti](../DIAGNOSTICS.md).
