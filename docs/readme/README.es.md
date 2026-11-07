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
  <b>Español</b> ·
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
  Un espacio de trabajo de escritorio remoto basado en Moonlight y Sunshine.<br>
  Mantén tu escritorio remoto listo en segundo plano, tráelo a tu espacio de trabajo
  actual con una sola acción y guárdalo sin volver a conectar.
</p>

<p align="center">
  <a href="https://github.com/keithxc/deskport/releases/tag/v0.7.3"><img alt="Desktop release" src="https://img.shields.io/badge/desktop-0.7.3-71e0c3"></a>
  <a href="https://apps.apple.com/us/app/deskport/id6812389978"><img alt="App Store" src="https://img.shields.io/badge/App%20Store-iPhone%20%26%20iPad%20%C2%B7%20%244.99-0a84ff?logo=apple&logoColor=white"></a>
  <a href="https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk"><img alt="Android 2.7 APK" src="https://img.shields.io/badge/Android-2.7%20APK-206d3a?logo=android&amp;logoColor=white"></a>
  <a href="../../LICENSE"><img alt="License" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
</p>

---

## Funciones principales

- **Una sola app de escritorio para ver y compartir.** Sunshine integrado, aprobación de dispositivos,
  credenciales y ajustes independientes; sin instalación aparte de Sunshine en los
  paquetes de host para macOS, Windows y Linux (no en el Flatpak solo visor).
- **Un espacio de trabajo que sigue a tu cliente.** Tamaño adaptativo en hosts macOS
  y KDE compatibles, HiDPI, diseños móviles vertical/horizontal y ajuste de tamaño por dispositivo.
  Los cambios de tamaño reconectan el vídeo brevemente; no son cambios de codificador sin interrupciones.
- **Elige la política de pantalla del host.** Pantalla principal virtual con pantallas duplicadas,
  pantalla principal virtual con las demás pantallas desactivadas o un espacio de trabajo extendido en hosts
  compatibles. DeskPort restaura la disposición guardada cuando termina la sesión.
- **Muévete entre dispositivos autorizados.** Confirma antes de tomar el control de una sesión
  activa; el cliente desplazado recibe una explicación autenticada de la toma de control.
- **Mantén el trabajo de escritorio a mano.** Oculta y recupera un visor conectado, libera
  la entrada remota al ocultarlo y desconecta la visualización independientemente del uso compartido local.
- **Guarda ajustes por equipo.** Imagen, audio, entrada, ajuste del escritorio y edición de
  direcciones se quedan con el dispositivo; tarjetas, fijados, temas e interfaces localizadas.
- **Comparte el portapapeles del escritorio bajo demanda.** El texto se actualiza al instante;
  las imágenes y archivos compatibles se obtienen cuando se solicitan, entre pares de escritorio vinculados
  que lo hayan activado. Consulta [alcance y límites del portapapeles](../CLIPBOARD.md).
- **Controles móviles nativos.** iPhone/iPad y el cliente Android ofrecen
  entrada táctil/trackpad, controles de teclado y acceso directo al escritorio remoto.
- **Inicio opcional al iniciar sesión y recuperación en macOS.** Requieren una sesión gráfica
  activa y aprobación del sistema; no desbloquean FileVault ni inician sesión por ti.

## Vista previa de la interfaz

Capturas de la interfaz actual con dispositivos de demostración sintéticos. Haz clic en un panel
para ver la imagen a tamaño completo; estas vistas previas no implican disponibilidad en tiendas ni en versiones publicadas.

| Dispositivos y grupos | Ajustes de pantalla por dispositivo | Manual integrado |
| --- | --- | --- |
| [![Dispositivos y grupos](../media/devices.png)](../media/devices.png) | [![Ajustes de pantalla por dispositivo](../media/device-settings.png)](../media/device-settings.png) | [![Manual integrado](../media/manual.png)](../media/manual.png) |

## Plataformas compatibles

