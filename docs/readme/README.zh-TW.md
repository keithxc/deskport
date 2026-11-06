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
  <b>繁體中文</b> ·
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
  <a href="README.id.md">Bahasa Indonesia</a> ·
  <a href="README.ms.md">Bahasa Melayu</a> ·
  <a href="README.th.md">ไทย</a> ·
  <a href="README.vi.md">Tiếng Việt</a> ·
  <a href="README.bo.md">བོད་ཡིག</a>
</p>

<p align="center">
  以 Moonlight 與 Sunshine 為基礎的遠端桌面工作區。<br>
  讓遠端桌面常駐背景，一個動作就把它叫到目前的工作區，
  收起時也不必重新連線。
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="桌面版本" src="https://img.shields.io/badge/desktop-0.7.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="../../LICENSE"><img alt="授權條款" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## 主要功能

- **一個桌面 App，既能檢視也能分享。** 內建 Sunshine、裝置核准、獨立的憑證與設定；
  macOS、Windows 與 Linux 主機安裝套件不必另外安裝 Sunshine（僅供檢視的 Flatpak 除外）。
- **工作區跟著你的用戶端走。** 在相容的 macOS 與 KDE 主機上自動調整大小，支援 HiDPI、
  行動裝置直橫向版面與個別裝置的尺寸設定。調整大小時視訊會短暫重新連線，並非無縫切換編碼器。
- **選擇主機的顯示策略。** 虛擬主螢幕並鏡像其他螢幕、虛擬主螢幕並關閉其他螢幕，或在符合條件的
  主機上使用延伸工作區。工作階段結束時，DeskPort 會還原已儲存的螢幕配置。
- **在已授權的裝置之間切換。** 接管進行中的工作階段前會先確認；被取代的用戶端會收到經過驗證的接管說明。
- **桌面工作隨手可得。** 隱藏或叫回已連線的檢視器，隱藏時釋放遠端輸入，
  也可以在不影響本機分享的情況下單獨中斷檢視。
- **每台電腦分別儲存設定。** 畫面、音訊、輸入、桌面微調與位址編輯都跟著裝置保存；
  另有卡片、釘選、主題與多語系介面。
- **視需要分享桌面剪貼簿。** 文字即時更新；在已綁定且啟用此選項的桌面裝置之間，
  支援的圖片與檔案會依需求取得。請參閱[剪貼簿範圍與限制](../CLIPBOARD.md)。
- **原生行動裝置操控。** iPhone/iPad 與開發中的 Android 用戶端提供觸控/觸控板輸入、
  鍵盤操控，並可直接進入遠端桌面。
- **可選的登入時啟動與 macOS 復原。** 需要有效的圖形登入工作階段並取得系統授權；
  它們不會解鎖 FileVault，也不會替你登入。

## 介面預覽

目前介面的截圖，使用的是示範裝置。點選即可查看大圖；這些預覽不代表已在商店上架或已正式發布。

| 裝置與群組 | 個別裝置顯示設定 | 內建使用手冊 |
| --- | --- | --- |
| [![裝置與群組](../media/devices.png)](../media/devices.png) | [![個別裝置顯示設定](../media/device-settings.png)](../media/device-settings.png) | [![內建使用手冊](../media/manual.png)](../media/manual.png) |

## 支援的平台

下方為 0.7.3 正式版連結。套件檢查與執行時驗證各有範圍，結果及未驗證項目請見[驗證報告](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)。

