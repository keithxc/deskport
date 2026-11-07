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
  <b>Türkçe</b> ·
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
  Moonlight ve Sunshine üzerine kurulu bir uzak masaüstü çalışma alanı.<br>
  Uzak masaüstünüzü arka planda hazır tutun, tek bir hareketle mevcut çalışma alanınıza
  getirin ve yeniden bağlanmadan kenara kaldırın.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3_stable-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Temel özellikler

- **Görüntüleme ve paylaşım için tek masaüstü uygulaması.** Paketlenmiş Sunshine, cihaz onayı,
  bağımsız kimlik bilgileri ve ayarlar; macOS, Windows ve Linux ana bilgisayar paketleri için ayrı
  Sunshine kurulumu gerekmez (yalnızca görüntüleyici olan Flatpak hariç).
- **İstemcinizi izleyen bir çalışma alanı.** Uyumlu macOS ve KDE ana bilgisayarlarında uyarlanabilir boyut,
  HiDPI, dikey/yatay mobil düzenler ve cihaz başına boyut ayarı.
  Boyut değişiklikleri videoyu kısa süreliğine yeniden bağlar; kesintisiz kodlayıcı değişimi değildir.
- **Ana bilgisayar ekran politikasını seçin.** Diğer ekranları yansıtan sanal birincil ekran,
  diğer ekranları kapatan sanal birincil ekran ya da uygun ana bilgisayarlarda genişletilmiş çalışma alanı.
  Oturum bittiğinde DeskPort kaydedilen düzeni geri yükler.
- **Yetkili cihazlar arasında geçiş yapın.** Etkin bir oturumu devralmadan önce onaylayın;
  yerinden edilen istemci kimliği doğrulanmış bir devralma açıklaması alır.
- **Masaüstü işleriniz el altında.** Bağlı görüntüleyiciyi gizleyip geri çağırın, gizliyken uzak girdiyi
  bırakın ve görüntülemeyi yerel paylaşımdan bağımsız olarak kesin.
- **Bilgisayar başına kaydedilen ayarlar.** Görüntü, ses, girdi, masaüstü ayarları ve adres düzenleme
  cihazla birlikte kalır; kartlar, sabitlemeler, temalar ve yerelleştirilmiş arayüzler.
- **Masaüstü panosunu isteğe bağlı paylaşın.** Metin hemen güncellenir; desteklenen görseller ve dosyalar,
  katılmayı seçmiş eşleşmiş masaüstü eşleri arasında istendiğinde alınır.
  Bkz. [pano kapsamı ve sınırları](../CLIPBOARD.md).
- **Yerel mobil kontroller.** iPhone/iPad ve Android istemcisi dokunma/izleme
  paneli girdisi, klavye kontrolleri ve uzak masaüstüne doğrudan giriş sunar.
- **İsteğe bağlı oturum açılışında başlatma ve macOS kurtarma.** Etkin bir grafik oturum ve sistem onayı
  gerektirir; FileVault kilidini açmaz veya sizin yerinize oturum açmaz.

## Arayüz önizlemesi

Sentetik demo cihazlarla güncel arayüzün ekran görüntüleri. Tam boyutlu görüntü için bir panele tıklayın;
bu önizlemeler mağaza veya sürüm kullanılabilirliği anlamına gelmez.

| Cihazlar ve gruplar | Cihaz başına ekran ayarları | Yerleşik kılavuz |
| --- | --- | --- |
| [![Cihazlar ve gruplar](../media/devices.png)](../media/devices.png) | [![Cihaz başına ekran ayarları](../media/device-settings.png)](../media/device-settings.png) | [![Yerleşik kılavuz](../media/manual.png)](../media/manual.png) |

## Desteklenen platformlar

Kaynak koduna karşı kurulu paket kapsamı, birleştirici yetenekleri, geçmiş kanıtlar ve kalan yerel
kontroller için [PC uygulama ve kabul matrisine](../PC_SUPPORT_MATRIX.md) bakın. Aşağıdaki sürüm
durumu her özelliği her GPU veya işletim sistemi oturumunda onaylamaz. 0.7.3 sürümü, kesin paket
kimliklerini ve doğrulama sınırlarını doğrulama raporunda kaydeder.

