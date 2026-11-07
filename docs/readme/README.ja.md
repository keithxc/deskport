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
  <b>日本語</b> ·
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
  Moonlight と Sunshine をベースにしたリモートデスクトップ・ワークスペース。<br>
  リモートデスクトップをバックグラウンドに常駐させ、ワンアクションで今のワークスペースに呼び出し、
  再接続せずにまた隠せます。
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="デスクトップ版" src="https://img.shields.io/badge/desktop-0.7.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="ライセンス" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## 主な機能

- **閲覧も共有もひとつのデスクトップアプリで。** Sunshine 内蔵、デバイス承認、独立した認証情報と設定。
  macOS・Windows・Linux のホスト用パッケージでは Sunshine を別途インストールする必要はありません（閲覧専用の Flatpak を除く）。
- **クライアントに合わせて変わるワークスペース。** 対応する macOS / KDE ホストでのアダプティブなサイズ調整、HiDPI、
  モバイルの縦横レイアウト、デバイスごとのサイズ設定。サイズ変更時は映像が一瞬再接続されます（シームレスなエンコーダ切り替えではありません）。
- **ホストの表示ポリシーを選択。** 仮想メインディスプレイ＋他画面のミラー、仮想メインディスプレイ＋他画面オフ、
  または条件を満たすホストでの拡張ワークスペース。セッション終了時に DeskPort が保存済みのレイアウトを復元します。
- **承認済みデバイス間で切り替え。** 進行中のセッションを引き継ぐ前に確認し、
  引き継がれた側のクライアントには認証済みの引き継ぎ理由が表示されます。
- **デスクトップ作業をすぐ手元に。** 接続中のビューアを隠したり呼び出したりでき、隠している間はリモート入力を解放し、
  ローカル共有とは別に閲覧だけを切断できます。
- **コンピュータごとに設定を保存。** 画質、音声、入力、デスクトップの微調整、アドレス編集はデバイスごとに保持。
  カード表示、ピン留め、テーマ、ローカライズされた UI。
- **必要なときにデスクトップのクリップボードを共有。** テキストは即時に反映され、
  オプションを有効にしたバインド済みデスクトップ間では対応する画像やファイルをオンデマンドで取得します。
  [クリップボードの範囲と制限](../CLIPBOARD.md)を参照してください。
- **ネイティブなモバイル操作。** iPhone/iPad とAndroid クライアントで、
  タッチ／トラックパッド入力、キーボード操作、リモートデスクトップへの直接移動が可能です。
- **オプションのログイン時起動と macOS 復元。** アクティブなグラフィカルログインとシステムの承認が必要です。
  FileVault のロック解除やログインを代わりに行うものではありません。

## 画面プレビュー

デモ用の架空デバイスを使った現在の画面のスクリーンショットです。クリックで拡大表示できます。
これらのプレビューはストア公開やリリースを意味するものではありません。

| デバイスとグループ | デバイスごとの表示設定 | 内蔵マニュアル |
| --- | --- | --- |
| [![デバイスとグループ](../media/devices.png)](../media/devices.png) | [![デバイスごとの表示設定](../media/device-settings.png)](../media/device-settings.png) | [![内蔵マニュアル](../media/manual.png)](../media/manual.png) |

## 対応プラットフォーム

以下は正式版 0.7.3 のリンクです。パッケージ検証と実行時の検証は対象範囲が異なります。結果と未検証項目は[検証レポート](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)をご覧ください。

