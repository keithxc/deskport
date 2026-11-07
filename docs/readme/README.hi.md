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
  <b>हिन्दी</b> ·
  <a href="README.id.md">Bahasa Indonesia</a> ·
  <a href="README.ms.md">Bahasa Melayu</a> ·
  <a href="README.th.md">ไทย</a> ·
  <a href="README.vi.md">Tiếng Việt</a> ·
  <a href="README.bo.md">བོད་ཡིག</a>
</p>

<p align="center">
  Moonlight और Sunshine पर बना एक रिमोट डेस्कटॉप वर्कस्पेस।<br>
  अपने रिमोट डेस्कटॉप को बैकग्राउंड में तैयार रखें, एक ही क्रिया से उसे मौजूदा वर्कस्पेस पर लाएँ,
  और बिना दोबारा कनेक्ट किए उसे छिपा दें।
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3_stable-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a> <del>$4.99</del>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## मुख्य विशेषताएँ

- **देखने और साझा करने के लिए एक ही डेस्कटॉप ऐप।** बंडल किया गया Sunshine, डिवाइस स्वीकृति, स्वतंत्र
  क्रेडेंशियल और सेटिंग्स; macOS, Windows और Linux होस्ट पैकेज के लिए अलग से Sunshine इंस्टॉल करने की
  ज़रूरत नहीं (केवल-व्यूअर Flatpak को छोड़कर)।
- **आपके क्लाइंट के अनुसार चलने वाला वर्कस्पेस।** संगत macOS और KDE होस्ट पर अनुकूली आकार, HiDPI,
  पोर्ट्रेट/लैंडस्केप मोबाइल लेआउट और हर डिवाइस के लिए आकार समायोजन।
  आकार बदलने पर वीडियो कुछ पल के लिए फिर से कनेक्ट होता है; यह निर्बाध एन्कोडर बदलाव नहीं है।
- **होस्ट डिस्प्ले नीति चुनें।** मिरर की गई स्क्रीन के साथ वर्चुअल प्राइमरी, बाकी स्क्रीन बंद करके
  वर्चुअल प्राइमरी, या सक्षम होस्ट पर विस्तारित वर्कस्पेस। सत्र समाप्त होने पर DeskPort सहेजा गया लेआउट
  बहाल करता है।
- **अधिकृत डिवाइसों के बीच आएँ-जाएँ।** सक्रिय सत्र अपने हाथ में लेने से पहले पुष्टि करें; हटाए गए
  क्लाइंट को टेकओवर की प्रमाणित जानकारी मिलती है।
- **डेस्कटॉप का काम पहुँच में रखें।** कनेक्टेड व्यूअर को छिपाएँ और वापस बुलाएँ, छिपे होने पर रिमोट
  इनपुट छोड़ें, और लोकल शेयरिंग से अलग देखना डिस्कनेक्ट करें।
- **हर कंप्यूटर के लिए सेटिंग्स सहेजें।** चित्र, ऑडियो, इनपुट, डेस्कटॉप ट्यूनिंग और पता संपादन डिवाइस
  के साथ रहते हैं; कार्ड, पिन, थीम और स्थानीयकृत इंटरफ़ेस।
- **ज़रूरत पड़ने पर क्लिपबोर्ड साझा करें।** टेक्स्ट तुरंत अपडेट होता है; समर्थित चित्र और फ़ाइलें
  अनुमति देने वाले जुड़े डेस्कटॉप पीयर के बीच माँगने पर लाई जाती हैं।
  देखें [क्लिपबोर्ड का दायरा और सीमाएँ](../CLIPBOARD.md)।
- **नेटिव मोबाइल नियंत्रण।** iPhone/iPad और Android क्लाइंट टच/ट्रैकपैड इनपुट, कीबोर्ड नियंत्रण
  और रिमोट डेस्कटॉप में सीधा प्रवेश देते हैं।
- **वैकल्पिक लॉगिन स्टार्टअप और macOS रिकवरी।** इनके लिए सक्रिय ग्राफ़िकल लॉगिन और सिस्टम स्वीकृति
  ज़रूरी है; ये FileVault अनलॉक नहीं करते और न ही आपकी ओर से लॉग इन करते हैं।

## इंटरफ़ेस पूर्वावलोकन

कृत्रिम डेमो डिवाइसों के साथ मौजूदा इंटरफ़ेस के स्क्रीनशॉट। पूरे आकार की छवि के लिए किसी पैनल पर
क्लिक करें; ये पूर्वावलोकन स्टोर या रिलीज़ में उपलब्धता नहीं दर्शाते।

| डिवाइस और समूह | हर डिवाइस की स्क्रीन सेटिंग्स | बिल्ट-इन मैनुअल |
| --- | --- | --- |
| [![डिवाइस और समूह](../media/devices.png)](../media/devices.png) | [![हर डिवाइस की स्क्रीन सेटिंग्स](../media/device-settings.png)](../media/device-settings.png) | [![बिल्ट-इन मैनुअल](../media/manual.png)](../media/manual.png) |

## समर्थित प्लेटफ़ॉर्म

सोर्स बनाम इंस्टॉल किए गए पैकेज का मौजूदा दायरा, कंपोज़िटर क्षमताएँ, ऐतिहासिक प्रमाण और बाकी नेटिव
जाँचों के लिए [PC कार्यान्वयन और स्वीकृति मैट्रिक्स](../PC_SUPPORT_MATRIX.md) देखें। नीचे दी गई रिलीज़
उपलब्धता हर GPU या OS सत्र पर हर सुविधा को प्रमाणित नहीं करती। 0.7.3 रिलीज़ अपनी सत्यापन रिपोर्ट में
सटीक पैकेज पहचान और सत्यापन सीमाएँ दर्ज करती है।

