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
  <a href="README.pl.md">Polski</a> ·
  <a href="README.tr.md">Türkçe</a> ·
  <a href="README.uk.md">Українська</a><br>
  <a href="README.ar.md">العربية</a> ·
  <a href="README.hi.md">हिन्दी</a> ·
  <b>Bahasa Indonesia</b> ·
  <a href="README.ms.md">Bahasa Melayu</a> ·
  <a href="README.th.md">ไทย</a> ·
  <a href="README.vi.md">Tiếng Việt</a> ·
  <a href="README.bo.md">བོད་ཡིག</a>
</p>

<p align="center">
  Ruang kerja desktop jarak jauh yang dibangun di atas Moonlight dan Sunshine.<br>
  Biarkan desktop jarak jauh siap di latar belakang, bawa ke ruang kerja saat ini dengan satu
  tindakan, lalu sembunyikan tanpa menyambung ulang.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3_stable-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Fitur utama

- **Satu aplikasi desktop untuk melihat dan berbagi.** Sunshine bawaan, persetujuan perangkat, kredensial
  dan pengaturan mandiri; paket host macOS, Windows, dan Linux tidak memerlukan instalasi Sunshine terpisah
  (kecuali Flatpak yang hanya untuk melihat).
- **Ruang kerja yang mengikuti klien Anda.** Ukuran adaptif pada host macOS dan KDE yang kompatibel, HiDPI,
  tata letak seluler potret/lanskap, dan penyesuaian ukuran per perangkat.
  Perubahan ukuran menyambung ulang video sesaat; ini bukan pergantian encoder yang mulus.
- **Pilih kebijakan layar host.** Layar utama virtual dengan layar lain dicerminkan, layar utama virtual
  dengan layar lain dimatikan, atau ruang kerja diperluas pada host yang mendukung. DeskPort memulihkan tata
  letak tersimpan saat sesi berakhir.
- **Berpindah antar perangkat yang diizinkan.** Konfirmasi sebelum mengambil alih sesi aktif; klien yang
  tergeser menerima penjelasan pengambilalihan yang diautentikasi.
- **Pekerjaan desktop tetap terjangkau.** Sembunyikan dan panggil kembali penampil yang tersambung, lepaskan
  input jarak jauh saat disembunyikan, dan putuskan tampilan terpisah dari berbagi lokal.
- **Pengaturan disimpan per komputer.** Gambar, audio, input, penyesuaian desktop, dan pengeditan alamat
  tetap melekat pada perangkat; kartu, sematan, tema, dan antarmuka yang dilokalkan.
- **Bagikan papan klip desktop sesuai permintaan.** Teks diperbarui seketika; gambar dan file yang didukung
  diambil saat diminta di antara rekan desktop terikat yang ikut serta.
  Lihat [cakupan dan batasan papan klip](../CLIPBOARD.md).
- **Kontrol seluler native.** iPhone/iPad dan klien Android menyediakan input
  sentuh/trackpad, kontrol keyboard, dan masuk langsung ke desktop jarak jauh.
- **Mulai saat login dan pemulihan macOS opsional.** Memerlukan login grafis aktif dan persetujuan sistem;
  fitur ini tidak membuka FileVault atau login atas nama Anda.

## Pratinjau antarmuka

Tangkapan layar antarmuka saat ini dengan perangkat demo sintetis. Klik panel untuk gambar ukuran penuh;
pratinjau ini tidak menandakan ketersediaan di toko atau rilis.

| Perangkat dan grup | Pengaturan layar per perangkat | Panduan bawaan |
| --- | --- | --- |
| [![Perangkat dan grup](../media/devices.png)](../media/devices.png) | [![Pengaturan layar per perangkat](../media/device-settings.png)](../media/device-settings.png) | [![Panduan bawaan](../media/manual.png)](../media/manual.png) |

## Platform yang didukung

Lihat [matriks implementasi dan penerimaan PC](../PC_SUPPORT_MATRIX.md) untuk cakupan kode sumber
dibandingkan paket terpasang, kemampuan compositor, bukti historis, dan pemeriksaan native yang tersisa.
Ketersediaan rilis di bawah tidak menjamin setiap fitur pada setiap GPU atau sesi OS. Rilis 0.7.3 mencatat
identitas paket yang tepat dan batas validasi dalam laporan verifikasinya.