| プラットフォーム | 役割 | リリース状況 | ダウンロード |
| --- | --- | --- | --- |
| macOS Apple Silicon、macOS 26+ | ビューア＋ホスト＋ネイティブ仮想ディスプレイ | **0.7.3 正式版**。Developer ID 署名、Apple 公証、ZIP 展開後の Gatekeeper 検証に合格 | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | ビューア＋ホスト。条件を満たす KWin・Mutter・Hyprland ホストではアダプティブな仮想ワークスペース | **0.7.3 正式版**：DEB、RPM、Arch、AppImage、ポータブルアーカイブ、Nix | [リリース](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [インストールガイド](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | ビューアのみ | **0.7.3 正式版**、Freedesktop Platform 25.08 | [リリース](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad、iOS/iPadOS 15+ | クライアントのみ | App Store で公開中。新しいビルドは TestFlight で別途テスト | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | クライアントのみ | **2.7 (63)** · Google Play のクローズド テスト中 | [APK をダウンロード](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | ビューア＋ホスト＋仮想ディスプレイ | **0.7.3 正式版**。インストーラーはホストを含み、ポータブル ZIP はビューア専用。未インストール状態のパッケージと CLI を検証済み。アプリとインストーラーは未署名 | [インストーラー](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | 未検証 | 検証済みのリリースパッケージなし | — |

> **Windows のウイルス対策に関する注意：** ウイルス対策ソフトが DeskPort の Windows ヘルパーコンポーネントを誤検知する可能性があります。
> 私たちのテストではこの現象は確認されていません。Defender は有効のままにし、警告が表示された場合は
> リリースの検証レポートと[サンプル履歴](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice)を確認してください。

ネイティブ Linux パッケージには glibc 2.39 以上が必要です：Ubuntu 24.04+/Debian 13+、Fedora 44、
最新の Arch。AppImage は古い glibc のデスクトップ向けにプライベートなランタイムを同梱していますが、
ホスト機能は引き続きハードウェアドライバーとコンポジタの対応に左右されます。KDE の仮想ディスプレイホストには
コンポジタの許可エントリが必要です。ネイティブ／Nix パッケージは固定のエントリを提供し、
AppImage とポータブル版は共有開始時に現在のディスプレイヘルパーのパスを自動登録します。
ホスト入力には引き続きデバイス権限が必要です。モバイルアプリはデスクトップをホストしません。
ソース定義があることやビルドに成功したことだけでは、対応プラットフォームとはみなしません。

## インストール

**macOS：** [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) を開き、
DeskPort を「アプリケーション」にドラッグして起動します。ホスト機能を使うには「画面収録」と
「アクセシビリティ」の権限を許可してください。Nix や Homebrew は不要です。

**Linux：** お使いのシステムに合った [0.7.3 パッケージ](https://github.com/keithxc/deskport/releases/tag/v0.7.3)を選び、
ホスト入力の権限設定を含めて[インストールガイド](../LINUX_PACKAGES.md)に従ってください。Nix の場合：`nix run github:keithxc/deskport/v0.7.3`。
Flatpak は閲覧のみです。リリースには [SHA-256 チェックサム](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt)と[検証レポート](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)が含まれます。

**Android 2.7 (63)** · Google Play のクローズド テストが完了するまで、GitHub で [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) を配布します。モバイル版のソースコードは非公開です。ここで配布するのはインストーラーのみです。

ビルド 63 は Google Play 版と同じ署名鍵を使用しています。今後、より新しい Play 版に上書き更新すると、パソコンとのペアリングと設定が引き継がれます。GitHub のビルド 62 をお使いの場合は、63 のインストール前に旧版をアンインストールしてください。ローカル設定が消去されるため、再度ペアリングが必要です。

**iPhone / iPad：** [App Store から DeskPort をインストール](https://apps.apple.com/us/app/deskport/id6812389978)してください。
モバイルアプリは買い切りの有料アプリです。現地価格はお使いのストアでご確認ください。
デスクトップ版は今後も無料かつオープンソースです。ご購入は署名、テスト用ハードウェア、
今後の開発に充てられます。

ホストで共有を開始し、クライアントでそのホストを選択して、ホスト側でデバイスを承認します。
両デバイス間でネットワークが到達可能である必要があります：LAN か、Tailscale などご自身の VPN を使ってください。
DeskPort はアカウントサービス、ホスティング型デスクトップ、リレー、ネットワークトンネルを提供しません。
従来の Sunshine PIN ペアリングも引き続き使えます。

**0.7.3 正式版：** 22 言語の共有 UI カタログとブラウザーでの言語切り替え、Windows のログイン済みセッションでの共有と仮想ディスプレイ DPI、Linux ランタイムと Wayland クリップボード、保護付きの Hyprland/X11 ワークスペース、任意で有効にできるローカル IME、正方形のデバイスカードを改善しています。 変更点と検証範囲は [0.7.3 リリースノート](../releases/RELEASE_0.7.3.md)をご覧ください。

**0.7.3 の検証範囲：** Windows では最終パッケージ内容の一致、PE、ネイティブ CLI、Defender の検出ゼロを確認しました。インストール、GUI 接続、ストリーミングは未検証です。最終 AppImage は通常の FUSE とソフトウェア GPU を使う Bazzite VM の検証に合格しました。Ubuntu 26.04 VM では 0.7.2 の DEB で VA クラッシュを再現し、最終 0.7.3 DEB で修正を確認しました。アップグレードは合格しました。ロールバックでは旧版のクラッシュが再現され、その後のアンインストールでも合成テスト設定が保持されました。GNOME のキャプチャと復旧は未受け入れです。コンテナー・Flatpak の結果とその他の制限は[検証レポート](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt)をご覧ください。これらは物理ハードウェア全体の動作確認を意味しません。

**過去のリリース：0.7.2 安定版：** [デスクトップ版](https://github.com/keithxc/deskport/releases/tag/v0.7.2)は、リサイズ、リモートストリーミング、データセーバー、Linux ランタイムの改善をまとめ、Bazzite/KDE Wayland での AppImage の問題を修正しています。詳しくは [0.7.2 リリースノート](../releases/RELEASE_0.7.2.md)を、Linux ディストリビューション別のインストール手順は[英語版 README](../../README.md#linux-installation-by-distribution)をご覧ください。

**ブラウザアクセス：** デスクトップホストは HTTPS のリモートデスクトップを提供し、
30 秒のログインコード、オプションの 7 日間ブラウザペアリング、アダプティブなサイズ調整、
全画面表示に対応しています。[0.7.1 リリースノート](../releases/RELEASE_0.7.1.md)と
[ブラウザガイド](../BROWSER_ACCESS.md)を参照してください。AppImage にはプライベートな glibc ランタイムが含まれ、
ネイティブパッケージは引き続き glibc 2.39 が必要です。

### Linux リポジトリでの提供状況

| チャネル | 状況 | インストール |
| --- | --- | --- |
| GitHub リリースパッケージ | 0.7.3 正式版 | 上記からお使いのディストリビューション用パッケージをダウンロード |
| アップストリーム Nix flake | 0.7.3 正式版 | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | レシピはテスト済み。新規アカウント登録の一時停止により公開できていません | [レシピをローカルでビルド](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) をレビュー待ちで提出済み。未マージ | 取り込まれるまではアップストリームの flake を使用 |
| Arch 公式リポジトリ / Flathub | 未登録 | リリースからダウンロード。Flatpak はビューアのみ |

ダウンロードした Arch パッケージは `pacman -U` でインストールします。これで
`pacman -S deskport` が使えるようになるわけではありません。同様に、アップストリームの flake で
Nixpkgs に `pkgs.deskport` が加わるわけでもありません。ホスト入力、キャプチャ権限、ファイアウォール設定は
アプリのインストールとは別です。[Linux インストールガイド](../LINUX_PACKAGES.md#nixos-and-nix)を参照してください。

## 類似製品との比較

これはワークフローとプラットフォームの比較で、リンク先の公式ドキュメントに基づき **2026-09-20** に確認したものです。
レイテンシや画質のベンチマークではありません。各製品の機能やプラットフォームの制限は変わる可能性があります。

| 観点 | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| 主なワークフロー | 常駐するリモートデスクトップ・ワークスペース、アダプティブなサイズ調整、デバイス間の引き継ぎ | 別々のクライアント／ホストアプリによるゲーム・デスクトップのストリーミング | 汎用のリモート操作とサポート | インタラクティブなリモートデスクトップとコラボレーション |
| デスクトップのホスト | macOS Apple Silicon、Linux x86_64、Windows x64 | Windows、macOS、Linux、FreeBSD（各プラットフォームの制限あり） | Windows、macOS、Linux | Windows、macOS。Linux はホスト不可 |
| デスクトップクライアント | macOS Apple Silicon、Linux x86_64、Windows x64 | Windows、macOS、Linux など | Windows、macOS、Linux | Windows、macOS、Linux、対応 Chromium ブラウザ |
| モバイルクライアント | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS、Android | iOS/iPadOS、Android。iOS はホスト不可 | Android。iOS/iPadOS クライアントなし |
| セットアップ方式 | ネイティブのデスクトップパッケージにホストを内蔵。DeskPort でデバイスを承認 | Sunshine を別途インストール・設定し、Moonlight とペアリング | 公開サーバー基盤またはセルフホストサーバー | Parsec アカウントとアプリ |
| ネットワーク／サービス | LAN／VPN の到達性は各自で用意。DeskPort のリレーなし | セルフホストのストリーミング。ネットワーク到達性を設定 | 公開基盤、またはセルフホストの OSS/Pro サーバー | Parsec のアカウント／サービス基盤 |
| ソースと配布 | GPL のデスクトップ版、無料のデスクトップパッケージ。有料の Apple アプリは別途配布 | オープンソースのクライアントとホスト | オープンソースのクライアントと OSS サーバー。商用 Pro サーバーあり | プロプライエタリなアプリ／サービス |

出典：[Moonlight](https://moonlight-stream.org/)、
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html)、
[RustDesk clients](https://rustdesk.com/docs/en/client/)、
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/)、
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility)、
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux)。
DeskPort の現在のプラットフォーム制限は上記のとおりです。この表はすべての OS での機能の同等性や、
計測された性能面の優位性を意味するものではありません。

[0.7.3 リリースノート](../releases/RELEASE_0.7.3.md)、
[アーキテクチャ](../ARCHITECTURE.md)、[ロードマップ](../ROADMAP.md)も参照してください。

## Linux でのビルドと実行

Nix と flakes を有効にした状態で：

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

ビューアのサードパーティ製ソース依存関係は同梱（vendored）されています。共有コアと Nix のビルド入力は
固定された外部依存のままです。ネイティブビルドでは `shared/deskport-core` の初期化が必要です。
[docs/VENDORED.md](../VENDORED.md) を参照してください。
SSH の設定、ローカルホスト管理、VPS の前提条件、Omarchy/Hyprland については
[CLI ガイド](../CLI.md)を参照してください。`deskport doctor` はホストの前提条件をチェックし、
`deskport status --json` はウィンドウを開かずに実行中のインスタンスを調べます。
`nix run . -- --help` でコマンドラインインターフェースを一覧表示します。接続前にホストで共有を開始し、
デバイスをバインドしてください。従来の Sunshine PIN ペアリングも利用できます。個人のホスト情報や
ペアリング認証情報は含まれておらず、Moonlight からインポートされることもありません。
新しく手動で追加するアドレスは、DeskPort のポート `48989` が既定です。異なる場合はホストの共有ページに
表示されたポートを入力するか、標準の単体 Sunshine に明示的に接続するには `host:47989` を使ってください。
保存済み／検出済みのエンドポイントはそれぞれのポートを保持します。

編集可能なネイティブビルドの場合：

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

フォークをレビューしやすく保つため、アップストリームのファイル名は変更していません。

## Moonlight との違いは？

- デバイス承認、引き継ぎ、デバイスごとの設定に対応した Sunshine ホストを内蔵。
- ビューアを隠している間もセッションは接続されたまま。トレイ／メニューバーから呼び出せます。
- デスクトップホストが提供する HTTPS ブラウザアクセス。
- 独立した `deskport` 実行ファイル、`DeskPort` Qt 設定名前空間、
  `io.github.keithxc.DeskPort` Linux アプリケーション ID。
- 既定でウィンドウ表示のストリーミングと絶対ポインター操作。
- フォーカスを失ったときにミュート。ゲーム最適化、ゲームパッドマウス、マルチコントローラーモード、
  バックグラウンドのゲームパッド入力、Discord プレゼンスは既定でオフ。
- この独立アプリではアップストリーム Moonlight の更新通知を表示しません。
- 固定された Nix 環境と Linux ビルドワークフロー。

デスクトップ UI にはデバイス、共有、設定の各ページがあり、言語選択と独立したホスト権限を備えています。
ビューアを閉じてもセッションは維持され、デバイス一覧が開きます。アクティブなデバイスには「デスクトップに戻る」が表示され、
他のデバイスは現在のセッションが切断されるまで詳細のみを表示します。よく使うデバイスはピン留めでき、
コンパクトな一覧とカードを切り替えられます。外観はシステムに従い、ライト／ダークの固定も可能です。

## プラットフォームの範囲

プラットフォームごとのリリース状況は上の[プラットフォーム表](#対応プラットフォーム)を参照してください。
デスクトップ版は macOS Apple Silicon、Linux x86_64、Windows x64 をビューアとホストの両方としてカバーし、
モバイルアプリはクライアントのみです。

クライアントのプラットフォーム対応とホスト対応は別物です：Mac ホストに DeskPort の Mac クライアントは不要です。
ハードウェアデコード、ライブ入力、画質、呼び出しのレイテンシは実際のセッションでのテストが必要で、
ビルドの成功だけでは証明されません。

## 次のマイルストーン

**50 回の非表示／表示サイクル**を通してセッションを接続したまま保ち、ウィンドウを現在のワークスペースに表示し、
ローカル入力を確実に戻すこと。新しいフレームのレイテンシとバックグラウンドのリソース使用量は、
ウィンドウの表示とは分けて計測します。

受け入れ基準と先送りした機能は[ロードマップ](../ROADMAP.md)、出自とメンテナンスの範囲は
[アップストリームに関する注記](../UPSTREAM.md)を参照してください。

## 検証

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

スモークテストは一時的な XDG 設定／キャッシュディレクトリとオフスクリーンの Qt プラットフォームを使用します。
ホストとのペアリング、ストリームの開始、入力の注入は行いません。

## 日常のデスクトップ操作

ウィンドウを閉じても DeskPort はトレイ／メニューバーで動作し続けます。**Open DeskPort** で呼び出し、
**Disconnect viewer** で現在の接続だけを終了し、**Quit DeskPort** でサービスを終了します。
ビューアを閉じてもローカル共有は動作し続けます。

プレーンテキストのクリップボード共有とシステムショートカットのキャプチャは、バインドされた両方のデバイスで既定で有効です。
設定の変更は再接続後に反映されます。共有されるのは新しくコピーした内容のみで、上限は 1 MiB です。
オプションを有効にした互換性のあるデスクトップ間では、対応する画像やファイルをオンデマンドで取得します。
[制限](../CLIPBOARD.md)を参照してください。デスクトップポインターモードでは、キーボードの入力先は
フォーカスされた映像内のポインターに従います。
**Ctrl+Alt+Shift+Z** で入力を解放、
**Ctrl+Alt+Shift+Q** でビューアを切断します。明示的に解放した後は、映像内をクリックすると入力が戻ります。
OS が予約しているショートカットはデスクトップのコンポジタによって異なります。

ログイン時起動と復元には、アクティブなグラフィカルログインセッションが必要です。コンピュータへの無人アクセスに頼る前に、
[受け入れチェックと制限](../INPUT_SERVICE_ACCEPTANCE.md)を確認してください。

## ライセンスと帰属

DeskPort は [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) の独立した派生プロジェクトで、
元は v6.1.0 をベースにしています。Moonlight や Sunshine の公式リリースではありません。
Moonlight がストリーミングの基盤を提供し、[Sunshine](https://github.com/LizardByte/Sunshine)
は macOS・Windows・Linux のホスト用パッケージに同梱され（閲覧専用の Flatpak を除く）、Linux の Nix パッケージでも提供されます。
別途インストールした Sunshine サービスは独立したままです。

GPL-3.0-or-later。[LICENSE](../../LICENSE)、保持されているソースの告知、および
[docs/VENDORED.md](../VENDORED.md) に記載された各同梱依存関係のライセンスを参照してください。元のドキュメントは
[README.upstream.md](README.upstream.md) に保存されています。

### 診断とフィードバック

診断ログは既定で有効です。問題を再現してからログ ZIP を作成し、GitHub issue の下書きを開いてください。
添付する前に ZIP の内容を確認してください：GitHub issue は公開されており、DeskPort が代わりにアップロードや送信を行うことはありません。
[診断データとその範囲](../DIAGNOSTICS.md)を参照してください。