| 平台 | 角色 | 發布狀態 | 下載 |
| --- | --- | --- | --- |
| macOS Apple Silicon，macOS 26+ | 檢視端 + 主機端 + 原生虛擬顯示器 | **0.7.3 正式版**；Developer ID 簽署、Apple 公證與 ZIP 解壓後的 Gatekeeper 檢查通過 | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | 檢視端 + 主機端；符合條件的 KWin、Mutter 與 Hyprland 主機支援自動調整的虛擬工作區 | **0.7.3 正式版**：DEB、RPM、Arch、AppImage、可攜式封存檔與 Nix | [發布頁面](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [安裝指南](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | 僅檢視端 | **0.7.3 正式版**，Freedesktop Platform 25.08 | [發布頁面](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad，iOS/iPadOS 15+ | 僅用戶端 | 已在 App Store 上架；較新的版本另於 TestFlight 測試 | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | 僅用戶端 | **2.7** · Google Play 封閉測試待完成 | APK 待發布 |
| Windows x64 | 檢視端 + 主機端 + 虛擬顯示器 | **0.7.3 正式版**；安裝程式包含主機，可攜式 ZIP 僅為檢視端；未安裝狀態的套件與 CLI 驗證通過；應用程式與安裝程式未簽署 | [安裝程式](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | 未經驗證 | 沒有經過驗證的發布套件 | — |

> **Windows 防毒軟體提醒：** 防毒軟體有可能將 DeskPort 的 Windows 輔助元件誤判為威脅。
> 我們自行測試時並未遇到這種情況。請保持 Defender 開啟；如果出現警告，
> 請查看發布附帶的驗證報告與[樣本紀錄](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice)。

原生 Linux 套件需要 glibc 2.39 以上：Ubuntu 24.04+/Debian 13+、Fedora 44、
最新版 Arch。AppImage 內附私有執行環境，可用於 glibc 較舊的桌面；主機功能仍取決於硬體驅動程式
與合成器支援。KDE 虛擬顯示器主機需要合成器授權項目。原生套件與 Nix 套件提供固定的項目；
AppImage 與可攜版會在開始分享時自動註冊目前顯示輔助程式的路徑。主機輸入仍需要裝置權限。
行動 App 無法作為桌面主機。
只有套件定義或能成功編譯，並不代表該平台受到支援。

## 安裝

**macOS：** 開啟[DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg)，
把 DeskPort 拖進「應用程式」後開啟。若要使用主機端功能，請授予「螢幕錄製」與
「輔助使用」權限。不需要 Nix 或 Homebrew。

**Linux：** 選擇適合你系統的 [0.7.3 套件](https://github.com/keithxc/deskport/releases/tag/v0.7.3)，
並依照[安裝指南](../LINUX_PACKAGES.md)操作，包括主機輸入權限的設定。使用 Nix：`nix run github:keithxc/deskport/v0.7.3`。
Flatpak 僅提供檢視功能。發布檔案附有 [SHA-256 檢查碼](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt)與[驗證報告](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)。

**Android 版 DeskPort** 尚未完成 Google Play 正式上架所需的封閉測試。上架前，我們計劃透過 [GitHub Releases](https://github.com/keithxc/deskport/releases) 暫時提供已簽署的早期版本 APK。iOS 和 Android 用戶端的原始碼儲存庫均為私有，此儲存庫公開的是桌面端原始碼。

**iPhone / iPad：** 從 [App Store 安裝 DeskPort](https://apps.apple.com/us/app/deskport/id6812389978)。
行動 App 為一次性付費購買，實際價格請以你所在地區的商店為準。
桌面版永遠免費並維持開源。你的購買會用於程式碼簽署、測試硬體與後續開發。

在主機上開始分享，在用戶端選擇該主機，然後在主機上核准這台裝置。兩台裝置之間的網路必須互通：
使用區域網路或你自己的 VPN（例如 Tailscale）。DeskPort 不提供帳號服務、代管桌面、中繼
或網路通道。傳統的 Sunshine PIN 配對仍然可用。

**0.7.3 正式版：** 本次整合了 22 種語言的共用介面目錄與瀏覽器語言切換、Windows 已登入工作階段的分享與虛擬顯示器 DPI、Linux 執行環境與 Wayland 剪貼簿、受保護的 Hyprland/X11 工作區、可選的本機輸入法，以及正方形裝置卡片的改進。 變更與驗證範圍請見 [0.7.3 版本說明](../releases/RELEASE_0.7.3.md)。

**0.7.3 驗證範圍：** Windows 最終套件內容一致性、PE、原生 CLI 檢查通過，Defender 掃描零檢出；未進行安裝、GUI 連線或串流驗收。最終 AppImage 通過 Bazzite 虛擬機中的正常 FUSE 與軟體 GPU 檢查。Ubuntu 26.04 虛擬機重現了 0.7.2 DEB 的 VA 當機，並確認最終 0.7.3 DEB 已修正且升級通過。回復舊版重現了原有當機，隨後解除安裝仍保留了合成測試設定。GNOME 擷取與恢復仍未通過驗收。容器、Flatpak 結果及其他限制請見[驗證報告](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)；這些結果不代表完整的實體硬體驗收。

**歷史版本：0.7.2 穩定版：** [桌面版本](https://github.com/keithxc/deskport/releases/tag/v0.7.2)整合了調整大小、遠端串流、省流量與 Linux 執行環境方面的改進，並修正了 Bazzite/KDE Wayland 上的 AppImage 問題。詳見 [0.7.2 版本說明](../releases/RELEASE_0.7.2.md)；各 Linux 發行版的安裝步驟請見[英文 README](../../README.md#linux-installation-by-distribution)。

**瀏覽器存取：** 桌面主機提供 HTTPS 遠端桌面，支援 30 秒登入碼、可選的七天瀏覽器配對、
自動調整大小與全螢幕。請參閱 [0.7.1 版本說明](../releases/RELEASE_0.7.1.md)與
[瀏覽器使用指南](../BROWSER_ACCESS.md)。AppImage 內含私有的 glibc 執行環境；
原生套件仍需要 glibc 2.39。

### Linux 軟體庫狀態

| 管道 | 狀態 | 安裝方式 |
| --- | --- | --- |
| GitHub 發布套件 | 0.7.3 正式版 | 在上方下載適合你發行版的套件 |
| 上游 Nix flake | 0.7.3 正式版 | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | 配方已測試；因 AUR 暫停新帳號註冊而無法發布 | [在本機建置配方](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | 已提交 [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) 等待審查；尚未合併 | 合併前請使用上游 flake |
| Arch 官方軟體庫 / Flathub | 未收錄 | 請從發布頁面下載；Flatpak 僅為檢視端 |

可下載的 Arch 套件以 `pacman -U` 安裝，並不代表可以使用
`pacman -S deskport`。同樣地，上游 flake 也不會讓 Nixpkgs 出現
`pkgs.deskport`。主機輸入、擷取權限與防火牆設定都和 App 安裝分開處理；請參閱
[Linux 安裝指南](../LINUX_PACKAGES.md#nixos-and-nix)。

## 與同類產品比較

這是工作流程與平台的比較，依據所附連結的官方文件於 **2026-09-20** 查核，
並非延遲或畫質評測。各產品的功能與平台限制可能會變動。

| 面向 | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| 主要工作流程 | 常駐的遠端桌面工作區、自動調整大小與跨裝置接管 | 透過各自獨立的用戶端與主機 App 進行遊戲/桌面串流 | 一般遠端控制與技術支援 | 互動式遠端桌面與協作 |
| 桌面主機平台 | macOS Apple Silicon、Linux x86_64 與 Windows x64 | Windows、macOS、Linux 與 FreeBSD，依各平台限制而定 | Windows、macOS 與 Linux | Windows 與 macOS；Linux 無法作為主機 |
| 桌面用戶端 | macOS Apple Silicon、Linux x86_64 與 Windows x64 | Windows、macOS、Linux 等 | Windows、macOS 與 Linux | Windows、macOS、Linux 與支援的 Chromium 瀏覽器 |
| 行動用戶端 | iPhone/iPad 已發布；Android 開發中 | iOS/iPadOS 與 Android | iOS/iPadOS 與 Android；iOS 無法作為主機 | Android；沒有 iOS/iPadOS 用戶端 |
| 設定方式 | 原生桌面套件內建主機；在 DeskPort 中核准裝置 | 另外安裝並設定 Sunshine，再與 Moonlight 配對 | 公用伺服器或自架伺服器 | Parsec 帳號與 App |
| 網路/服務模式 | 自備區域網路/VPN 連線；沒有 DeskPort 中繼 | 自架串流；需自行設定網路連通 | 公用基礎架構或自架 OSS/Pro 伺服器 | Parsec 帳號/服務基礎架構 |
| 原始碼與發行 | GPL 桌面版，桌面套件免費；付費 Apple App 另行發行 | 開源用戶端與主機 | 開源用戶端與 OSS 伺服器；另有商業 Pro 伺服器 | 專有 App/服務 |

資料來源：[Moonlight](https://moonlight-stream.org/)、
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html)、
[RustDesk clients](https://rustdesk.com/docs/en/client/)、
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/)、
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility)、
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux)。
DeskPort 目前的平台限制列於上方；此表並不表示各作業系統的功能完全相同，
也不代表經過量測的效能優勢。

請參閱 [0.7.3 版本說明](../releases/RELEASE_0.7.3.md)、
[架構說明](../ARCHITECTURE.md)與[開發藍圖](../ROADMAP.md)。

## 在 Linux 上建置與執行

啟用 Nix 與 flakes 後：

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

檢視端的第三方原始碼相依套件已內建（vendored）；共用核心與 Nix 建置輸入仍是鎖定版本的外部相依項目。
原生建置需要初始化 `shared/deskport-core`。請參閱 [docs/VENDORED.md](../VENDORED.md)。
SSH 設定、本機主機管理、VPS 先決條件以及 Omarchy/Hyprland 請參閱
[命令列指南](../CLI.md)。`deskport doctor` 會檢查主機運作的先決條件；
`deskport status --json` 不必開啟視窗就能查看執行中的實例。
`nix run . -- --help` 會列出命令列介面。連線前請先在主機上開始分享並綁定裝置。傳統的
Sunshine PIN 配對同樣可用。本專案不包含、也不會從 Moonlight 匯入任何個人主機或配對憑證。
新增的手動位址預設使用 DeskPort 的連接埠 `48989`。若不同，請輸入主機分享頁面上顯示的連接埠；
若要明確連線到預設的獨立 Sunshine 安裝，可使用 `host:47989`。已儲存或探索到的端點會保留各自的連接埠。

可編輯的原生建置：

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

上游專案的檔名維持不變，讓這個分支依然方便審閱。

## 和 Moonlight 有什麼不同？

- 內建 Sunshine 主機端，支援裝置核准、接管與個別裝置設定。
- 檢視器隱藏時工作階段保持連線；可從系統匣/選單列叫回。
- 桌面主機提供 HTTPS 瀏覽器存取。
- 獨立的 `deskport` 執行檔、`DeskPort` Qt 設定命名空間，以及
  `io.github.keithxc.DeskPort` Linux 應用程式 ID。
- 預設以視窗模式串流並使用絕對指標控制。
- 失去焦點時靜音；預設關閉遊戲最佳化、手把滑鼠、多手把模式、背景手把輸入與 Discord 狀態。
- 這個獨立 App 不會顯示上游 Moonlight 的更新提示。
- 鎖定版本的 Nix 環境與一套 Linux 建置流程。

桌面介面提供裝置、分享與設定頁面，包含語言選擇與獨立的主機權限。關閉檢視器會保留其工作階段
並開啟裝置清單。使用中的裝置會提供「返回桌面」，其他裝置在目前工作階段中斷前只顯示詳細資訊。
可以釘選常用裝置，並在精簡清單與卡片之間切換。外觀跟隨系統，也可強制使用淺色或深色。

## 平台範圍

各平台的發布狀態請見上方的[平台表](#支援的平台)。桌面版涵蓋 macOS Apple Silicon、Linux x86_64
與 Windows x64，皆可作為檢視端與主機端；行動 App 僅為用戶端。

用戶端平台支援與主機端支援是彼此獨立的：Mac 主機不需要使用 DeskPort 的 Mac 用戶端。
硬體解碼、即時輸入、畫質與叫回延遲都需要實際工作階段測試，建置成功並不能證明這些。

## 下一個里程碑

讓一個工作階段在 **50 次隱藏/顯示循環**中保持連線，把視窗顯示在目前的工作區，並穩定地交還本機輸入。
新畫面的延遲與背景資源用量，要和視窗出現時間分開量測。

驗收標準與延後的功能請見[開發藍圖](../ROADMAP.md)，來源與維護範圍請見
[上游說明](../UPSTREAM.md)。

## 驗證

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

冒煙測試使用暫時的 XDG 設定/快取目錄與離屏 Qt 平台。它不會與主機配對、啟動串流或注入輸入。

## 日常桌面操作

關閉視窗後，DeskPort 仍會在系統匣/選單列執行。用 **Open DeskPort** 叫回它，用 **Disconnect viewer** 只結束
目前的連線，或用 **Quit DeskPort** 結束整個服務。檢視器關閉時，本機分享會繼續執行。

雙方已綁定的裝置預設啟用純文字剪貼簿分享與系統快速鍵擷取。設定變更會在重新連線後生效。只會分享新的
複製內容，上限 1 MiB。相容且已啟用此選項的桌面裝置之間，可依需求取得支援的圖片與檔案；請參閱[限制](../CLIPBOARD.md)。
在桌面指標模式下，鍵盤輸入會跟著位於焦點視訊內的指標。
**Ctrl+Alt+Shift+Z** 釋放輸入；
**Ctrl+Alt+Shift+Q** 中斷檢視器。明確釋放後，點一下畫面內部即可重新取得輸入。
系統保留的快速鍵取決於桌面合成器。

登入時啟動與復原需要有效的圖形登入工作階段。在依賴某台電腦進行無人值守存取之前，請先閱讀
[驗收檢查與限制](../INPUT_SERVICE_ACCEPTANCE.md)。

## 授權與致謝

DeskPort 是 [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) 的獨立衍生專案，
最初以 v6.1.0 為基礎，並非 Moonlight 或 Sunshine 的官方發行版。Moonlight 提供串流基礎；
[Sunshine](https://github.com/LizardByte/Sunshine) 已內建於 macOS、Windows 與 Linux 主機套件（僅供檢視的 Flatpak 除外），
並由 Linux Nix 套件提供。另外安裝的 Sunshine 服務維持獨立。

GPL-3.0-or-later；請參閱 [LICENSE](../../LICENSE)、保留的原始碼聲明，以及
[docs/VENDORED.md](../VENDORED.md) 中列出的各內建相依套件授權條款。原始文件保留在
[README.upstream.md](README.upstream.md)。

### 診斷與意見回饋

診斷紀錄預設為開啟。重現問題後，產生紀錄 ZIP 並開啟 GitHub issue 草稿。附加前請先檢查 ZIP 內容：
GitHub issue 是公開的，DeskPort 不會替你上傳或提交任何內容。
請參閱[診斷資料與限制](../DIAGNOSTICS.md)。