Los siguientes enlaces corresponden a la versión oficial 0.7.3. Las comprobaciones de paquetes y de ejecución tienen alcances distintos; consulta los resultados y las limitaciones en el [informe de verificación](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

| Plataforma | Función | Estado de la versión | Descarga |
| --- | --- | --- | --- |
| macOS Apple Silicon, macOS 26+ | Visor + host + pantalla virtual nativa | **0.7.3 estable**; firmas Developer ID, notarización de Apple y Gatekeeper tras extraer el ZIP verificados | [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg) |
| Linux x86_64 | Visor + host; espacio de trabajo virtual adaptativo en hosts KWin, Mutter y Hyprland compatibles | **0.7.3 estable**: DEB, RPM, Arch, AppImage, archivo portable y Nix | [Versión](https://github.com/keithxc/deskport/releases/tag/v0.7.3) · [guía de instalación](../LINUX_PACKAGES.md) |
| Linux x86_64 Flatpak | Solo visor | **0.7.3 estable**, Freedesktop Platform 25.08 | [Versión](https://github.com/keithxc/deskport/releases/tag/v0.7.3) |
| iPhone / iPad, iOS/iPadOS 15+ | Solo cliente | App Store; las compilaciones más recientes se prueban por separado en TestFlight | [App Store](https://apps.apple.com/us/app/deskport/id6812389978) |
| Android 8.0+ | Solo cliente | **2.7 (63)** · Prueba cerrada de Google Play en curso | [Descargar APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) |
| Windows x64 | Visor + host + pantalla virtual | **0.7.3 estable**; instalador con host; ZIP portable solo como visor; paquete y CLI verificados sin instalar; aplicación e instalador sin firma | [Instalador](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-windows-x64-setup-full.exe) |
| Linux ARM64 / Mac Intel | No cualificado | Sin paquete de versión verificado | — |

> **Aviso sobre antivirus en Windows:** Es posible que algún antivirus marque los componentes auxiliares de DeskPort para Windows como un falso positivo.
> No lo hemos detectado en nuestras propias pruebas. Mantén Defender activado; si aparece una advertencia,
> consulta el informe de verificación de la versión y el [historial de muestras](../WINDOWS_DEVELOPMENT.md#windows-antivirus-notice).

Los paquetes nativos de Linux requieren glibc 2.39+: Ubuntu 24.04+/Debian 13+, Fedora 44,
Arch actual. El AppImage incluye un runtime privado para escritorios con glibc más antigua;
los controladores de hardware y el soporte del compositor siguen determinando las capacidades de host. El host de pantalla virtual en KDE necesita
entradas de permisos del compositor. Los paquetes nativos/Nix aportan entradas fijas; las compilaciones
AppImage y portables registran automáticamente la ruta actual del helper de pantalla
al empezar a compartir. La entrada en el host sigue requiriendo permisos de dispositivo. Las apps móviles
no actúan como host de escritorios.
Una definición de código fuente o una compilación exitosa por sí solas no constituyen una plataforma compatible.

## Instalación

**macOS:** Abre el [DMG](https://github.com/keithxc/deskport/releases/download/v0.7.3/DeskPort-0.7.3-macos-arm64.dmg),
arrastra DeskPort a Aplicaciones y ábrelo. Concede los permisos de Grabación de pantalla y
Accesibilidad para usar las funciones de host. No se necesitan Nix ni Homebrew.

**Linux:** Elige el [paquete 0.7.3](https://github.com/keithxc/deskport/releases/tag/v0.7.3)
para tu sistema y sigue la [guía de instalación](../LINUX_PACKAGES.md),
incluidos los permisos de entrada del host. Con Nix: `nix run github:keithxc/deskport/v0.7.3`.
Flatpak solo permite visualizar. Los recursos de la versión incluyen [sumas de verificación SHA-256](https://github.com/keithxc/deskport/releases/download/v0.7.3/SHA256SUMS.txt) y un [informe de verificación](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt).

**Android 2.7 (63)** · Hasta que termine la prueba cerrada de Google Play, el [APK](https://github.com/keithxc/deskport/releases/download/v0.7.8/DeskPort-2.7-63.apk) está disponible temporalmente en GitHub. Los clientes móviles son de código cerrado. Aquí solo se distribuye el instalador.

La compilación 63 usa la misma clave de firma que Google Play. Al actualizar a una versión posterior de Play se conservan los equipos vinculados y los ajustes. Si instalaste la compilación 62 de GitHub, debes desinstalarla antes de instalar la 63. Se borrarán los ajustes locales y tendrás que volver a vincular los equipos.

**iPhone / iPad:** Instala [DeskPort desde la App Store](https://apps.apple.com/us/app/deskport/id6812389978).
La app móvil es una compra única de pago; consulta tu tienda para ver el precio local actual.
Las versiones de escritorio siguen siendo gratuitas y de código abierto. Las compras financian la firma,
el hardware de pruebas y el desarrollo continuo.

Inicia Compartir en el host, selecciónalo en el cliente y aprueba el dispositivo en el
host. Ambos dispositivos necesitan una ruta de red funcional: usa una LAN o tu propia VPN, como
Tailscale. DeskPort no ofrece servicio de cuentas, escritorios alojados, relé
ni túnel de red. El emparejamiento clásico por PIN de Sunshine sigue disponible.

**0.7.3 estable:** Esta versión incorpora un catálogo de interfaz compartido en 22 idiomas con cambio de idioma en el navegador y mejora la función de compartir en sesiones de Windows con sesión iniciada y el DPI de las pantallas virtuales, el runtime de Linux y el portapapeles de Wayland, los espacios de trabajo Hyprland/X11 protegidos, el IME local opcional y las tarjetas cuadradas de dispositivos. Consulta los cambios y los límites de verificación en las [notas de la versión 0.7.3](../releases/RELEASE_0.7.3.md).

**Alcance de la verificación de 0.7.3:** En Windows pasaron la comparación exacta del contenido, las comprobaciones PE y de CLI nativa, y el análisis de Defender sin detecciones; no se probaron la instalación, la conexión gráfica ni el streaming. El AppImage final pasó las pruebas en una VM de Bazzite con FUSE normal y GPU por software. En la VM de Ubuntu 26.04, el DEB de 0.7.2 reprodujo el fallo de VA; el DEB final de 0.7.3 lo corrigió y pasó la actualización. La reversión reprodujo el fallo de la versión anterior; la desinstalación posterior conservó la configuración sintética de prueba. La captura y recuperación en GNOME siguen sin aceptación. Los resultados de contenedores/Flatpak y las demás limitaciones están en el [informe de verificación](https://github.com/keithxc/deskport/releases/download/v0.7.3/VERIFICATION.txt); no equivalen a una validación completa en hardware físico.

**Histórico: 0.7.2 estable:** La [versión de escritorio](https://github.com/keithxc/deskport/releases/tag/v0.7.2) reúne mejoras de redimensionado, streaming remoto, ahorro de datos y runtime de Linux, y corrige problemas del AppImage en Bazzite/KDE Wayland. Consulta las [notas de la versión 0.7.2](../releases/RELEASE_0.7.2.md); los pasos de instalación por distribución Linux están en el [README en inglés](../../README.md#linux-installation-by-distribution).

**Acceso desde el navegador:** Los hosts de escritorio sirven un escritorio remoto HTTPS con un
código de inicio de sesión de 30 segundos, emparejamiento opcional del navegador durante siete días, tamaño adaptativo
y pantalla completa. Consulta las [notas de la versión 0.7.1](../releases/RELEASE_0.7.1.md) y la
[guía del navegador](../BROWSER_ACCESS.md). El AppImage incluye un runtime privado de glibc;
los paquetes nativos mantienen su requisito de glibc 2.39.

### Disponibilidad en repositorios de Linux

| Canal | Estado | Instalación |
| --- | --- | --- |
| Paquetes de versión en GitHub | 0.7.3 estable | Descarga arriba el paquete para tu distribución |
| Flake de Nix upstream | 0.7.3 estable | `nix run github:keithxc/deskport/v0.7.3` |
| AUR `deskport-bin` | Receta probada; publicación bloqueada por la pausa en el registro de cuentas nuevas | [Compila la receta localmente](../LINUX_PACKAGES.md#arch-linux-and-aur) |
| Nixpkgs `deskport` | [PR #572043](https://github.com/NixOS/nixpkgs/pull/572043) enviado para revisión; sin fusionar | Usa el flake upstream hasta su inclusión |
| Repositorios oficiales de Arch / Flathub | No listado | Usa las descargas de la versión; Flatpak es solo visor |

El paquete de Arch descargable se instala con `pacman -U`; no hace que
`pacman -S deskport` esté disponible. Del mismo modo, el flake upstream no hace que
`pkgs.deskport` esté disponible en Nixpkgs. La entrada del host, los permisos de captura y la configuración
del cortafuegos son independientes de la instalación de la aplicación; consulta la
[guía de instalación en Linux](../LINUX_PACKAGES.md#nixos-and-nix).

## Comparación con productos similares

Esta es una comparación de flujo de trabajo y plataformas, verificada con la documentación oficial
enlazada el **2026-09-20**; no es una prueba de latencia ni de calidad de imagen. Las funciones
de los productos y los límites de plataforma pueden cambiar.

| Aspecto | DeskPort | Moonlight + Sunshine | RustDesk | Parsec |
| --- | --- | --- | --- | --- |
| Flujo de trabajo principal | Espacio de trabajo de escritorio remoto persistente, tamaño adaptativo y traspaso entre dispositivos | Streaming de juegos/escritorio mediante apps cliente y host separadas | Control remoto general y soporte | Escritorio remoto interactivo y colaboración |
| Plataformas de host de escritorio | macOS Apple Silicon, Linux x86_64 y Windows x64 | Windows, macOS, Linux y FreeBSD, según los límites de cada plataforma | Windows, macOS y Linux | Windows y macOS; Linux no puede actuar como host |
| Clientes de escritorio | macOS Apple Silicon, Linux x86_64 y Windows x64 | Windows, macOS y Linux, entre otros | Windows, macOS y Linux | Windows, macOS, Linux y navegadores Chromium compatibles |
| Clientes móviles | iPhone/iPad (App Store) · Android (GitHub APK) | iOS/iPadOS y Android | iOS/iPadOS y Android; iOS no puede actuar como host | Android; sin cliente para iOS/iPadOS |
| Modelo de configuración | Host integrado en los paquetes nativos de escritorio; aprueba dispositivos en DeskPort | Instala y configura Sunshine por separado y luego empareja Moonlight | Infraestructura de servidores pública o servidor autoalojado | Cuenta y aplicación de Parsec |
| Modelo de red/servicio | Aporta tu propia accesibilidad por LAN/VPN; sin relé de DeskPort | Streaming autoalojado; configura la accesibilidad de red | Infraestructura pública o servidor OSS/Pro autoalojado | Infraestructura de cuenta/servicio de Parsec |
| Código y distribución | Escritorio GPL, paquetes de escritorio gratuitos; app de Apple de pago distribuida por separado | Clientes y host de código abierto | Cliente de código abierto y servidor OSS; opción comercial de servidor Pro | Aplicación/servicio propietario |

Fuentes: [Moonlight](https://moonlight-stream.org/),
[Sunshine installation and platform support](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html),
[RustDesk clients](https://rustdesk.com/docs/en/client/),
[RustDesk self-hosting](https://rustdesk.com/docs/en/self-host/),
[Parsec platform compatibility](https://support.parsec.app/hc/en-us/articles/32381568346644-Hardware-and-Software-Compatibility),
[Parsec Linux setup](https://support.parsec.app/hc/en-us/articles/32381552552340-Install-Parsec-App-on-Linux).
Los límites de plataforma actuales de DeskPort se indican arriba; esta tabla no implica
paridad de funciones en todos los sistemas operativos ni una ventaja de rendimiento medida.

Consulta las [notas de la versión 0.7.3](../releases/RELEASE_0.7.3.md),
la [arquitectura](../ARCHITECTURE.md) y la [hoja de ruta](../ROADMAP.md).

## Compilar y ejecutar en Linux

Con Nix y flakes habilitados:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

Las dependencias de código de terceros del visor están incluidas en el repositorio (vendored); el núcleo compartido y
las entradas de compilación de Nix siguen siendo dependencias externas fijadas. Las compilaciones nativas deben inicializar
`shared/deskport-core`. Consulta [docs/VENDORED.md](../VENDORED.md).
Para la configuración de SSH, la gestión local del host, los requisitos de VPS y Omarchy/Hyprland,
consulta la [guía de la CLI](../CLI.md). `deskport doctor` comprueba los requisitos para actuar como host;
`deskport status --json` inspecciona la instancia en ejecución sin abrir una ventana.
`nix run . -- --help` muestra la interfaz de línea de comandos. Inicia Compartir en el host y vincula los dispositivos antes de conectar. El emparejamiento
clásico por PIN de Sunshine también está disponible. No se incluye ninguna credencial personal de host
ni de emparejamiento, ni se importa desde Moonlight.
Las nuevas direcciones manuales usan por defecto el puerto de DeskPort `48989`. Incluye el puerto que aparece
en la página de compartir del host si es distinto, o usa `host:47989` para conectar explícitamente
a una instalación independiente de Sunshine con la configuración predeterminada. Los endpoints guardados/descubiertos conservan
sus propios puertos.

Para una compilación nativa editable:

```sh
git submodule update --init shared/deskport-core
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

Los nombres de archivo del proyecto upstream se mantienen sin cambios para que el fork siga siendo fácil de revisar.

## ¿Qué lo diferencia de Moonlight?

- Host Sunshine integrado con aprobación de dispositivos, toma de control y ajustes por dispositivo.
- Las sesiones siguen conectadas mientras el visor está oculto; recupéralo desde la bandeja/barra de menús.
- Acceso desde el navegador por HTTPS servido por los hosts de escritorio.
- Ejecutable `deskport` independiente, espacio de nombres de ajustes Qt `DeskPort` e
  ID de aplicación Linux `io.github.keithxc.DeskPort`.
- Streaming en ventana y control con puntero absoluto por defecto.
- Silencio al perder el foco; optimización para juegos, ratón con mando, modo multimando,
  entrada de mando en segundo plano y presencia en Discord desactivados por defecto.
- Sin avisos de actualización de Moonlight upstream para esta aplicación independiente.
- Un entorno Nix fijado y un flujo de compilación para Linux.

La interfaz de escritorio ofrece páginas de dispositivos, uso compartido y ajustes, con selección
de idioma y permisos de host separados. Cerrar el visor mantiene su sesión disponible y abre la lista de dispositivos.
Un dispositivo activo ofrece Volver al escritorio; los demás dispositivos muestran detalles hasta que se
desconecta la sesión actual. Fija los dispositivos que usas a menudo y elige una lista compacta
o tarjetas. La apariencia sigue al sistema, con opciones para forzar modo claro u oscuro.

## Alcance de plataformas

Consulta la [tabla de plataformas](#plataformas-compatibles) de arriba para ver el estado de cada
plataforma. Las versiones de escritorio cubren macOS Apple Silicon, Linux x86_64 y Windows x64
como visor y como host; las apps móviles son solo clientes.

El soporte de plataformas cliente y el soporte de host son independientes: un host Mac no requiere
un cliente DeskPort para Mac. La decodificación por hardware, la entrada en vivo, la calidad de imagen y la latencia de recuperación requieren
pruebas en sesiones reales; una compilación exitosa no las garantiza.

## Próximo hito

Mantener una sesión conectada durante **50 ciclos de ocultar/mostrar**, mostrar la ventana en el
espacio de trabajo actual y devolver la entrada local de forma fiable. Medir la latencia de fotogramas nuevos
y el uso de recursos en segundo plano por separado de la aparición de la ventana.

Consulta [la hoja de ruta](../ROADMAP.md) para ver los criterios de aceptación y las funciones aplazadas,
y las [notas upstream](../UPSTREAM.md) sobre procedencia y límites de mantenimiento.

## Validación

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

La prueba de humo usa directorios temporales de configuración/caché XDG y una plataforma Qt
fuera de pantalla. No se empareja con ningún host, no inicia un stream ni inyecta entrada.

## Controles diarios del escritorio

Cerrar una ventana mantiene DeskPort en ejecución en la bandeja/barra de menús. Usa **Open DeskPort**
para recuperarlo, **Disconnect viewer** para terminar solo la conexión actual o
**Quit DeskPort** para salir del servicio. El uso compartido local continúa cuando se cierra un visor.

El uso compartido del portapapeles de texto plano y la captura de atajos del sistema están activados por defecto en
ambos dispositivos vinculados. Los cambios de ajustes se aplican tras volver a conectar. Solo se comparten las copias nuevas,
de hasta 1 MiB. Las imágenes y archivos compatibles se obtienen bajo demanda entre
pares de escritorio compatibles que lo hayan activado; consulta los [límites](../CLIPBOARD.md). En el modo de puntero
de escritorio, el enrutamiento del teclado sigue al puntero dentro del vídeo enfocado.
**Ctrl+Alt+Shift+Z** libera la entrada;
**Ctrl+Alt+Shift+Q** desconecta el visor. Haz clic dentro para recuperar la entrada tras
liberarla explícitamente. Los atajos reservados por el sistema operativo dependen del compositor del escritorio.

El inicio al iniciar sesión y la recuperación requieren una sesión gráfica activa. Consulta
[comprobaciones de aceptación y limitaciones](../INPUT_SERVICE_ACCEPTANCE.md) antes de depender
de un equipo para acceso desatendido.

## Licencia y atribución

DeskPort es un derivado independiente de [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
basado inicialmente en v6.1.0. No es una versión oficial de Moonlight ni de Sunshine.
Moonlight proporciona la base de streaming; [Sunshine](https://github.com/LizardByte/Sunshine)
se incluye en los paquetes de host para macOS, Windows y Linux (no en el Flatpak solo visor) y lo proporciona el paquete Nix de Linux.
Los servicios de Sunshine instalados por separado se mantienen independientes.

GPL-3.0-or-later; consulta [LICENSE](../../LICENSE), los avisos de código conservados y la
licencia de cada dependencia incluida que figura en [docs/VENDORED.md](../VENDORED.md). La documentación original se conserva en
[README.upstream.md](README.upstream.md).

### Diagnóstico y comentarios

Los registros de diagnóstico están activados por defecto. Reproduce el problema,
luego crea un ZIP de registros y abre un borrador de issue en GitHub. Revisa el ZIP antes de adjuntarlo:
los issues de GitHub son públicos y DeskPort no sube ni envía nada por
ti. Consulta [datos de diagnóstico y límites](../DIAGNOSTICS.md).