| Platform | Peran | Status rilis | Unduh |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Penampil + host + layar virtual native | **0.7.3 stabil**; ditandatangani Developer ID dan dinotarisasi Apple | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Penampil + host; ruang kerja adaptif pada host KWin, Mutter, dan Hyprland yang mendukung | **0.7.3 stabil**: DEB, RPM, Arch, AppImage, arsip portabel, dan Nix | [Rilis](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [panduan instalasi](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Hanya penampil | **0.7.3 stabil**, Freedesktop Platform 25.08 | [Rilis](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Hanya klien | App Store; build lebih baru diuji terpisah di TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Hanya klien | **2.7 (63)** · Pengujian tertutup Google Play masih berlangsung | [Unduh APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Penampil + host + layar virtual | **0.7.3 stabil**; installer lengkap bisa menjadi host, ZIP portabel hanya melihat; aplikasi dan installer belum ditandatangani | [Installer](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | Belum memenuhi syarat | Tidak ada paket rilis terverifikasi | — |

> **Pemberitahuan antivirus Windows:** perangkat lunak antivirus mungkin keliru menandai komponen pembantu Windows DeskPort.
> Kami belum mengalaminya dalam pengujian sendiri. Biarkan Defender aktif; jika muncul peringatan,
> lihat laporan verifikasi rilis dan [riwayat sampel](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Paket Linux native memerlukan glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44, Arch terbaru. AppImage
menyertakan runtime privat untuk desktop dengan glibc lebih lama; kemampuan host tetap bergantung pada driver
perangkat keras dan dukungan compositor. Hosting layar virtual di KDE memerlukan entri izin compositor. Paket
native/Nix menyediakan entri tetap; AppImage dan build portabel mendaftarkan jalur pembantu layar saat ini secara
otomatis ketika berbagi dimulai. Input host tetap memerlukan izin perangkat. Aplikasi seluler tidak menjadi host
desktop.
Definisi kode sumber atau kompilasi yang berhasil saja tidak menjadikan sebuah platform didukung.

## Instalasi

**macOS:** buka [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
seret DeskPort ke Applications lalu buka. Berikan izin Perekaman Layar dan Aksesibilitas untuk fitur host.
Nix dan Homebrew tidak diperlukan.

**Linux:** pilih [paket 0.7.3](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
untuk sistem Anda dan ikuti [langkah per distribusi di bawah](#instalasi-linux-per-distribusi),
termasuk [persyaratan input host](../LINUX_PACKAGES.md#first-connection-and-hosting).
Dengan Nix: `nix run github:keithxc/deskport/v0.7.3`.
Flatpak hanya untuk melihat. Rilis menyertakan [checksum SHA-256](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) dan [laporan verifikasi](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · Selama pengujian tertutup Google Play berlangsung, [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) tersedia sementara di GitHub. Kode sumber klien seluler bersifat tertutup. Di sini hanya tersedia berkas instalasi.

Build 63 menggunakan kunci penandatanganan yang sama dengan Google Play. Pembaruan ke versi Play yang lebih baru akan mempertahankan pasangan komputer dan pengaturan. Jika Anda memasang build 62 dari GitHub, hapus instalasinya sebelum memasang build 63. Pengaturan lokal akan terhapus dan komputer perlu dipasangkan kembali.

**iPhone / iPad:** pasang [DeskPort dari App Store](https://apps.apple.com/us/app/deskport/id6812389978).
Aplikasi seluler adalah pembelian berbayar sekali; lihat toko Anda untuk harga lokal terkini. Rilis desktop
tetap gratis dan sumber terbuka. Pembelian mendukung penandatanganan, perangkat uji, dan pengembangan berkelanjutan.

Mulai Berbagi di host, pilih host itu di klien, lalu setujui perangkat di host. Kedua perangkat memerlukan
jalur jaringan yang berfungsi: gunakan LAN atau VPN Anda sendiri seperti Tailscale. DeskPort tidak menyediakan
layanan akun, desktop yang di-host, relay, atau terowongan jaringan. Pemasangan PIN Sunshine lama tetap tersedia.

**0.7.3 stabil:** pembaruan desktop ini menghadirkan katalog UI bersama dalam 22 bahasa beserta terjemahan
desktop lama tambahan, pergantian bahasa di browser, berbagi di sesi Windows yang sudah masuk tanpa UAC
berulang, penskalaan sistem layar virtual, perbaikan runtime Linux, ruang kerja Hyprland/X11 yang terlindungi,
cadangan papan klip Wayland, IME lokal opsional, dan kartu perangkat persegi. Lihat
[catatan rilis 0.7.3](../releases/RELEASE_0.7.3.md) untuk cakupan tepat dan pemeriksaan yang tersisa. Pemeriksaan
paket yang tepat mencakup kepercayaan distribusi macOS, muatan Windows, CLI native dan pemindaian antivirus
terlindungi, serta skenario runtime/paket Linux. Hasil native dan integrasi sebelumnya mempertahankan identitas
sumber aslinya.

**0.7.2 stabil (verifikasi historis):** [rilis desktop](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
menggabungkan peningkatan pengubahan ukuran, streaming jarak jauh, penghemat data, dan runtime Linux.
21 aset di dalamnya mencakup paket, sumber terkait, materi penautan ulang, dan catatan verifikasi.
Lihat [catatan rilis 0.7.2](../releases/RELEASE_0.7.2.md). AppImage final lulus pemeriksaan di VM Bazzite KDE
Wayland baru dengan startup FUSE normal, runtime penampil/pembantu/pemulihan yang cocok, dan pembersihan
terverifikasi; penerimaan GPU fisik dan streaming nyata tetap terpisah.

Windows Setup lulus peningkatan dari 0.7.1 dengan pemeriksaan pelestarian konfigurasi dan pemasangan.
Pemeriksaan GUI lokal tanpa jaringan mencakup peluncuran dari pintasan, Pengaturan/Tentang, tutup ke baki,
panggil kembali, dan Keluar. Penghapusan instalasi dan penerimaan streaming/input nyata yang direncanakan
belum diuji. Perlombaan fokus pada otomatisasi menyebabkan koneksi tak disengaja ke host yang sudah dipasangkan;
pemeriksaan berikutnya mengonfirmasi nol sesi aktif, tidak ada lease yang dipesan, dan tata letak layar asli
dipulihkan. Insiden ini bukan penerimaan streaming, dan diagnostik tidak dapat memastikan bahwa tidak ada input
jarak jauh yang terkirim. Lihat catatan rilis untuk cakupan tepatnya.

**Kandidat sebelumnya:** [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
menghadirkan penyambungan ulang yang lebih cepat saat mengubah ukuran, transisi buram, dan seret yang lebih
mulus di macOS; [0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
menambahkan perbaikan AppImage untuk Bazzite. [Catatan rc.1](../releases/RELEASE_0.7.2-rc.1.md)
dan [catatan rc.2](../releases/RELEASE_0.7.2-rc.2.md) mempertahankan cakupan validasi sebelumnya.

**Akses browser:** host desktop menyajikan desktop jarak jauh HTTPS dengan kode masuk 30 detik, pemasangan
browser opsional selama tujuh hari, ukuran adaptif, dan layar penuh. Lihat [catatan rilis 0.7.1](../releases/RELEASE_0.7.1.md)
dan [panduan browser](../BROWSER_ACCESS.md). AppImage menyertakan runtime glibc privat; paket native tetap
memerlukan glibc 2.39.

### Instalasi Linux per distribusi

Petunjuk ini ditujukan untuk sistem desktop **x86_64** dan unduhan rilis **0.7.3**. Paket native memerlukan
glibc 2.39 atau lebih baru. Ini adalah unduhan upstream, bukan paket dari repositori distribusi DeskPort;
pasang rilis berikutnya dengan mengunduh dan memverifikasi paket baru lagi.

AppImage 0.7.3 mempertahankan perbaikan runtime portabel, pemulihan layar, penghentian proses, izin, dan
mulai saat login untuk Bazzite/KDE Wayland [issue #5](https://github.com/keithxc/deskport/issues/5),
termasuk backport kecil Qt Wayland 6.4.2. Backport itu khusus AppImage; paket native dan Flatpak
mempertahankan runtime Qt dasar. AppImage final **0.7.3** lulus startup FUSE normal dan pemeriksaan
pembantu/runtime di VM Bazzite KDE Wayland baru dengan SELinux Enforcing dan grafis perangkat lunak. Ini tidak
memverifikasi GPU fisik atau streaming nyata; lihat [cakupan verifikasi 0.7.3](../releases/RELEASE_0.7.3.md#validation-boundary).

Hasil instalasi, peningkatan, rollback, penghapusan, dan pelestarian konfigurasi pada VM lengkap dicatat
dalam [matriks siklus hidup desktop](../LINUX_DESKTOP_ACCEPTANCE.md). Uji asap GUI AppImage tidak
memverifikasi operasi paket native. Instalasi paket dan peluncuran GUI saja tidak memverifikasi tangkapan
langsung, dekode video perangkat keras, input jarak jauh, kualitas visual, atau latensi.

VM Ubuntu 26.04 GNOME yang disimpan mereproduksi crash saat startup pada pustaka VA-API Wayland privat yang
lebih lama di DEB 0.7.2 yang diterbitkan. DEB 0.7.3 final memakai libva 2.23.0 yang dipatok di kedua pohon
runtime privat dan mengisolasi utilitas sistem dari penimpaan pustaka host. Paket ini lulus pemeriksaan
GUI/API dan peningkatan pada overlay baru VM tersebut. Konfigurasi sintetis tetap utuh selama rollback dan
penghapusan; crash versi lama direproduksi sebelum peningkatan dan setelah rollback. Aset rilis yang ada
mempertahankan hash aslinya. Startup tangkapan GNOME dan pembuatan output virtual berulang tetap menjadi celah
penerimaan terpisah. KWin 6.3 di Debian 13 juga masih di bawah minimum 6.6 untuk pembantu layar host.

| Distribusi / desktop | Jalur unduhan |
| --- | --- |
| Ubuntu 26.04 GNOME | DEB atau AppImage 0.7.3; regresi startup DEB lulus, celah tangkapan/pemulihan GNOME masih ada |
| Debian 13 KDE | DEB, dipasang dengan APT |
| Fedora 44 KDE | RPM, dipasang dengan DNF |
| Arch Linux KDE | Paket Arch lokal, dipasang dengan `pacman -U` |
| Bazzite KDE | AppImage 0.7.3; pemeriksaan VM paket tepat di atas |
| Omarchy / Hyprland | Paket Arch lokal, dipasang dengan `pacman -U` |

Buka terminal di sesi desktop biasa Anda. Setiap rangkaian perintah memeriksa nama file unduhan yang tepat
terhadap `SHA256SUMS.txt` rilis tersebut sebelum memasang; checksum harus melaporkan file itu sebagai `OK`.
Entri yang tidak ada atau tidak cocok menghentikan rangkaian.
Jalankan DeskPort sebagai pengguna biasa setelah instalasi.
Sebelum peningkatan, hentikan berbagi dan tutup sepenuhnya instance DeskPort lama dengan tindakan **Keluar**,
misalnya dari menu baki. Menutup jendela mungkin hanya menyembunyikannya. Setelah itu jalankan versi yang baru
dipasang agar biner baru yang berjalan.

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — unduh, pasang, perbarui, dan hapus</summary>

Unduh [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb),
lalu buka terminal di direktori yang berisi file itu:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT memasang dependensi dari repositori Ubuntu yang dikonfigurasi. Awalan `./` memilih file yang diunduh.
Untuk peluncuran berikutnya, buka DeskPort dari menu aplikasi. Untuk peningkatan, unduh DEB kompatibel
berikutnya, verifikasi checksum-nya, dan ulangi `sudo apt install ./NEW_FILENAME.deb` dengan nama file
sebenarnya. Hapus paket dengan `sudo apt remove deskport`.

Ubuntu 24.04 juga memenuhi minimum glibc paket native. Ubuntu 22.04 memerlukan AppImage, Flatpak khusus
penampil, atau build Nix/sumber yang didukung terpisah. Hosting di GNOME bergantung pada kemampuan
Mutter/tangkapan yang terpasang; lihat [persyaratan layar adaptif](../LINUX_ADAPTIVE_DISPLAY.md).

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — unduh, pasang, perbarui, dan hapus</summary>

Unduh [deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb),
lalu buka terminal di direktori unduhan:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT menyelesaikan dependensi paket. Buka DeskPort dari menu aplikasi pada peluncuran berikutnya. Tingkatkan
dengan mengunduh dan memverifikasi DEB kompatibel berikutnya, lalu memasangnya dengan
`sudo apt install ./NEW_FILENAME.deb`. Hapus dengan `sudo apt remove deskport`. Debian 12 tidak memenuhi minimum
glibc paket native; gunakan AppImage, Flatpak khusus penampil, atau build Nix/sumber yang didukung.

Untuk hosting di KDE, periksa [persyaratan compositor dan input](../LINUX_PACKAGES.md#first-connection-and-hosting)
sebelum mulai berbagi.

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — unduh, pasang, perbarui, dan hapus</summary>

Unduh [deskport-0.7.3-1.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1.x86_64.rpm),
lalu buka terminal di direktori unduhan:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo dnf install ./deskport-0.7.3-1.x86_64.rpm &&
deskport
```

DNF menyelesaikan dependensi dari repositori Fedora yang aktif. Buka DeskPort dari menu aplikasi pada
peluncuran berikutnya. Tingkatkan dengan mengunduh dan memverifikasi RPM kompatibel berikutnya, lalu memasangnya
dengan `sudo dnf install ./NEW_FILENAME.rpm`. Hapus dengan `sudo dnf remove deskport`. Jalur ini untuk Fedora
biasa; gunakan petunjuk AppImage untuk Bazzite.
Untuk hosting di KDE, periksa juga [persyaratan compositor dan input](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — unduh, pasang, perbarui, dan hapus</summary>

Lakukan pembaruan penuh sistem Arch seperti biasa, lalu unduh
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Buka terminal di direktori unduhan:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` memasang paket lokal dan menyelesaikan dependensi dari repositori yang dikonfigurasi. Buka DeskPort
dari menu aplikasi pada peluncuran berikutnya. Pasang paket yang diunduh kemudian dengan
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` setelah memverifikasi checksum-nya. Hapus dengan
`sudo pacman -R deskport`. Unduhan ini tidak menambahkan DeskPort ke repositori resmi atau AUR.
Untuk hosting di KDE, periksa juga [persyaratan compositor dan input](../LINUX_PACKAGES.md#first-connection-and-hosting).

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, startup, dan penghapusan</summary>

Unduh
[DeskPort-0.7.3-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage).
Hentikan berbagi dan gunakan **Keluar** untuk menutup sepenuhnya instance DeskPort yang lebih lama; menutup
jendela mungkin membuatnya tetap berjalan. Simpan AppImage baru di lokasi permanen sebelum mengaktifkan mulai
saat login:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.3-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.3-x86_64.AppImage &&
./DeskPort-0.7.3-x86_64.AppImage
```

Jalankan seperti biasa; pustaka bawaan tidak perlu dihapus atau dipindahkan. Jika mulai saat login merujuk ke
AppImage lama, buka file baru di jalur akhirnya dan segarkan mulai saat login. Untuk memakai AppImage di
distribusi x86_64 lain yang tercantum, ikuti langkah ini alih-alih memasang paket native-nya.

[Panduan AppImage](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/) Bazzite menjelaskan
Gear Lever untuk integrasi menu aplikasi. Jika alat integrasi memindahkan file, segarkan startup DeskPort dari
lokasi akhirnya setelah itu. Peningkatan memerlukan pengunduhan dan verifikasi AppImage baru. Untuk menghapus,
nonaktifkan mulai saat login DeskPort, keluar dari DeskPort, hapus file, dan hapus entri menu melalui alat
integrasi yang membuatnya. Jalankan AppImage di host Bazzite untuk berbagi desktop; instalasi Distrobox tidak
membentuk integrasi dengan desktop host. Flatpak di bawah hanya untuk melihat.

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — paket Arch lokal dan persyaratan hosting</summary>

Lakukan pembaruan sistem Omarchy seperti biasa, lalu unduh
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst).
Buka terminal di direktori unduhan:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

Gunakan peluncur aplikasi untuk peluncuran berikutnya. Tingkatkan dengan mengunduh dan memverifikasi paket Arch
lokal berikutnya, lalu memasangnya dengan `sudo pacman -U ./NEW_FILENAME.pkg.tar.zst`. Hapus dengan
`sudo pacman -R deskport`. Menu paket repositori/AUR Omarchy tidak menjadikan unduhan GitHub ini paket repositori.

Hosting Hyprland menggunakan output headless khusus. Kode sumber 0.7.3 mendukung kebijakan cermin, matikan yang
lain, dan perluasan ketika identitas monitor dan status runtime dapat dipulihkan dengan aman; lihat
[lease layar Hyprland](../HYPRLAND_DISPLAY.md). Ikuti [panduan host Omarchy/Hyprland](../CLI.md#omarchy-and-hyprland)
untuk persyaratan compositor aktif, izin tangkapan, dan layanan SSH. Periksa versi Hyprland yang terpasang sebelum
menerapkan sintaks izin. Instalasi paket saja tidak mengonfigurasi login desktop tanpa pengawasan atau
memverifikasi stream yang berfungsi.

</details>

<details>
<summary><strong>Alternatif Flatpak</strong> — hanya melihat, pada sistem dengan Flatpak terkonfigurasi</summary>

Unduh [DeskPort-0.7.3-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-client-x86_64.flatpak),
lalu buka terminal di direktori unduhan:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user ./DeskPort-0.7.3-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

Bundel lokal mungkin mengunduh runtime Freedesktop Platform 25.08. Bundel ini hanya untuk melihat, tanpa
hosting Sunshine atau integrasi layanan login desktop. Pasang bundel yang diunduh kemudian dengan
`flatpak install --user --or-update ./NEW_FILENAME.flatpak` setelah memverifikasi checksum-nya. Hapus dengan
`flatpak uninstall --user io.github.keithxc.DeskPort`. Ini adalah bundel rilis GitHub; instalasi tidak memerlukan
listing DeskPort di Flathub.

</details>

Untuk hosting, gunakan sesi grafis biasa yang sudah login dan konfigurasikan
[prasyarat tangkapan dan input](../LINUX_PACKAGES.md#first-connection-and-hosting). Uji berbagi dengan klien
di LAN atau VPN yang dituju sebelum mengaktifkan mulai saat login opsional. Instalasi paket tidak mengonfigurasi
keterjangkauan jaringan atau memberi akses input host. Hentikan berbagi dan keluar dari DeskPort sebelum menghapus
paket; nonaktifkan mulai saat login jika pernah diaktifkan. Penghapusan paket mempertahankan pengaturan DeskPort
di direktori home Anda.

### Ketersediaan repositori Linux

| Kanal | Status | Instalasi |
| --- | --- | --- |
| Paket rilis GitHub | 0.7.3 stabil | Unduh paket untuk distribusi Anda di atas |
| Nix flake upstream | 0.7.3 stabil | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Resep diuji; penerbitan terhambat jeda pendaftaran akun baru | [Bangun resep secara lokal](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) diajukan untuk ditinjau; belum digabung | Gunakan flake upstream hingga disertakan |
| Repositori resmi Arch / Flathub | Tidak tercantum | Gunakan unduhan rilis; Flatpak hanya penampil |

Paket Arch yang dapat diunduh dipasang dengan `pacman -U`; paket ini tidak membuat `pacman -S deskport` tersedia.
Demikian pula, flake upstream tidak membuat `pkgs.deskport` tersedia di Nixpkgs. Input host, izin tangkapan, dan
pengaturan firewall terpisah dari pemasangan aplikasi; lihat [panduan instalasi Linux](../LINUX_PACKAGES.md#nixos-and-nix).

## Perbandingan dengan produk serupa

Ini adalah perbandingan alur kerja dan platform, diperiksa terhadap dokumentasi resmi yang ditautkan pada
**2026-09-20**, bukan tolok ukur latensi atau kualitas gambar. Fitur produk dan batasan platform dapat berubah.

| Aspek | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Alur kerja utama | Ruang kerja desktop jarak jauh persisten, ukuran adaptif, dan serah terima antarperangkat | Streaming game/desktop melalui aplikasi klien dan host terpisah | Kendali jarak jauh dan dukungan umum | Desktop jarak jauh interaktif dan kolaborasi |
| Platform host desktop | macOS Apple Silicon, Linux x86_64, dan Windows x64 | Windows, macOS, Linux, dan FreeBSD, dengan batasan platform | Windows, macOS, dan Linux | Windows dan macOS; Linux tidak bisa menjadi host |
| Klien desktop | macOS Apple Silicon, Linux x86_64, dan Windows x64 | Windows, macOS, dan Linux, antara lain | Windows, macOS, dan Linux | Windows, macOS, Linux, dan browser Chromium yang didukung |
| Klien seluler | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS dan Android | iOS/iPadOS dan Android; iOS tidak bisa menjadi host | Android; tidak ada klien iOS/iPadOS |
| Model penyiapan | Host bawaan dalam paket desktop native; setujui perangkat di DeskPort | Pasang dan konfigurasi Sunshine terpisah, lalu pasangkan Moonlight | Infrastruktur server publik atau server yang di-host sendiri | Akun dan aplikasi Parsec |
| Model jaringan/layanan | Keterjangkauan LAN/VPN milik sendiri; tanpa relay DeskPort | Streaming yang di-host sendiri; konfigurasikan keterjangkauan jaringan | Infrastruktur publik atau server OSS/Pro yang di-host sendiri | Infrastruktur akun/layanan Parsec |
| Sumber dan distribusi | Desktop GPL, paket desktop gratis; aplikasi Apple berbayar didistribusikan terpisah | Klien dan host sumber terbuka | Klien sumber terbuka dan server OSS; opsi server Pro komersial | Aplikasi/layanan proprietary |

Sumber: [Moonlight](https://moonlight-stream.org/),
[instalasi dan dukungan platform Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[klien RustDesk](https://rustdesk.com/docs/en/client/),
[self-hosting RustDesk](https://rustdesk.com/docs/en/self-host/),
[kompatibilitas platform Parsec](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[penyiapan Parsec di Linux](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
Batasan platform DeskPort saat ini tercantum di atas; tabel ini tidak menyiratkan kesetaraan fitur di setiap OS
atau keunggulan kinerja yang terukur.

Lihat [catatan rilis 0.7.3](../releases/RELEASE_0.7.3.md), [arsitektur](../ARCHITECTURE.md), dan [peta jalan](../ROADMAP.md).

## Membangun dan menjalankan di Linux

Dengan Nix dan flakes diaktifkan:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

Dependensi sumber pihak ketiga untuk penampil disertakan dalam repositori; inti bersama dan input build Nix tetap
menjadi dependensi eksternal yang dipatok. Build native harus menginisialisasi `shared/deskport-core`. Lihat
[docs/VENDORED.md](../VENDORED.md). Untuk penyiapan SSH, pengelolaan host lokal, prasyarat VPS, dan
Omarchy/Hyprland, lihat [panduan CLI](../CLI.md). `deskport doctor` memeriksa prasyarat hosting;
`deskport status --json` memeriksa instance yang berjalan tanpa membuka jendela. `nix run . -- --help` menampilkan
antarmuka baris perintah. Mulai Berbagi di host dan ikat perangkat sebelum menyambung. Pemasangan PIN Sunshine lama
juga tersedia. Tidak ada kredensial host atau pemasangan pribadi yang disertakan atau diimpor dari Moonlight.
Alamat manual baru secara bawaan memakai port DeskPort `48989`. Sertakan port yang tampil di halaman berbagi host
jika berbeda, atau gunakan `host:47989` untuk menyambung secara eksplisit ke instalasi Sunshine mandiri bawaan.
Endpoint yang disimpan/ditemukan mempertahankan port masing-masing.

Untuk build native yang dapat diedit:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

Nama file proyek upstream tetap tidak diubah agar fork mudah ditinjau.

## Apa bedanya dengan Moonlight?

- Host Sunshine bawaan dengan persetujuan perangkat, pengambilalihan, dan pengaturan per perangkat.
- Sesi tetap tersambung saat penampil disembunyikan; panggil kembali dari baki/bilah menu.
- Akses browser HTTPS yang disajikan host desktop.
- Executable `deskport` terpisah, namespace pengaturan Qt `DeskPort`, dan ID aplikasi Linux
  `io.github.keithxc.DeskPort`.
- Streaming berjendela dan kontrol penunjuk absolut secara bawaan.
- Bisukan saat kehilangan fokus; optimisasi game, mouse gamepad, mode multi-kontroler, input gamepad latar
  belakang, dan kehadiran Discord dinonaktifkan secara bawaan.
- Tidak ada permintaan pembaruan Moonlight upstream untuk aplikasi independen ini.
- Lingkungan Nix terkunci dan alur kerja build Linux.

Antarmuka desktop menyediakan halaman perangkat, berbagi, dan pengaturan, dengan pemilihan bahasa dan izin host
terpisah. Menutup penampil membuat sesinya tetap tersedia dan membuka daftar perangkat. Perangkat aktif menawarkan
Kembali ke desktop; perangkat lain menampilkan detail hingga sesi saat ini diputus. Sematkan perangkat yang sering
dipakai dan pilih daftar ringkas atau kartu. Tampilan mengikuti sistem, dengan opsi paksa terang dan gelap.

## Cakupan platform

Lihat [tabel platform](#platform-yang-didukung) di atas untuk status rilis per platform. Rilis desktop mencakup macOS
Apple Silicon, Linux x86_64, dan Windows x64 sebagai penampil sekaligus host; aplikasi seluler hanya klien.

Dukungan klien dan host terpisah: host Mac tidak memerlukan klien DeskPort untuk Mac. Dekode perangkat keras,
input langsung, kualitas gambar, dan latensi pemanggilan kembali memerlukan pengujian sesi nyata; build yang
berhasil tidak membuktikannya.

## Tonggak berikutnya

Menjaga satu sesi tetap tersambung selama **50 siklus sembunyikan/tampilkan**, menampilkan jendela di ruang
kerja saat ini, dan mengembalikan input lokal secara andal. Mengukur latensi frame baru dan penggunaan sumber
daya latar belakang secara terpisah dari kemunculan jendela.

Lihat [peta jalan](../ROADMAP.md) untuk kriteria penerimaan dan fitur yang ditunda, serta
[catatan upstream](../UPSTREAM.md) untuk asal-usul dan batas pemeliharaan.

## Validasi

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

Uji asap memakai direktori konfigurasi/cache XDG sementara dan platform Qt offscreen. Uji ini tidak memasangkan
dengan host, tidak memulai stream, dan tidak menyuntikkan input.

## Kontrol desktop sehari-hari

Menutup jendela membuat DeskPort tetap berjalan di baki/bilah menu. Gunakan **Buka DeskPort** untuk memanggilnya
kembali, **Putuskan penampil** untuk mengakhiri koneksi saat ini saja, atau **Keluar dari DeskPort** untuk
menghentikan layanan. Berbagi lokal berlanjut saat penampil ditutup.

Berbagi papan klip teks biasa dan penangkapan pintasan sistem aktif secara bawaan di kedua perangkat terikat.
Perubahan pengaturan berlaku setelah menyambung ulang. Hanya salinan baru yang dibagikan, hingga 1 MiB. Gambar dan
file yang didukung diambil saat diminta di antara rekan desktop kompatibel yang ikut serta; lihat
[batasan](../CLIPBOARD.md). Dalam mode penunjuk desktop, perutean keyboard mengikuti penunjuk di dalam video yang
difokuskan.
**Ctrl+Alt+Shift+Z** melepaskan input;
**Ctrl+Alt+Shift+Q** memutus penampil. Klik di dalam untuk mendapatkan kembali input setelah pelepasan eksplisit.
Pintasan yang dicadangkan OS bergantung pada compositor desktop.

Mulai saat login dan pemulihan memerlukan sesi login grafis aktif. Lihat
[pemeriksaan penerimaan dan batasan](../INPUT_SERVICE_ACCEPTANCE.md) sebelum mengandalkan komputer untuk akses
tanpa pengawasan.

## Lisensi dan atribusi

DeskPort adalah turunan independen dari [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt), awalnya
berbasis v6.1.0. Ini bukan rilis resmi Moonlight atau Sunshine. Moonlight menyediakan fondasi streaming;
[Sunshine](https://github.com/LizardByte/Sunshine) disertakan dalam paket host macOS, Windows, dan Linux (kecuali
Flatpak khusus penampil) dan disediakan oleh paket Nix Linux. Layanan Sunshine yang dipasang terpisah tetap
independen.

GPL-3.0-or-later; lihat [LICENSE](../../LICENSE), pemberitahuan sumber yang dipertahankan, dan lisensi setiap dependensi
yang disertakan dalam [docs/VENDORED.md](../VENDORED.md). Dokumentasi asli disimpan di
[README.upstream.md](README.upstream.md).

### Diagnostik dan masukan

Log diagnostik aktif secara bawaan. Reproduksi masalahnya, lalu buat ZIP log dan buka draf issue GitHub. Tinjau
ZIP sebelum melampirkannya: issue GitHub bersifat publik, dan DeskPort tidak mengunggah atau mengirim apa pun atas
nama Anda. Lihat [data diagnostik dan batasan](../DIAGNOSTICS.md).