| Platform | Rol | Sürüm durumu | İndirme |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Görüntüleyici + ana bilgisayar + yerel sanal ekran | **0.7.3 kararlı**; Developer ID ile imzalı ve Apple tarafından onaylanmış | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Görüntüleyici + ana bilgisayar; uygun KWin, Mutter ve Hyprland ana bilgisayarlarında uyarlanabilir çalışma alanı | **0.7.3 kararlı**: DEB, RPM, Arch, AppImage, taşınabilir arşiv ve Nix | [Sürüm](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [kurulum kılavuzu](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Yalnızca görüntüleyici | **0.7.3 kararlı**, Freedesktop Platform 25.08 | [Sürüm](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Yalnızca istemci | App Store; yeni derlemeler TestFlight'ta ayrıca test edilir | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Yalnızca istemci | **2.7 (63)** · Google Play kapalı testi sürüyor | [APK indir](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Görüntüleyici + ana bilgisayar + sanal ekran | **0.7.3 kararlı**; tam yükleyici ana bilgisayar olur, taşınabilir ZIP yalnızca görüntüler; uygulama ve yükleyici imzasız | [Yükleyici](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | Nitelendirilmedi | Doğrulanmış sürüm paketi yok | — |

> **Windows antivirüs uyarısı:** Antivirüs yazılımları DeskPort'un Windows yardımcı bileşenlerini yanlışlıkla tehdit olarak işaretleyebilir.
> Kendi testlerimizde bununla karşılaşmadık. Defender'ı açık tutun; bir uyarı görürseniz
> sürüm doğrulama raporuna ve [örnek geçmişine](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice) bakın.

Yerel Linux paketleri glibc 2.39+ gerektirir: Ubuntu 24.04+/Debian 13+, Fedora 44,
güncel Arch. AppImage, eski glibc'li masaüstleri için özel bir çalışma zamanı içerir; ana bilgisayar
yetenekleri yine de donanım sürücülerine ve birleştirici desteğine bağlıdır. KDE'de sanal ekranla ana
bilgisayar olmak birleştirici izin girdileri gerektirir. Yerel/Nix paketleri sabit girdiler sağlar;
AppImage ve taşınabilir derlemeler paylaşım başladığında geçerli ekran yardımcısı yolunu otomatik olarak
kaydeder. Ana bilgisayar girdisi yine de cihaz izinleri gerektirir. Mobil uygulamalar masaüstü paylaşmaz.
Yalnızca bir kaynak tanımı veya başarılı bir derleme, bir platformu desteklenen yapmaz.

## Kurulum

**macOS:** [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) dosyasını açın,
DeskPort'u Uygulamalar klasörüne sürükleyip başlatın. Ana bilgisayar özellikleri için Ekran Kaydı ve
Erişilebilirlik izinlerini verin. Nix ve Homebrew gerekmez.

**Linux:** Sisteminiz için [0.7.3 paketini](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
seçin ve [aşağıdaki dağıtıma özel adımları](#dağıtıma-göre-linux-kurulumu) izleyin; buna
[ana bilgisayar girdi gereksinimleri](../LINUX_PACKAGES.md#first-connection-and-hosting) de dahildir.
Nix ile: `nix run github:keithxc/deskport/v0.7.3`.
Flatpak yalnızca görüntüleme sağlar. Sürüm, [SHA-256 sağlama toplamlarını](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) ve bir [doğrulama raporunu](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt) içerir.

**Android 2.7 (63)** · Google Play kapalı testi tamamlanana kadar [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) geçici olarak GitHub üzerinden indirilebilir. Mobil istemcilerin kaynak kodu kapalıdır. Burada yalnızca kurulum paketi dağıtılır.

63 numaralı derleme Google Play ile aynı uygulama imzalama anahtarını kullanır. Play üzerinden daha yeni bir sürüme güncellediğinizde eşleştirilmiş bilgisayarlar ve ayarlarınız korunur. GitHub’dan 62 numaralı derlemeyi yüklediyseniz 63’ü yüklemeden önce uygulamayı kaldırmanız gerekir. Yerel ayarlar silinir ve bilgisayarları yeniden eşleştirmeniz gerekir.

**iPhone / iPad:** [DeskPort'u App Store'dan](https://apps.apple.com/us/app/deskport/id6812389978) yükleyin.
Mobil uygulama tek seferlik ücretli bir satın alımdır; güncel yerel fiyat için mağazanıza bakın.
Masaüstü sürümleri ücretsiz ve açık kaynak olarak kalır. Satın alımlar imzalamayı, test donanımını ve
geliştirmenin sürmesini destekler.

Ana bilgisayarda Paylaşımı Başlat'ı seçin, istemcide onu seçin ve cihazı ana bilgisayarda onaylayın.
İki cihaz da birbirine ağ üzerinden ulaşabilmelidir: bir LAN veya Tailscale gibi kendi VPN'inizi kullanın.
DeskPort hesap hizmeti, barındırılan masaüstü, aktarma sunucusu veya ağ tüneli sağlamaz.
Eski Sunshine PIN eşleştirmesi kullanılmaya devam eder.

**0.7.3 kararlı:** Bu masaüstü güncellemesi, ek eski masaüstü çevirileriyle birlikte 22 dilli ortak bir
arayüz kataloğu, tarayıcıda dil değiştirme, tekrarlanan UAC olmadan oturum açılmış Windows oturumlarında
paylaşım, sanal ekran sistem ölçeklemesi, Linux çalışma zamanı onarımları, korumalı Hyprland/X11 çalışma
alanları, Wayland pano yedekleri, isteğe bağlı yerel IME ve kare cihaz kartları getirir. Kesin kapsam ve
kalan kontroller için [0.7.3 sürüm notlarına](../releases/RELEASE_0.7.3.md) bakın. Kesin paket kontrolleri
macOS dağıtım güvenini, Windows yüklerini, yerel CLI'yi ve korumalı antivirüs taramasını, ayrıca Linux
çalışma zamanı/paket senaryolarını kapsar. Daha önceki yerel ve entegrasyon sonuçları özgün kaynak
kimliklerini korur.

**0.7.2 kararlı (geçmiş doğrulama):** [Masaüstü sürümü](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
yeniden boyutlandırma, uzak akış, veri tasarrufu ve Linux çalışma zamanı iyileştirmelerini birleştirir.
21 varlığı paketleri, karşılık gelen kaynakları, yeniden bağlama materyallerini ve doğrulama kayıtlarını
içerir. Bkz. [0.7.2 sürüm notları](../releases/RELEASE_0.7.2.md).
Son AppImage, normal FUSE başlatma, eşleşen görüntüleyici/yardımcı/kurtarma çalışma zamanları ve doğrulanmış
temizlikle yeni bir Bazzite KDE Wayland sanal makinesi kontrolünü geçti; fiziksel GPU ve gerçek akış kabulü
ayrı kalır.

Windows Kurulumu, 0.7.1'den yükseltmeyi yapılandırma ve eşleştirme korunması kontrolleriyle geçti.
Bağlantısız yerel GUI kontrolleri kısayoldan başlatma, Ayarlar/Hakkında, tepsiye kapatma, geri çağırma ve
Çıkış'ı kapsadı. Kaldırma ve planlanan gerçek akış/girdi kabulü test edilmedi. Otomasyondaki bir odak
yarışı, mevcut eşleşmiş bir ana bilgisayara istenmeyen bir bağlantıya yol açtı; sonraki kontroller sıfır
etkin oturum, ayrılmış kira olmadığını ve özgün ekran düzeninin geri yüklendiğini doğruladı. Olay akış kabulü
değildir ve tanılama, hiçbir uzak girdinin iletilmediğini kanıtlayamaz. Kesin kapsam için sürüm notlarına bakın.

**Önceki adaylar:** [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
daha hızlı yeniden boyutlandırma bağlantıları, bulanık geçişler ve daha akıcı macOS sürüklemeleri getirdi;
[0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
Bazzite AppImage düzeltmelerini ekledi. [rc.1 notları](../releases/RELEASE_0.7.2-rc.1.md)
ve [rc.2 notları](../releases/RELEASE_0.7.2-rc.2.md) önceki doğrulama kapsamını korur.

**Tarayıcı erişimi:** Masaüstü ana bilgisayarları 30 saniyelik oturum kodu, isteğe bağlı yedi günlük
tarayıcı eşleştirmesi, uyarlanabilir boyut ve tam ekranla HTTPS uzak masaüstü sunar.
Bkz. [0.7.1 sürüm notları](../releases/RELEASE_0.7.1.md) ve [tarayıcı kılavuzu](../BROWSER_ACCESS.md).
AppImage özel bir glibc çalışma zamanı içerir; yerel paketler glibc 2.39 gereksinimini korur.

### Dağıtıma göre Linux kurulumu

Bu talimatlar **x86_64** masaüstü sistemleri ve **0.7.3** sürüm indirmeleri içindir. Yerel paketler
glibc 2.39 veya daha yenisini gerektirir. Bunlar bir DeskPort dağıtım deposundaki paketler değil, üst
kaynaktan indirmelerdir; gelecekteki sürümleri yeni paketi yeniden indirip doğrulayarak kurun.

0.7.3 AppImage, Bazzite/KDE Wayland [sorun #5](https://github.com/keithxc/deskport/issues/5) için
taşınabilir çalışma zamanı, ekran geri yükleme, süreç sonlandırma, izin ve oturum açılışında başlatma
düzeltmelerini, küçük Qt Wayland 6.4.2 geri aktarımı dahil korur. Bu geri aktarım yalnızca AppImage'a
özgüdür; yerel paketler ve Flatpak temel Qt çalışma zamanını korur. Son **0.7.3** AppImage, SELinux
Enforcing ve yazılım grafikleriyle yeni bir Bazzite KDE Wayland sanal makinesinde normal FUSE başlatma ve
yardımcı/çalışma zamanı kontrollerini geçti. Bu, fiziksel GPU'yu veya gerçek akışı doğrulamaz; bkz.
[0.7.3 doğrulama kapsamı](../releases/RELEASE_0.7.3.md#validation-boundary).

Tam sanal makinede kurulum, yükseltme, geri alma, kaldırma ve yapılandırma koruma sonuçları
[masaüstü yaşam döngüsü matrisinde](../LINUX_DESKTOP_ACCEPTANCE.md) izlenir.
AppImage GUI duman testi yerel paket işlemlerini doğrulamaz.
Paket kurulumu ve GUI başlatma tek başına canlı yakalamayı, donanım video kod çözmeyi, uzak girdiyi,
görüntü kalitesini veya gecikmeyi doğrulamaz.

Saklanan Ubuntu 26.04 GNOME sanal makinesi, yayımlanmış 0.7.2 DEB'in eski özel VA-API Wayland
kütüphanesinde bir başlangıç çökmesini yeniden üretir. Son 0.7.3 DEB, her iki özel çalışma zamanı
ağacında sabitlenmiş libva 2.23.0'ı kullanır ve sistem araçlarını ana bilgisayar kütüphane
geçersiz kılmalarından yalıtır. Bu sanal makinenin yeni bir katmanında GUI/API ve yükseltme kontrollerini
geçti. Sentetik yapılandırma geri alma ve kaldırma boyunca bozulmadan kaldı; eski sürüm çökmesi
yükseltmeden önce ve geri almadan sonra yeniden üretildi. Mevcut sürüm varlıkları özgün karmalarını korur.
GNOME yakalama başlatma ve tekrarlanan sanal çıkış oluşturma ayrı kabul açıkları olarak kalır. Debian 13'ün
KWin 6.3'ü de ana bilgisayar ekran yardımcısının 6.6 alt sınırının altında kalır.

| Dağıtım / masaüstü | İndirme yolu |
| --- | --- |
| Ubuntu 26.04 GNOME | 0.7.3 DEB veya AppImage; DEB başlangıç regresyonu geçti, GNOME yakalama/kurtarma açıkları sürüyor |
| Debian 13 KDE | DEB, APT ile kurulur |
| Fedora 44 KDE | RPM, DNF ile kurulur |
| Arch Linux KDE | Yerel Arch paketi, `pacman -U` ile kurulur |
| Bazzite KDE | 0.7.3 AppImage; yukarıdaki kesin paket sanal makine kontrolleri |
| Omarchy / Hyprland | Yerel Arch paketi, `pacman -U` ile kurulur |

Normal masaüstü oturumunuzda bir terminal açın. Her komut dizisi, kurmadan önce indirilen dosyanın tam adını
o sürümün `SHA256SUMS.txt` dosyasıyla karşılaştırır; sağlama toplamı o dosya için `OK` bildirmelidir.
Eksik bir girdi veya uyuşmazlık diziyi durdurur.
Kurulumdan sonra DeskPort'u normal kullanıcınız olarak başlatın.
Yükseltmeden önce paylaşımı durdurun ve eski DeskPort örneğini, örneğin tepsi menüsündeki **Çıkış**
eylemiyle tamamen kapatın. Pencereyi kapatmak onu yalnızca gizleyebilir.
Ardından yeni ikili dosyanın çalışması için yeni kurulan sürümü başlatın.

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — indirme, kurulum, güncelleme ve kaldırma</summary>

[deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb) dosyasını indirin,
ardından bu dosyanın bulunduğu dizinde bir terminal açın:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT bağımlılıkları yapılandırılmış Ubuntu depolarından kurar. Baştaki `./` indirilen dosyayı seçer.
Sonraki açılışlarda DeskPort'u uygulama menüsünden açın. Yükseltmek için bir sonraki uyumlu DEB'i indirin,
sağlama toplamını doğrulayın ve gerçek dosya adıyla `sudo apt install ./NEW_FILENAME.deb` komutunu tekrarlayın.
Kurulu paketi `sudo apt remove deskport` ile kaldırın.

Ubuntu 24.04 de yerel paketin glibc alt sınırını karşılar. Ubuntu 22.04 için AppImage, yalnızca
görüntüleyici Flatpak veya ayrıca desteklenen bir Nix/kaynak derlemesi gerekir. GNOME'da ana bilgisayar
olmak, kurulu Mutter/yakalama yeteneklerine bağlıdır; bkz.
[uyarlanabilir ekran gereksinimleri](../LINUX_ADAPTIVE_DISPLAY.md).

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — indirme, kurulum, güncelleme ve kaldırma</summary>

[deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb) dosyasını indirin,
ardından indirme dizininde bir terminal açın:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT paketin bağımlılıklarını çözer. Sonraki açılışlarda DeskPort'u uygulama menüsünden açın. Bir sonraki
uyumlu DEB'i indirip doğruladıktan sonra `sudo apt install ./NEW_FILENAME.deb` ile kurarak yükseltin.
`sudo apt remove deskport` ile kaldırın. Debian 12 yerel paketin glibc alt sınırını karşılamaz; AppImage,
yalnızca görüntüleyici Flatpak veya desteklenen bir Nix/kaynak derlemesi kullanın.

KDE'de ana bilgisayar olmadan önce [birleştirici ve girdi gereksinimlerini](../LINUX_PACKAGES.md#first-connection-and-hosting)
kontrol edin.

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — indirme, kurulum, güncelleme ve kaldırma</summary>

[deskport-0.7.3-1.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1.x86_64.rpm) dosyasını indirin,
ardından indirme dizininde bir terminal açın:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo dnf install ./deskport-0.7.3-1.x86_64.rpm &&
deskport
```

DNF bağımlılıkları etkin Fedora depolarından çözer. Sonraki açılışlarda DeskPort'u uygulama menüsünden
açın. Bir sonraki uyumlu RPM'i indirip doğruladıktan sonra `sudo dnf install ./NEW_FILENAME.rpm` ile
kurarak yükseltin. `sudo dnf remove deskport` ile kaldırın.
Bu yol standart Fedora içindir; Bazzite için AppImage talimatlarını kullanın.
KDE'de ana bilgisayar olmak için [birleştirici ve girdi gereksinimlerini](../LINUX_PACKAGES.md#first-connection-and-hosting) de kontrol edin.

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — indirme, kurulum, güncelleme ve kaldırma</summary>

Her zamanki tam Arch sistem güncellemenizi yapın, ardından
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst) dosyasını indirin.
İndirme dizininde bir terminal açın:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` yerel paketi kurar ve bağımlılıkları yapılandırılmış depolardan çözer. Sonraki açılışlarda
DeskPort'u uygulama menüsünden açın. Gelecekte indirilen paketleri sağlama toplamlarını doğruladıktan sonra
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` ile kurun. `sudo pacman -R deskport` ile kaldırın. Bu indirme
DeskPort'u resmî depolara veya AUR'a eklemez.
KDE'de ana bilgisayar olmak için [birleştirici ve girdi gereksinimlerini](../LINUX_PACKAGES.md#first-connection-and-hosting) de kontrol edin.

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, başlatma ve kaldırma</summary>

[DeskPort-0.7.3-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage)
dosyasını indirin. Paylaşımı durdurun ve eski DeskPort örneklerini tamamen kapatmak için **Çıkış**'ı
kullanın; pencereyi kapatmak onu çalışır durumda bırakabilir. Oturum açılışında başlatmayı etkinleştirmeden
önce yeni AppImage'ı kalıcı bir konuma kaydedin:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.3-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.3-x86_64.AppImage &&
./DeskPort-0.7.3-x86_64.AppImage
```

Normal şekilde başlatın; paketlenmiş kütüphaneleri kaldırmanız veya taşımanız gerekmez. Oturum açılışında
başlatma eski bir AppImage'ı gösteriyorsa, yeni dosyayı son konumunda açın ve oturum açılışında başlatmayı
yenileyin. AppImage'ı listelenen başka bir x86_64 dağıtımında kullanmak için yerel paketi kurmak yerine bu
adımları izleyin.

Bazzite'in [AppImage kılavuzu](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/),
uygulama menüsü entegrasyonu için Gear Lever'ı anlatır. Bir entegrasyon aracı dosyayı taşırsa, ardından
DeskPort başlatmasını son konumundan yenileyin. Yükseltmeler yeni AppImage'ı indirip doğrulamayı gerektirir.
Kaldırmak için DeskPort oturum açılışında başlatmayı devre dışı bırakın, DeskPort'tan çıkın, dosyayı silin ve
menü girdisini oluşturan entegrasyon aracıyla kaldırın. Masaüstü paylaşımı için AppImage'ı Bazzite ana
bilgisayarında çalıştırın; Distrobox kurulumu ana bilgisayar masaüstü entegrasyonu sağlamaz. Aşağıdaki
Flatpak yalnızca görüntüleme sağlar.

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — yerel Arch paketi ve ana bilgisayar gereksinimleri</summary>

Omarchy'nin normal sistem güncellemesini yapın, ardından
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst) dosyasını indirin.
İndirme dizininde bir terminal açın:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

Sonraki açılışlarda uygulama başlatıcısını kullanın. Bir sonraki yerel Arch paketini indirip doğruladıktan
sonra `sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` ile kurarak yükseltin. `sudo pacman -R deskport` ile
kaldırın. Omarchy'nin depo/AUR paket menüsü bu GitHub indirmesini bir depo paketine dönüştürmez.

Hyprland'da ana bilgisayar olmak, ayrılmış bir başsız çıkış kullanır. 0.7.3 kaynağı, monitör kimliği ve
çalışma zamanı durumu güvenle geri yüklenebildiğinde yansıtma, diğerlerini kapatma ve genişletme
politikalarını destekler; bkz. [Hyprland ekran kiraları](../HYPRLAND_DISPLAY.md).
Etkin birleştirici, yakalama izni ve SSH hizmeti gereksinimleri için
[Omarchy/Hyprland ana bilgisayar kılavuzunu](../CLI.md#omarchy-and-hyprland) izleyin.
İzin sözdizimini uygulamadan önce kurulu Hyprland sürümünü kontrol edin.
Paket kurulumu tek başına gözetimsiz masaüstü oturumunu yapılandırmaz veya çalışan bir akışı doğrulamaz.

</details>

<details>
<summary><strong>Flatpak alternatifi</strong> — yalnızca görüntüleme, Flatpak yapılandırılmış sistemlerde</summary>

[DeskPort-0.7.3-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-client-x86_64.flatpak) dosyasını indirin,
ardından indirme dizininde bir terminal açın:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user ./DeskPort-0.7.3-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

Yerel paket Freedesktop Platform 25.08 çalışma zamanını indirebilir. Sunshine ana bilgisayarı veya masaüstü
oturum hizmeti entegrasyonu olmadan yalnızca görüntüleme sağlar. Gelecekte indirilen paketleri sağlama
toplamlarını doğruladıktan sonra `flatpak install --user --or-update ./NEW_FILENAME.flatpak` ile kurun.
`flatpak uninstall --user io.github.keithxc.DeskPort` ile kaldırın. Bu bir GitHub sürüm paketidir; kurulum
için Flathub'da DeskPort kaydı gerekmez.

</details>

Ana bilgisayar olmak için normal oturum açılmış grafik oturumu kullanın ve
[yakalama ve girdi önkoşullarını](../LINUX_PACKAGES.md#first-connection-and-hosting) yapılandırın.
İsteğe bağlı oturum açılışında başlatmayı etkinleştirmeden önce paylaşımı hedeflediğiniz LAN veya VPN'deki
bir istemciyle test edin. Paket kurulumu ağ erişilebilirliğini yapılandırmaz veya ana bilgisayar girdi
erişimi vermez. Paketi kaldırmadan önce paylaşımı durdurun ve DeskPort'tan çıkın; etkinleştirdiyseniz oturum
açılışında başlatmayı kapatın. Paket kaldırma, ana dizininizdeki DeskPort ayarlarını korur.

### Linux depo durumu

| Kanal | Durum | Kurulum |
| --- | --- | --- |
| GitHub sürüm paketleri | 0.7.3 kararlı | Yukarıdan dağıtımınıza uygun paketi indirin |
| Üst kaynak Nix flake | 0.7.3 kararlı | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Tarif test edildi; yayın, yeni hesap kaydı duraklaması nedeniyle engellendi | [Tarifi yerelde derleyin](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) incelemeye gönderildi; birleştirilmedi | Dahil edilene kadar üst kaynak flake'i kullanın |
| Resmî Arch depoları / Flathub | Listelenmiyor | Sürüm indirmelerini kullanın; Flatpak yalnızca görüntüleyicidir |

İndirilebilir Arch paketi `pacman -U` ile kurulur; `pacman -S deskport` komutunu kullanılabilir yapmaz.
Benzer şekilde, üst kaynak flake Nixpkgs'te `pkgs.deskport` sağlamaz. Ana bilgisayar girdisi, yakalama
izinleri ve güvenlik duvarı ayarı uygulama kurulumundan ayrıdır; bkz.
[Linux kurulum kılavuzu](../LINUX_PACKAGES.md#nixos-and-nix).

## Benzer ürünlerle karşılaştırma

Bu bir iş akışı ve platform karşılaştırmasıdır; bağlantısı verilen resmî belgelerle **2026-09-20**
tarihinde kontrol edilmiştir ve bir gecikme veya görüntü kalitesi kıyaslaması değildir. Ürün özellikleri
ve platform sınırları değişebilir.

| Konu | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Ana iş akışı | Kalıcı uzak masaüstü çalışma alanı, uyarlanabilir boyut ve cihaz devri | Ayrı istemci ve ana bilgisayar uygulamalarıyla oyun/masaüstü akışı | Genel uzaktan kontrol ve destek | Etkileşimli uzak masaüstü ve iş birliği |
| Masaüstü ana bilgisayar platformları | macOS Apple Silicon, Linux x86_64 ve Windows x64 | Platform sınırlarıyla Windows, macOS, Linux ve FreeBSD | Windows, macOS ve Linux | Windows ve macOS; Linux ana bilgisayar olamaz |
| Masaüstü istemcileri | macOS Apple Silicon, Linux x86_64 ve Windows x64 | Diğerlerinin yanı sıra Windows, macOS ve Linux | Windows, macOS ve Linux | Windows, macOS, Linux ve desteklenen Chromium tarayıcıları |
| Mobil istemciler | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS ve Android | iOS/iPadOS ve Android; iOS ana bilgisayar olamaz | Android; iOS/iPadOS istemcisi yok |
| Kurulum modeli | Yerel masaüstü paketlerinde paketlenmiş ana bilgisayar; cihazları DeskPort'ta onaylayın | Sunshine'ı ayrı kurup yapılandırın, ardından Moonlight'ı eşleştirin | Herkese açık sunucu altyapısı veya kendi barındırdığınız sunucu | Parsec hesabı ve uygulaması |
| Ağ/hizmet modeli | Kendi LAN/VPN erişiminiz; DeskPort aktarma sunucusu yok | Kendi barındırdığınız akış; ağ erişimini yapılandırın | Herkese açık altyapı veya kendi barındırdığınız OSS/Pro sunucu | Parsec hesap/hizmet altyapısı |
| Kaynak ve dağıtım | GPL masaüstü, ücretsiz masaüstü paketleri; ücretli Apple uygulaması ayrı dağıtılır | Açık kaynak istemciler ve ana bilgisayar | Açık kaynak istemci ve OSS sunucu; ticari Pro sunucu seçeneği | Tescilli uygulama/hizmet |

Kaynaklar: [Moonlight](https://moonlight-stream.org/),
[Sunshine kurulumu ve platform desteği](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk istemcileri](https://rustdesk.com/docs/en/client/),
[RustDesk kendi sunucunda barındırma](https://rustdesk.com/docs/en/self-host/),
[Parsec platform uyumluluğu](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec Linux kurulumu](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
DeskPort'un güncel platform sınırları yukarıda listelenmiştir; bu tablo her işletim sisteminde özellik
eşitliği veya ölçülmüş bir performans üstünlüğü anlamına gelmez.

Bkz. [0.7.3 sürüm notları](../releases/RELEASE_0.7.3.md),
[mimari](../ARCHITECTURE.md) ve [yol haritası](../ROADMAP.md).

## Linux'ta derleme ve çalıştırma

Nix ve flake'ler etkinken:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

Görüntüleyicinin üçüncü taraf kaynak bağımlılıkları depoya dahildir; ortak çekirdek ve Nix derleme
girdileri sabitlenmiş harici bağımlılıklar olarak kalır. Yerel derlemeler `shared/deskport-core`
alt modülünü başlatmalıdır. Bkz. [docs/VENDORED.md](../VENDORED.md).
SSH kurulumu, yerel ana bilgisayar yönetimi, VPS önkoşulları ve Omarchy/Hyprland için
[CLI kılavuzuna](../CLI.md) bakın. `deskport doctor` ana bilgisayar önkoşullarını kontrol eder;
`deskport status --json` pencere açmadan çalışan örneği inceler.
`nix run . -- --help` komut satırı arayüzünü listeler. Bağlanmadan önce ana bilgisayarda Paylaşımı
Başlat'ı seçin ve cihazları eşleyin. Eski Sunshine PIN eşleştirmesi de kullanılabilir. Hiçbir kişisel
ana bilgisayar veya eşleştirme kimlik bilgisi dahil edilmez ya da Moonlight'tan içe aktarılmaz.
Yeni el ile girilen adresler varsayılan olarak DeskPort'un `48989` bağlantı noktasını kullanır. Farklıysa
ana bilgisayarın paylaşım sayfasında gösterilen bağlantı noktasını ekleyin veya varsayılan bağımsız bir
Sunshine kurulumuna açıkça bağlanmak için `host:47989` kullanın. Kaydedilmiş/bulunmuş uç noktalar kendi
bağlantı noktalarını korur.

Düzenlenebilir yerel derleme için:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

Çatalın incelenebilir kalması için üst kaynak proje dosya adları değiştirilmemiştir.

## Moonlight'tan farkı nedir?

- Cihaz onayı, devralma ve cihaz başına ayarlarla paketlenmiş Sunshine ana bilgisayarı.
- Görüntüleyici gizliyken oturumlar bağlı kalır; tepsiden/menü çubuğundan geri çağırın.
- Masaüstü ana bilgisayarlarının sunduğu HTTPS tarayıcı erişimi.
- Ayrı `deskport` yürütülebilir dosyası, `DeskPort` Qt ayar ad alanı ve
  `io.github.keithxc.DeskPort` Linux uygulama kimliği.
- Varsayılan olarak pencereli akış ve mutlak işaretçi kontrolü.
- Odak kaybında sessize alma; oyun optimizasyonu, gamepad faresi, çoklu denetleyici modu,
  arka planda gamepad girdisi ve Discord durumu varsayılan olarak kapalı.
- Bu bağımsız uygulama için üst kaynak Moonlight güncelleme istemleri yok.
- Kilitli bir Nix ortamı ve Linux derleme iş akışı.

Masaüstü arayüzü dil seçimi ve ayrı ana bilgisayar izinleriyle cihaz, paylaşım ve ayar sayfaları sunar.
Görüntüleyiciyi kapatmak oturumu kullanılabilir tutar ve cihaz listesini açar. Etkin bir cihaz Masaüstüne
Dön seçeneği sunar; diğer cihazlar mevcut oturum kesilene kadar ayrıntıları gösterir. Sık kullanılan
cihazları sabitleyin ve kompakt liste veya kart görünümünü seçin. Görünüm sistemi izler; açık ve koyu tema
zorlanabilir.

## Platform kapsamı

Platform başına sürüm durumu için yukarıdaki [platform tablosuna](#desteklenen-platformlar) bakın. Masaüstü
sürümleri macOS Apple Silicon, Linux x86_64 ve Windows x64'ü hem görüntüleyici hem ana bilgisayar olarak
kapsar; mobil uygulamalar yalnızca istemcidir.

İstemci ve ana bilgisayar desteği ayrıdır: Mac ana bilgisayarı bir DeskPort Mac istemcisi gerektirmez.
Donanım kod çözme, canlı girdi, görüntü kalitesi ve geri çağırma gecikmesi gerçek oturum testleri gerektirir;
başarılı bir derleme bunları kanıtlamaz.

## Sonraki kilometre taşı

Tek bir oturumu **50 gizle/göster döngüsü** boyunca bağlı tutmak, pencereyi geçerli çalışma alanında
göstermek ve yerel girdiyi güvenilir biçimde geri vermek. Taze kare gecikmesini ve arka plan kaynak
kullanımını pencerenin görünmesinden ayrı ölçmek.

Kabul ölçütleri ve ertelenen özellikler için [yol haritasına](../ROADMAP.md), köken ve bakım sınırları
için [üst kaynak notlarına](../UPSTREAM.md) bakın.

## Doğrulama

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

Duman testi geçici XDG yapılandırma/önbellek dizinleri ve ekran dışı bir Qt platformu kullanır.
Bir ana bilgisayarla eşleşmez, akış başlatmaz veya girdi enjekte etmez.

## Günlük masaüstü kontrolleri

Bir pencereyi kapatmak DeskPort'u tepside/menü çubuğunda çalışır durumda bırakır. Geri çağırmak için
**DeskPort'u Aç**, yalnızca mevcut bağlantıyı sonlandırmak için **Görüntüleyiciyi Kes**, hizmetten çıkmak
için **DeskPort'tan Çık** seçeneğini kullanın. Bir görüntüleyici kapandığında yerel paylaşım sürer.

Düz metin pano paylaşımı ve sistem kısayolu yakalama, eşleşmiş iki cihazda da varsayılan olarak açıktır.
Ayar değişiklikleri yeniden bağlandıktan sonra uygulanır. Yalnızca yeni kopyalamalar, 1 MiB'ye kadar
paylaşılır. Desteklenen görseller ve dosyalar, katılmayı seçmiş uyumlu masaüstü eşleri arasında istendiğinde
alınır; bkz. [sınırlar](../CLIPBOARD.md). Masaüstü işaretçi modunda klavye yönlendirmesi odaklanmış video
içindeki işaretçiyi izler.
**Ctrl+Alt+Shift+Z** girdiyi bırakır;
**Ctrl+Alt+Shift+Q** görüntüleyiciyi keser. Açıkça bıraktıktan sonra girdiyi geri almak için içeri tıklayın.
İşletim sistemine ayrılmış kısayollar masaüstü birleştiricisine bağlıdır.

Oturum açılışında başlatma ve kurtarma, etkin bir grafik oturum gerektirir. Gözetimsiz erişim için bir
bilgisayara güvenmeden önce [kabul kontrollerine ve sınırlamalara](../INPUT_SERVICE_ACCEPTANCE.md) bakın.

## Lisans ve atıf

DeskPort, başlangıçta v6.1.0'a dayanan, [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt)
projesinin bağımsız bir türevidir. Resmî bir Moonlight veya Sunshine sürümü değildir.
Moonlight akış temelini sağlar; [Sunshine](https://github.com/LizardByte/Sunshine)
macOS, Windows ve Linux ana bilgisayar paketlerinde (yalnızca görüntüleyici olan Flatpak hariç) paketlenir
ve Linux Nix paketiyle sağlanır. Ayrı kurulmuş Sunshine hizmetleri bağımsız tutulur.

GPL-3.0-or-later; bkz. [LICENSE](../../LICENSE), korunan kaynak bildirimleri ve
[docs/VENDORED.md](../VENDORED.md) içinde listelenen her dahil bağımlılığın lisansı. Özgün belgeler
[README.upstream.md](README.upstream.md) içinde korunur.

### Tanılama ve geri bildirim

Tanılama günlükleri varsayılan olarak açıktır. Sorunu yeniden üretin, ardından bir günlük ZIP'i oluşturun
ve bir GitHub sorun taslağı açın. Eklemeden önce ZIP'i gözden geçirin: GitHub sorunları herkese açıktır ve
DeskPort sizin adınıza hiçbir şey yüklemez veya göndermez. Bkz.
[tanılama verileri ve sınırlar](../DIAGNOSTICS.md).