| प्लेटफ़ॉर्म | भूमिका | रिलीज़ स्थिति | डाउनलोड |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | व्यूअर + होस्ट + नेटिव वर्चुअल डिस्प्ले | **0.7.3 स्थिर**; Developer ID से हस्ताक्षरित और Apple द्वारा नोटराइज़्ड | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | व्यूअर + होस्ट; सक्षम KWin, Mutter और Hyprland होस्ट पर अनुकूली वर्कस्पेस | **0.7.3 स्थिर**: DEB, RPM, Arch, AppImage, पोर्टेबल आर्काइव और Nix | [रिलीज़](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [इंस्टॉलेशन गाइड](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | केवल व्यूअर | **0.7.3 स्थिर**, Freedesktop Platform 25.08 | [रिलीज़](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | केवल क्लाइंट | App Store; नए बिल्ड TestFlight में अलग से परखे जाते हैं | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | केवल क्लाइंट | **2.7 (63)** · Google Play का बंद परीक्षण जारी है | [APK डाउनलोड करें](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | व्यूअर + होस्ट + वर्चुअल डिस्प्ले | **0.7.3 स्थिर**; पूर्ण इंस्टॉलर होस्ट करता है, पोर्टेबल ZIP केवल देखता है; ऐप और इंस्टॉलर हस्ताक्षरित नहीं | [इंस्टॉलर](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Intel Mac | योग्य नहीं | कोई सत्यापित रिलीज़ पैकेज नहीं | — |

> **Windows एंटीवायरस सूचना:** एंटीवायरस सॉफ़्टवेयर DeskPort के Windows सहायक घटकों को गलती से खतरा बता सकता है।
> हमारे अपने परीक्षण में ऐसा नहीं हुआ। Defender चालू रखें; अगर चेतावनी दिखे,
> तो रिलीज़ सत्यापन रिपोर्ट और [नमूना इतिहास](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice) देखें।

नेटिव Linux पैकेज को glibc 2.39+ चाहिए: Ubuntu 24.04+/Debian 13+, Fedora 44, नवीनतम Arch। पुराने glibc
वाले डेस्कटॉप के लिए AppImage में निजी रनटाइम शामिल है; फिर भी होस्टिंग क्षमता हार्डवेयर ड्राइवर और
कंपोज़िटर समर्थन पर निर्भर करती है। KDE पर वर्चुअल डिस्प्ले होस्टिंग के लिए कंपोज़िटर अनुमति प्रविष्टियाँ
चाहिए। नेटिव/Nix पैकेज निश्चित प्रविष्टियाँ देते हैं; AppImage और पोर्टेबल बिल्ड शेयरिंग शुरू होने पर मौजूदा
डिस्प्ले-हेल्पर पथ अपने-आप पंजीकृत करते हैं। होस्ट इनपुट के लिए अब भी डिवाइस अनुमतियाँ चाहिए। मोबाइल ऐप
डेस्कटॉप होस्ट नहीं करते।
केवल सोर्स परिभाषा या सफल कंपाइलेशन से कोई प्लेटफ़ॉर्म समर्थित नहीं हो जाता।

## इंस्टॉल करें

**macOS:** [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) खोलें,
DeskPort को Applications में खींचें और खोलें। होस्ट सुविधाओं के लिए Screen Recording और Accessibility
अनुमतियाँ दें। Nix और Homebrew की ज़रूरत नहीं है।

**Linux:** अपने सिस्टम के लिए [0.7.3 पैकेज](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
चुनें और [नीचे दिए वितरण-विशिष्ट चरणों](#वितरण-के-अनुसार-linux-इंस्टॉलेशन) का पालन करें,
जिनमें [होस्ट इनपुट आवश्यकताएँ](../LINUX_PACKAGES.md#first-connection-and-hosting) भी शामिल हैं।
Nix के साथ: `nix run github:keithxc/deskport/v0.7.3`।
Flatpak केवल देखने की सुविधा देता है। रिलीज़ में [SHA-256 चेकसम](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) और [सत्यापन रिपोर्ट](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt) शामिल हैं।

**Android 2.7 (63)** · ~~$4.99~~ · Google Play का बंद परीक्षण पूरा होने तक [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) अस्थायी रूप से GitHub पर उपलब्ध है। मोबाइल क्लाइंट का स्रोत कोड निजी है। यहाँ केवल इंस्टॉलर दिया गया है।

बिल्ड 63 में Google Play वाली साइनिंग कुंजी का इस्तेमाल हुआ है। आगे चलकर Play के नए संस्करण में अपडेट करने पर पेयर किए गए कंप्यूटर और सेटिंग्स बनी रहेंगी। अगर आपने GitHub से बिल्ड 62 इंस्टॉल किया था, तो 63 इंस्टॉल करने से पहले उसे अनइंस्टॉल करना होगा। इससे स्थानीय सेटिंग्स मिट जाएँगी और कंप्यूटर फिर से पेयर करने होंगे।

**iPhone / iPad:** [App Store से DeskPort](https://apps.apple.com/us/app/deskport/id6812389978) इंस्टॉल करें।
मोबाइल ऐप एक बार की सशुल्क खरीद है; मौजूदा स्थानीय कीमत अपने स्टोर में देखें। डेस्कटॉप रिलीज़ मुफ़्त
और ओपन सोर्स रहती हैं। खरीदारी से साइनिंग, परीक्षण हार्डवेयर और निरंतर विकास में मदद मिलती है।

होस्ट पर शेयरिंग शुरू करें, क्लाइंट पर उसे चुनें और होस्ट पर डिवाइस को स्वीकृत करें। दोनों डिवाइसों के
बीच काम करने वाला नेटवर्क पथ होना चाहिए: LAN या Tailscale जैसा अपना VPN इस्तेमाल करें। DeskPort कोई अकाउंट
सेवा, होस्टेड डेस्कटॉप, रिले या नेटवर्क टनल नहीं देता। पुरानी Sunshine PIN पेयरिंग उपलब्ध रहती है।

**0.7.3 स्थिर:** यह डेस्कटॉप अपडेट अतिरिक्त पुराने डेस्कटॉप अनुवादों के साथ 22 भाषाओं वाला साझा UI
कैटलॉग, ब्राउज़र में भाषा बदलना, बार-बार UAC के बिना साइन-इन किए Windows सत्रों में शेयरिंग, वर्चुअल
डिस्प्ले सिस्टम स्केलिंग, Linux रनटाइम सुधार, सुरक्षित Hyprland/X11 वर्कस्पेस, Wayland क्लिपबोर्ड फ़ॉलबैक,
वैकल्पिक लोकल IME और चौकोर डिवाइस कार्ड लाता है। सटीक दायरे और बाकी जाँचों के लिए
[0.7.3 रिलीज़ नोट्स](../releases/RELEASE_0.7.3.md) देखें। सटीक-पैकेज जाँचों में macOS वितरण विश्वसनीयता, Windows
पेलोड, नेटिव CLI और सुरक्षित एंटीवायरस स्कैनिंग, तथा Linux रनटाइम/पैकेज परिदृश्य शामिल हैं। पहले के नेटिव
और इंटीग्रेशन परिणाम अपनी मूल सोर्स पहचान बनाए रखते हैं।

**0.7.2 स्थिर (ऐतिहासिक सत्यापन):** [डेस्कटॉप रिलीज़](https://github.com/keithxc/deskport/releases/tag/v0.7.2)
आकार बदलने, रिमोट स्ट्रीमिंग, डेटा-सेवर और Linux रनटाइम सुधारों को जोड़ती है। इसकी 21 फ़ाइलों में पैकेज,
संबंधित सोर्स, रीलिंकिंग सामग्री और सत्यापन रिकॉर्ड शामिल हैं। देखें [0.7.2 रिलीज़ नोट्स](../releases/RELEASE_0.7.2.md)।
अंतिम AppImage ने सामान्य FUSE स्टार्टअप, मेल खाते व्यूअर/हेल्पर/रिकवरी रनटाइम और सत्यापित सफ़ाई के साथ
नए Bazzite KDE Wayland VM की जाँच पास की; भौतिक GPU और वास्तविक स्ट्रीमिंग की स्वीकृति अलग है।

Windows Setup ने कॉन्फ़िगरेशन और पेयरिंग संरक्षण जाँचों के साथ 0.7.1 से अपग्रेड पास किया। नेटवर्क से
अलग लोकल GUI जाँचों में शॉर्टकट से लॉन्च, Settings/About, ट्रे में बंद करना, वापस बुलाना और Quit शामिल थे।
अनइंस्टॉल और योजनाबद्ध वास्तविक स्ट्रीमिंग/इनपुट स्वीकृति अभी परखी नहीं गई। ऑटोमेशन में फ़ोकस रेस के कारण
पहले से पेयर किए गए होस्ट से अनचाहा कनेक्शन हुआ; बाद की जाँचों ने शून्य सक्रिय सत्र, कोई आरक्षित लीज़ नहीं
और मूल डिस्प्ले लेआउट की बहाली की पुष्टि की। यह घटना स्ट्रीमिंग स्वीकृति नहीं है, और डायग्नोस्टिक्स यह
साबित नहीं कर सकते कि कोई रिमोट इनपुट नहीं भेजा गया। सटीक दायरे के लिए रिलीज़ नोट्स देखें।

**पिछले कैंडिडेट:** [0.7.2-rc.1](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.1)
ने आकार बदलने पर तेज़ री-कनेक्ट, धुंधले ट्रांज़िशन और macOS पर बेहतर ड्रैग पेश किए;
[0.7.2-rc.2](https://github.com/keithxc/deskport/releases/tag/v0.7.2-rc.2)
ने Bazzite AppImage सुधार जोड़े। उनके [rc.1 नोट्स](../releases/RELEASE_0.7.2-rc.1.md)
और [rc.2 नोट्स](../releases/RELEASE_0.7.2-rc.2.md) पहले का सत्यापन दायरा बनाए रखते हैं।

**ब्राउज़र एक्सेस:** डेस्कटॉप होस्ट 30 सेकंड के साइन-इन कोड, सात दिनों की वैकल्पिक ब्राउज़र पेयरिंग,
अनुकूली आकार और फ़ुलस्क्रीन के साथ HTTPS रिमोट डेस्कटॉप देते हैं। देखें [0.7.1 रिलीज़ नोट्स](../releases/RELEASE_0.7.1.md)
और [ब्राउज़र गाइड](../BROWSER_ACCESS.md)। AppImage में निजी glibc रनटाइम शामिल है; नेटिव पैकेज glibc 2.39
की आवश्यकता बनाए रखते हैं।

### वितरण के अनुसार Linux इंस्टॉलेशन

ये निर्देश **x86_64** डेस्कटॉप सिस्टम और **0.7.3** रिलीज़ डाउनलोड के लिए हैं। नेटिव पैकेज को glibc 2.39
या नया चाहिए। ये अपस्ट्रीम डाउनलोड हैं, DeskPort वितरण रिपॉज़िटरी के पैकेज नहीं; भविष्य की रिलीज़ नया पैकेज
फिर से डाउनलोड और सत्यापित करके इंस्टॉल करें।

0.7.3 AppImage, Bazzite/KDE Wayland [issue #5](https://github.com/keithxc/deskport/issues/5) के लिए पोर्टेबल
रनटाइम, डिस्प्ले बहाली, प्रोसेस समापन, अनुमति और लॉगिन स्टार्टअप सुधारों को, छोटे Qt Wayland 6.4.2 बैकपोर्ट
सहित, बनाए रखता है। वह बैकपोर्ट केवल AppImage के लिए है; नेटिव पैकेज और Flatpak अपना बेसलाइन Qt रनटाइम
रखते हैं। अंतिम **0.7.3** AppImage ने SELinux Enforcing और सॉफ़्टवेयर ग्राफ़िक्स वाले नए Bazzite KDE Wayland
VM में सामान्य FUSE स्टार्टअप और हेल्पर/रनटाइम जाँच पास की। इससे भौतिक GPU या वास्तविक स्ट्रीमिंग सत्यापित
नहीं होती; देखें [0.7.3 सत्यापन दायरा](../releases/RELEASE_0.7.3.md#validation-boundary)।

पूर्ण VM में इंस्टॉलेशन, अपग्रेड, रोलबैक, हटाने और कॉन्फ़िगरेशन बनाए रखने के परिणाम
[डेस्कटॉप लाइफ़साइकल मैट्रिक्स](../LINUX_DESKTOP_ACCEPTANCE.md) में दर्ज हैं। AppImage GUI स्मोक टेस्ट नेटिव
पैकेज संचालन को सत्यापित नहीं करता। केवल पैकेज इंस्टॉल करने और GUI लॉन्च करने से लाइव कैप्चर, हार्डवेयर
वीडियो डिकोडिंग, रिमोट इनपुट, दृश्य गुणवत्ता या लेटेंसी सत्यापित नहीं होती।

रखा गया Ubuntu 26.04 GNOME VM प्रकाशित 0.7.2 DEB की पुरानी निजी VA-API Wayland लाइब्रेरी में स्टार्टअप
क्रैश दोहराता है। अंतिम 0.7.3 DEB दोनों निजी रनटाइम ट्री में पिन किया गया libva 2.23.0 इस्तेमाल करता है और
सिस्टम यूटिलिटी को होस्ट लाइब्रेरी ओवरराइड से अलग करता है। इसने उस VM के नए ओवरले में GUI/API और अपग्रेड
जाँच पास की। रोलबैक और अनइंस्टॉल के दौरान कृत्रिम कॉन्फ़िगरेशन सुरक्षित रहा; पुराने संस्करण का क्रैश अपग्रेड
से पहले और रोलबैक के बाद दोहराया गया। मौजूदा रिलीज़ फ़ाइलें अपने मूल हैश रखती हैं। GNOME कैप्चर स्टार्टअप और
बार-बार वर्चुअल आउटपुट बनाना अलग स्वीकृति कमियाँ बनी हुई हैं। Debian 13 का KWin 6.3 भी होस्ट डिस्प्ले-हेल्पर
के न्यूनतम 6.6 से नीचे है।

| वितरण / डेस्कटॉप | डाउनलोड मार्ग |
| --- | --- |
| Ubuntu 26.04 GNOME | 0.7.3 DEB या AppImage; DEB स्टार्टअप रिग्रेशन पास, GNOME कैप्चर/रिकवरी कमियाँ बाकी |
| Debian 13 KDE | DEB, APT से इंस्टॉल |
| Fedora 44 KDE | RPM, DNF से इंस्टॉल |
| Arch Linux KDE | लोकल Arch पैकेज, `pacman -U` से इंस्टॉल |
| Bazzite KDE | 0.7.3 AppImage; ऊपर बताई गई सटीक-पैकेज VM जाँचें |
| Omarchy / Hyprland | लोकल Arch पैकेज, `pacman -U` से इंस्टॉल |

अपने सामान्य डेस्कटॉप सत्र में टर्मिनल खोलें। हर कमांड क्रम इंस्टॉल करने से पहले डाउनलोड की गई फ़ाइल के
सटीक नाम को उस रिलीज़ की `SHA256SUMS.txt` से मिलाता है; चेकसम को उस फ़ाइल के लिए `OK` बताना चाहिए। प्रविष्टि
न होने या मेल न खाने पर क्रम रुक जाता है।
इंस्टॉलेशन के बाद DeskPort को अपने सामान्य उपयोगकर्ता के रूप में चलाएँ।
अपग्रेड से पहले शेयरिंग रोकें और पुराने DeskPort इंस्टेंस को उसके **Quit** विकल्प, जैसे ट्रे मेनू, से पूरी
तरह बंद करें। विंडो बंद करने से वह केवल छिप सकती है। इसके बाद नया इंस्टॉल किया गया संस्करण चलाएँ ताकि नई
बाइनरी चले।

<details>
<summary><strong>Ubuntu 26.04 GNOME</strong> — डाउनलोड, इंस्टॉल, अपडेट और हटाना</summary>

[deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb) डाउनलोड करें,
फिर उस फ़ाइल वाली डायरेक्टरी में टर्मिनल खोलें:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT आपकी कॉन्फ़िगर की गई Ubuntu रिपॉज़िटरी से निर्भरताएँ इंस्टॉल करता है। आगे का `./` डाउनलोड की गई फ़ाइल
चुनता है। बाद में DeskPort को ऐप्लिकेशन मेनू से खोलें। अपग्रेड के लिए अगला संगत DEB डाउनलोड करें, उसका चेकसम
सत्यापित करें और असली फ़ाइल नाम के साथ `sudo apt install ./NEW_FILENAME.deb` दोहराएँ। इंस्टॉल किया गया पैकेज
`sudo apt remove deskport` से हटाएँ।

Ubuntu 24.04 भी नेटिव पैकेज की न्यूनतम glibc आवश्यकता पूरी करता है। Ubuntu 22.04 के लिए AppImage, केवल-व्यूअर
Flatpak या अलग से समर्थित Nix/सोर्स बिल्ड चाहिए। GNOME पर होस्टिंग इंस्टॉल की गई Mutter/कैप्चर क्षमताओं पर निर्भर
करती है; देखें [अनुकूली डिस्प्ले आवश्यकताएँ](../LINUX_ADAPTIVE_DISPLAY.md)।

</details>

<details>
<summary><strong>Debian 13 KDE</strong> — डाउनलोड, इंस्टॉल, अपडेट और हटाना</summary>

[deskport_0.7.3-1_amd64.deb](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport_0.7.3-1_amd64.deb) डाउनलोड करें,
फिर डाउनलोड डायरेक्टरी में टर्मिनल खोलें:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport_0.7.3-1_amd64.deb" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo apt update &&
sudo apt install ./deskport_0.7.3-1_amd64.deb &&
deskport
```

APT पैकेज की निर्भरताएँ हल करता है। बाद में DeskPort को ऐप्लिकेशन मेनू से खोलें। अगला संगत DEB डाउनलोड और
सत्यापित करके उसे `sudo apt install ./NEW_FILENAME.deb` से इंस्टॉल कर अपग्रेड करें। `sudo apt remove deskport`
से हटाएँ। Debian 12 नेटिव पैकेज की न्यूनतम glibc आवश्यकता पूरी नहीं करता; AppImage, केवल-व्यूअर Flatpak या
समर्थित Nix/सोर्स बिल्ड इस्तेमाल करें।

KDE पर होस्टिंग के लिए शेयरिंग शुरू करने से पहले [कंपोज़िटर और इनपुट आवश्यकताएँ](../LINUX_PACKAGES.md#first-connection-and-hosting)
जाँचें।

</details>

<details>
<summary><strong>Fedora 44 KDE</strong> — डाउनलोड, इंस्टॉल, अपडेट और हटाना</summary>

[deskport-0.7.3-1.x86_64.rpm](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1.x86_64.rpm) डाउनलोड करें,
फिर डाउनलोड डायरेक्टरी में टर्मिनल खोलें:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1.x86_64.rpm" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo dnf install ./deskport-0.7.3-1.x86_64.rpm &&
deskport
```

DNF सक्षम Fedora रिपॉज़िटरी से निर्भरताएँ हल करता है। बाद में DeskPort को ऐप्लिकेशन मेनू से खोलें। अगला संगत
RPM डाउनलोड और सत्यापित करके उसे `sudo dnf install ./NEW_FILENAME.rpm` से इंस्टॉल कर अपग्रेड करें।
`sudo dnf remove deskport` से हटाएँ। यह मार्ग सामान्य Fedora के लिए है; Bazzite के लिए AppImage निर्देश अपनाएँ।
KDE पर होस्टिंग के लिए [कंपोज़िटर और इनपुट आवश्यकताएँ](../LINUX_PACKAGES.md#first-connection-and-hosting) भी जाँचें।

</details>

<details>
<summary><strong>Arch Linux KDE</strong> — डाउनलोड, इंस्टॉल, अपडेट और हटाना</summary>

अपना सामान्य पूर्ण Arch सिस्टम अपडेट करें, फिर
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst) डाउनलोड करें।
डाउनलोड डायरेक्टरी में टर्मिनल खोलें:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

`pacman -U` लोकल पैकेज इंस्टॉल करता है और कॉन्फ़िगर की गई रिपॉज़िटरी से निर्भरताएँ हल करता है। बाद में DeskPort
को ऐप्लिकेशन मेनू से खोलें। भविष्य में डाउनलोड किए गए पैकेज उनके चेकसम सत्यापित करने के बाद
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` से इंस्टॉल करें। `sudo pacman -R deskport` से हटाएँ। यह डाउनलोड
DeskPort को आधिकारिक रिपॉज़िटरी या AUR में नहीं जोड़ता।
KDE पर होस्टिंग के लिए [कंपोज़िटर और इनपुट आवश्यकताएँ](../LINUX_PACKAGES.md#first-connection-and-hosting) भी जाँचें।

</details>

<details>
<summary><strong>Bazzite KDE</strong> — AppImage, स्टार्टअप और हटाना</summary>

[DeskPort-0.7.3-x86_64.AppImage](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage)
डाउनलोड करें। शेयरिंग रोकें और किसी भी पुराने DeskPort इंस्टेंस को पूरी तरह बंद करने के लिए **Quit** इस्तेमाल
करें; विंडो बंद करने से वह चलता रह सकता है। लॉगिन स्टार्टअप सक्षम करने से पहले नई AppImage को स्थायी स्थान पर
सहेजें:

```sh
mkdir -p "$HOME/Applications/DeskPort" &&
cd "$HOME/Applications/DeskPort" &&
curl --fail --location --output DeskPort-0.7.3-x86_64.AppImage https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-x86_64.AppImage &&
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-x86_64.AppImage" { print }' SHA256SUMS.txt | sha256sum --check - &&
chmod +x DeskPort-0.7.3-x86_64.AppImage &&
./DeskPort-0.7.3-x86_64.AppImage
```

सामान्य रूप से लॉन्च करें; बंडल की गई लाइब्रेरी हटाने या खिसकाने की ज़रूरत नहीं। अगर लॉगिन स्टार्टअप किसी पुरानी
AppImage की ओर इशारा करता था, तो नई फ़ाइल को उसके अंतिम पथ पर खोलें और लॉगिन स्टार्टअप ताज़ा करें। सूची के किसी
अन्य x86_64 वितरण पर AppImage इस्तेमाल करने के लिए उसका नेटिव पैकेज इंस्टॉल करने के बजाय ये चरण अपनाएँ।

Bazzite की [AppImage गाइड](https://docs.bazzite.gg/Installing_and_Managing_Software/AppImage/) ऐप्लिकेशन मेनू
इंटीग्रेशन के लिए Gear Lever का वर्णन करती है। अगर कोई इंटीग्रेशन टूल फ़ाइल को खिसकाता है, तो बाद में उसके अंतिम
स्थान से DeskPort स्टार्टअप ताज़ा करें। अपग्रेड के लिए नई AppImage डाउनलोड और सत्यापित करनी होती है। हटाने के लिए
DeskPort लॉगिन स्टार्टअप बंद करें, DeskPort से बाहर निकलें, फ़ाइल मिटाएँ और जिस इंटीग्रेशन टूल ने मेनू प्रविष्टि
बनाई थी उसी से उसे हटाएँ। डेस्कटॉप शेयरिंग के लिए AppImage को Bazzite होस्ट पर चलाएँ; Distrobox इंस्टॉलेशन होस्ट
डेस्कटॉप इंटीग्रेशन नहीं देता। नीचे दिया Flatpak केवल देखने की सुविधा देता है।

</details>

<details>
<summary><strong>Omarchy / Hyprland</strong> — लोकल Arch पैकेज और होस्टिंग आवश्यकताएँ</summary>

Omarchy का सामान्य सिस्टम अपडेट करें, फिर
[deskport-0.7.3-1-x86_64.pkg.tar.zst](https://github.com/keithxc/deskport/releases/download/v0.7.3/deskport-0.7.3-1-x86_64.pkg.tar.zst) डाउनलोड करें।
डाउनलोड डायरेक्टरी में टर्मिनल खोलें:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "deskport-0.7.3-1-x86_64.pkg.tar.zst" { print }' SHA256SUMS.txt | sha256sum --check - &&
sudo pacman -U ./deskport-0.7.3-1-x86_64.pkg.tar.zst &&
deskport
```

बाद में ऐप्लिकेशन लॉन्चर इस्तेमाल करें। अगला लोकल Arch पैकेज डाउनलोड और सत्यापित करके उसे
`sudo pacman -U ./NEW_FILENAME.pkg.tar.zst` से इंस्टॉल कर अपग्रेड करें। `sudo pacman -R deskport` से हटाएँ।
Omarchy का रिपॉज़िटरी/AUR पैकेज मेनू इस GitHub डाउनलोड को रिपॉज़िटरी पैकेज नहीं बनाता।

Hyprland होस्टिंग एक समर्पित हेडलेस आउटपुट इस्तेमाल करती है। 0.7.3 सोर्स मिरर, बाकी बंद करें और विस्तार
नीतियों का समर्थन करता है, जब मॉनिटर पहचान और रनटाइम स्थिति सुरक्षित रूप से बहाल की जा सके; देखें
[Hyprland डिस्प्ले लीज़](../HYPRLAND_DISPLAY.md)। सक्रिय कंपोज़िटर, कैप्चर अनुमति और SSH सेवा आवश्यकताओं के लिए
[Omarchy/Hyprland होस्ट गाइड](../CLI.md#omarchy-and-hyprland) का पालन करें। अनुमति सिंटैक्स लागू करने से पहले
इंस्टॉल किया गया Hyprland संस्करण जाँचें। केवल पैकेज इंस्टॉल करने से बिना निगरानी वाला डेस्कटॉप लॉगिन
कॉन्फ़िगर नहीं होता और न ही चालू स्ट्रीम सत्यापित होती है।

</details>

<details>
<summary><strong>Flatpak विकल्प</strong> — केवल देखना, Flatpak कॉन्फ़िगर किए गए सिस्टम पर</summary>

[DeskPort-0.7.3-client-x86_64.flatpak](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-client-x86_64.flatpak) डाउनलोड करें,
फिर डाउनलोड डायरेक्टरी में टर्मिनल खोलें:

```sh
curl --fail --location --output SHA256SUMS.txt https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt &&
awk '$2 == "DeskPort-0.7.3-client-x86_64.flatpak" { print }' SHA256SUMS.txt | sha256sum --check - &&
flatpak install --user ./DeskPort-0.7.3-client-x86_64.flatpak &&
flatpak run io.github.keithxc.DeskPort
```

लोकल बंडल Freedesktop Platform 25.08 रनटाइम डाउनलोड कर सकता है। यह Sunshine होस्टिंग या डेस्कटॉप लॉगिन-सेवा
इंटीग्रेशन के बिना केवल देखने की सुविधा देता है। भविष्य में डाउनलोड किए गए बंडल चेकसम सत्यापित करने के बाद
`flatpak install --user --or-update ./NEW_FILENAME.flatpak` से इंस्टॉल करें।
`flatpak uninstall --user io.github.keithxc.DeskPort` से हटाएँ। यह GitHub रिलीज़ बंडल है; इंस्टॉलेशन के लिए
Flathub पर DeskPort लिस्टिंग की ज़रूरत नहीं।

</details>

होस्टिंग के लिए सामान्य लॉग-इन ग्राफ़िकल सत्र इस्तेमाल करें और
[कैप्चर तथा इनपुट पूर्वापेक्षाएँ](../LINUX_PACKAGES.md#first-connection-and-hosting) कॉन्फ़िगर करें।
वैकल्पिक लॉगिन स्टार्टअप सक्षम करने से पहले अपने इच्छित LAN या VPN पर किसी क्लाइंट के साथ शेयरिंग परखें। पैकेज
इंस्टॉलेशन नेटवर्क पहुँच कॉन्फ़िगर नहीं करता और न ही होस्ट इनपुट एक्सेस देता है। पैकेज हटाने से पहले शेयरिंग रोकें
और DeskPort से बाहर निकलें; अगर लॉगिन स्टार्टअप सक्षम किया था तो उसे बंद करें। पैकेज हटाने पर आपकी होम
डायरेक्टरी में DeskPort सेटिंग्स बनी रहती हैं।

### Linux रिपॉज़िटरी उपलब्धता

| चैनल | स्थिति | इंस्टॉलेशन |
| --- | --- | --- |
| GitHub रिलीज़ पैकेज | 0.7.3 स्थिर | ऊपर से अपने वितरण का पैकेज डाउनलोड करें |
| अपस्ट्रीम Nix flake | 0.7.3 स्थिर | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | रेसिपी परखी गई; नए अकाउंट पंजीकरण रुकने से प्रकाशन अवरुद्ध | [रेसिपी लोकल रूप से बनाएँ](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) समीक्षा के लिए भेजा गया; मर्ज नहीं हुआ | शामिल होने तक अपस्ट्रीम flake इस्तेमाल करें |
| आधिकारिक Arch रिपॉज़िटरी / Flathub | सूचीबद्ध नहीं | रिलीज़ डाउनलोड इस्तेमाल करें; Flatpak केवल-व्यूअर है |

डाउनलोड करने योग्य Arch पैकेज `pacman -U` से इंस्टॉल होता है; इससे `pacman -S deskport` उपलब्ध नहीं होता। इसी तरह,
अपस्ट्रीम flake से Nixpkgs में `pkgs.deskport` उपलब्ध नहीं होता। होस्ट इनपुट, कैप्चर अनुमतियाँ और फ़ायरवॉल सेटअप
ऐप्लिकेशन इंस्टॉल करने से अलग हैं; देखें [Linux इंस्टॉलेशन गाइड](../LINUX_PACKAGES.md#nixos-and-nix)।

## मिलते-जुलते उत्पादों से तुलना

यह कार्यप्रवाह और प्लेटफ़ॉर्म की तुलना है, जिसे **2026-09-20** को लिंक किए गए आधिकारिक दस्तावेज़ों से जाँचा गया;
यह लेटेंसी या छवि गुणवत्ता का बेंचमार्क नहीं है। उत्पाद सुविधाएँ और प्लेटफ़ॉर्म सीमाएँ बदल सकती हैं।

| पहलू | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| मुख्य कार्यप्रवाह | स्थायी रिमोट डेस्कटॉप वर्कस्पेस, अनुकूली आकार और डिवाइस हैंडऑफ़ | अलग क्लाइंट और होस्ट ऐप से गेम/डेस्कटॉप स्ट्रीमिंग | सामान्य रिमोट कंट्रोल और सहायता | इंटरैक्टिव रिमोट डेस्कटॉप और सहयोग |
| डेस्कटॉप होस्ट प्लेटफ़ॉर्म | macOS Apple Silicon, Linux x86_64 और Windows x64 | प्लेटफ़ॉर्म सीमाओं के साथ Windows, macOS, Linux और FreeBSD | Windows, macOS और Linux | Windows और macOS; Linux होस्ट नहीं कर सकता |
| डेस्कटॉप क्लाइंट | macOS Apple Silicon, Linux x86_64 और Windows x64 | Windows, macOS और Linux, आदि | Windows, macOS और Linux | Windows, macOS, Linux और समर्थित Chromium ब्राउज़र |
| मोबाइल क्लाइंट | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS और Android | iOS/iPadOS और Android; iOS होस्ट नहीं कर सकता | Android; कोई iOS/iPadOS क्लाइंट नहीं |
| सेटअप मॉडल | नेटिव डेस्कटॉप पैकेज में बंडल होस्ट; DeskPort में डिवाइस स्वीकृत करें | Sunshine अलग से इंस्टॉल और कॉन्फ़िगर करें, फिर Moonlight पेयर करें | सार्वजनिक सर्वर इन्फ्रास्ट्रक्चर या सेल्फ़-होस्टेड सर्वर | Parsec अकाउंट और ऐप |
| नेटवर्क/सेवा मॉडल | अपनी LAN/VPN पहुँच; कोई DeskPort रिले नहीं | सेल्फ़-होस्टेड स्ट्रीमिंग; नेटवर्क पहुँच कॉन्फ़िगर करें | सार्वजनिक इन्फ्रास्ट्रक्चर या सेल्फ़-होस्टेड OSS/Pro सर्वर | Parsec अकाउंट/सेवा इन्फ्रास्ट्रक्चर |
| सोर्स और वितरण | GPL डेस्कटॉप, मुफ़्त डेस्कटॉप पैकेज; सशुल्क Apple ऐप अलग से वितरित | ओपन-सोर्स क्लाइंट और होस्ट | ओपन-सोर्स क्लाइंट और OSS सर्वर; व्यावसायिक Pro सर्वर विकल्प | मालिकाना ऐप/सेवा |

स्रोत: [Moonlight](https://moonlight-stream.org/),
[Sunshine इंस्टॉलेशन और प्लेटफ़ॉर्म समर्थन](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk क्लाइंट](https://rustdesk.com/docs/en/client/),
[RustDesk सेल्फ़-होस्टिंग](https://rustdesk.com/docs/en/self-host/),
[Parsec प्लेटफ़ॉर्म संगतता](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec Linux सेटअप](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux)।
DeskPort की मौजूदा प्लेटफ़ॉर्म सीमाएँ ऊपर सूचीबद्ध हैं; यह तालिका हर OS पर सुविधा समानता या मापे गए प्रदर्शन
लाभ का दावा नहीं करती।

देखें [0.7.3 रिलीज़ नोट्स](../releases/RELEASE_0.7.3.md), [आर्किटेक्चर](../ARCHITECTURE.md) और [रोडमैप](../ROADMAP.md)।

## Linux पर बिल्ड करें और चलाएँ

Nix और flakes सक्षम होने पर:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

व्यूअर की तृतीय-पक्ष सोर्स निर्भरताएँ रिपॉज़िटरी में शामिल हैं; साझा कोर और Nix बिल्ड इनपुट पिन की गई बाहरी
निर्भरताएँ बने रहते हैं। नेटिव बिल्ड को `shared/deskport-core` इनिशियलाइज़ करना होगा। देखें [docs/VENDORED.md](../VENDORED.md)।
SSH सेटअप, लोकल होस्ट प्रबंधन, VPS पूर्वापेक्षाओं और Omarchy/Hyprland के लिए [CLI गाइड](../CLI.md) देखें।
`deskport doctor` होस्टिंग पूर्वापेक्षाएँ जाँचता है; `deskport status --json` बिना विंडो खोले चल रहे इंस्टेंस की जाँच
करता है। `nix run . -- --help` कमांड-लाइन इंटरफ़ेस दिखाता है। कनेक्ट करने से पहले होस्ट पर शेयरिंग शुरू करें और
डिवाइस बाइंड करें। पुरानी Sunshine PIN पेयरिंग भी उपलब्ध है। कोई निजी होस्ट या पेयरिंग क्रेडेंशियल शामिल नहीं है
और न ही Moonlight से आयात किया जाता है।
नए मैनुअल पते डिफ़ॉल्ट रूप से DeskPort का पोर्ट `48989` इस्तेमाल करते हैं। अगर होस्ट के शेयरिंग पेज पर दिखा पोर्ट
अलग है तो उसे जोड़ें, या डिफ़ॉल्ट स्टैंडअलोन Sunshine इंस्टॉलेशन से स्पष्ट रूप से कनेक्ट करने के लिए `host:47989`
इस्तेमाल करें। सहेजे/खोजे गए एंडपॉइंट अपने पोर्ट रखते हैं।

संपादन योग्य नेटिव बिल्ड के लिए:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

फ़ोर्क की समीक्षा आसान रखने के लिए अपस्ट्रीम प्रोजेक्ट के फ़ाइल नाम नहीं बदले गए हैं।

## Moonlight से क्या अलग है?

- डिवाइस स्वीकृति, टेकओवर और हर डिवाइस की सेटिंग्स के साथ बंडल Sunshine होस्ट।
- व्यूअर छिपा होने पर भी सत्र जुड़े रहते हैं; उसे ट्रे/मेनू बार से वापस बुलाएँ।
- डेस्कटॉप होस्ट द्वारा दिया जाने वाला HTTPS ब्राउज़र एक्सेस।
- अलग `deskport` एक्ज़ीक्यूटेबल, `DeskPort` Qt सेटिंग्स नेमस्पेस और
  `io.github.keithxc.DeskPort` Linux ऐप्लिकेशन ID।
- डिफ़ॉल्ट रूप से विंडो में स्ट्रीमिंग और एब्सोल्यूट-पॉइंटर नियंत्रण।
- फ़ोकस खोने पर म्यूट; गेम ऑप्टिमाइज़ेशन, गेमपैड माउस, मल्टी-कंट्रोलर मोड, बैकग्राउंड गेमपैड इनपुट और
  Discord प्रेज़ेंस डिफ़ॉल्ट रूप से बंद।
- इस स्वतंत्र ऐप्लिकेशन के लिए अपस्ट्रीम Moonlight अपडेट संकेत नहीं।
- लॉक किया गया Nix वातावरण और Linux बिल्ड वर्कफ़्लो।

डेस्कटॉप इंटरफ़ेस भाषा चयन और अलग होस्ट अनुमतियों के साथ डिवाइस, शेयरिंग और सेटिंग्स पेज देता है। व्यूअर बंद
करने पर उसका सत्र उपलब्ध रहता है और डिवाइस सूची खुलती है। सक्रिय डिवाइस "डेस्कटॉप पर लौटें" देता है; बाकी डिवाइस
मौजूदा सत्र डिस्कनेक्ट होने तक विवरण दिखाते हैं। बार-बार इस्तेमाल होने वाले डिवाइस पिन करें और कॉम्पैक्ट सूची
या कार्ड चुनें। रूप-रंग सिस्टम के अनुसार चलता है, लाइट और डार्क को बलपूर्वक चुना जा सकता है।

## प्लेटफ़ॉर्म दायरा

हर प्लेटफ़ॉर्म की रिलीज़ स्थिति के लिए ऊपर दी [प्लेटफ़ॉर्म तालिका](#समर्थित-प्लेटफ़ॉर्म) देखें। डेस्कटॉप रिलीज़
macOS Apple Silicon, Linux x86_64 और Windows x64 को व्यूअर और होस्ट दोनों रूपों में कवर करती हैं; मोबाइल ऐप केवल
क्लाइंट हैं।

क्लाइंट और होस्ट समर्थन अलग हैं: Mac होस्ट के लिए DeskPort Mac क्लाइंट ज़रूरी नहीं। हार्डवेयर डिकोड, लाइव इनपुट,
छवि गुणवत्ता और वापस बुलाने की लेटेंसी के लिए वास्तविक सत्र परीक्षण चाहिए; सफल बिल्ड इन्हें स्थापित नहीं करता।

## अगला पड़ाव

एक सत्र को **50 छिपाने/दिखाने के चक्रों** तक जुड़ा रखना, विंडो को मौजूदा वर्कस्पेस पर दिखाना और लोकल इनपुट
भरोसेमंद ढंग से लौटाना। ताज़ा फ़्रेम लेटेंसी और बैकग्राउंड संसाधन उपयोग को विंडो दिखने से अलग मापना।

स्वीकृति मानदंड और टाली गई सुविधाओं के लिए [रोडमैप](../ROADMAP.md), और उद्गम तथा रखरखाव सीमाओं के लिए
[अपस्ट्रीम नोट्स](../UPSTREAM.md) देखें।

## सत्यापन

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

स्मोक टेस्ट अस्थायी XDG कॉन्फ़िगरेशन/कैश डायरेक्टरी और ऑफ़स्क्रीन Qt प्लेटफ़ॉर्म इस्तेमाल करता है। यह किसी होस्ट
से पेयर नहीं करता, स्ट्रीम शुरू नहीं करता और इनपुट नहीं भेजता।

## रोज़मर्रा के डेस्कटॉप नियंत्रण

विंडो बंद करने पर DeskPort ट्रे/मेनू बार में चलता रहता है। वापस बुलाने के लिए **Open DeskPort**, केवल मौजूदा
कनेक्शन समाप्त करने के लिए **Disconnect viewer**, या सेवा से बाहर निकलने के लिए **Quit DeskPort** इस्तेमाल करें।
व्यूअर बंद होने पर लोकल शेयरिंग जारी रहती है।

सादा-टेक्स्ट क्लिपबोर्ड शेयरिंग और सिस्टम शॉर्टकट कैप्चर दोनों जुड़े डिवाइसों पर डिफ़ॉल्ट रूप से चालू हैं। सेटिंग
बदलाव दोबारा कनेक्ट करने के बाद लागू होते हैं। केवल नई कॉपी, 1 MiB तक, साझा की जाती हैं। समर्थित चित्र और फ़ाइलें
अनुमति देने वाले संगत डेस्कटॉप पीयर के बीच माँगने पर लाई जाती हैं; देखें [सीमाएँ](../CLIPBOARD.md)। डेस्कटॉप
पॉइंटर मोड में कीबोर्ड रूटिंग फ़ोकस किए गए वीडियो के भीतर पॉइंटर का अनुसरण करती है।
**Ctrl+Alt+Shift+Z** इनपुट छोड़ता है;
**Ctrl+Alt+Shift+Q** व्यूअर डिस्कनेक्ट करता है। स्पष्ट रूप से छोड़ने के बाद इनपुट वापस पाने के लिए भीतर क्लिक करें।
OS-आरक्षित शॉर्टकट डेस्कटॉप कंपोज़िटर पर निर्भर करते हैं।

लॉगिन स्टार्टअप और रिकवरी के लिए सक्रिय ग्राफ़िकल लॉगिन सत्र चाहिए। बिना निगरानी वाले एक्सेस के लिए किसी
कंप्यूटर पर भरोसा करने से पहले [स्वीकृति जाँच और सीमाएँ](../INPUT_SERVICE_ACCEPTANCE.md) देखें।

## लाइसेंस और श्रेय

DeskPort, [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) का एक स्वतंत्र व्युत्पन्न है, जो शुरू में
v6.1.0 पर आधारित था। यह Moonlight या Sunshine की आधिकारिक रिलीज़ नहीं है। Moonlight स्ट्रीमिंग की नींव देता है;
[Sunshine](https://github.com/LizardByte/Sunshine) macOS, Windows और Linux होस्ट पैकेज में बंडल है (केवल-व्यूअर
Flatpak को छोड़कर) और Linux Nix पैकेज द्वारा दिया जाता है। अलग से इंस्टॉल की गई Sunshine सेवाएँ स्वतंत्र रखी जाती हैं।

GPL-3.0-or-later; देखें [LICENSE](../../LICENSE), बनाए रखे गए सोर्स नोटिस और [docs/VENDORED.md](../VENDORED.md) में
सूचीबद्ध हर शामिल निर्भरता का लाइसेंस। मूल दस्तावेज़ [README.upstream.md](README.upstream.md) में सुरक्षित हैं।

### डायग्नोस्टिक्स और फ़ीडबैक

डायग्नोस्टिक लॉग डिफ़ॉल्ट रूप से चालू हैं। समस्या दोहराएँ, फिर लॉग ZIP बनाएँ और GitHub issue ड्राफ़्ट खोलें। संलग्न
करने से पहले ZIP की समीक्षा करें: GitHub issue सार्वजनिक होते हैं, और DeskPort आपकी ओर से कुछ भी अपलोड या सबमिट नहीं
करता। देखें [डायग्नोस्टिक डेटा और सीमाएँ](../DIAGNOSTICS.md)।
