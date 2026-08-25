# Vocalis Captions para OBS

Plugin de transcripción de voz a texto (STT), traducción en tiempo real y renderizado de subtítulos estilizados en vivo para OBS Studio.

El plugin admite dos modos operativos principales: inferencia local y privada mediante [whisper.cpp](https://github.com/ggerganov/whisper.cpp) con aceleración por hardware (Vulkan/CPU), o conexión en streaming a un backend remoto de inteligencia artificial mediante WebSockets y compresión de audio Opus.

[![OBS Studio](https://img.shields.io/badge/OBS%20Studio-30.0%2B%20%7C%2031.0%2B-blue)](https://obsproject.com/)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)](#compilación)
[![Language](https://img.shields.io/badge/Language-C%2B%2B17%20%2F%20Qt6-informational)](#requisitos-de-compilación)

---

## Tabla de Contenidos

- [Características Principales](#características-principales)
- [Arquitectura del Código](#arquitectura-del-código)
- [Instalación y Configuración](#instalación-y-configuración)
- [Modos de Operación](#modos-de-operación)
  - [1. Motor Local (whisper.cpp)](#1-motor-local-whispercpp)
  - [2. Servidor Remoto (WebSocket + Opus)](#2-servidor-remoto-websocket--opus)
- [Especificación del Protocolo de Red](#especificación-del-protocolo-de-red)
- [Catálogo de Modelos Whisper](#catálogo-de-modelos-whisper)
- [Compilación](#compilación)
  - [Requisitos de Compilación](#requisitos-de-compilación)
  - [Instrucciones de Construcción](#instrucciones-de-construcción)
  - [Instalación Automática al Compilar](#-instalación-automática-al-compilar)
- [Diagnóstico y Resolución de Problemas](#diagnóstico-y-resolución-de-problemas)
- [Licencia y Reconocimientos](#licencia-y-reconocimientos)

---

## Características Principales

- **Doble Motor de Inferencia:**
  - **Inferencia Local:** Procesamiento totalmente offline utilizando whisper.cpp. Soporta aceleración gráfica vía Vulkan e instrucciones vectoriales de procesador (AVX, AVX2, FMA, F16C).
  - **Inferencia Remota:** Streaming de baja latencia con códec Opus (16 kHz, mono, 24 kbps VoIP) a servidores WebSocket (`ws://` y `wss://` con TLS / SNI).
- **Detección de Actividad de Voz (VAD):**
  - Integración nativa del modelo neuronal Silero VAD v5 para segmentación precisa de locución y descarte de silencios y ruido de fondo.
  - Mecanismo de contingencia por umbral de energía RMS configurable.
- **Renderizado de Subtítulos Personalizado:**
  - Fuente de video nativa para OBS con soporte de ajuste de texto multilinea (*word-wrap*).
  - Plantillas de estilo predefinidas: Netflix/Cine, Pill Minimalista, Anime/Fansub, Cyberpunk/Neón, Alto Contraste y Personalizado.
  - Personalización tipográfica integral: familias de fuentes, colores RGBA, contornos, sombras paralelas, alineación y fondos con opacidad regulable.
  - Indicador visual de idioma de salida (*Language Badge*).
  - Animaciones de aparición (*fade-in*), redimensionamiento adaptativo (*morphing*) y ocultado automático programable tras inactividad.
- **Panel Dock de Control (Qt6):**
  - Monitorización en tiempo real del estado de captura de voz (Habla, Silencio, Silenciado, Pausa).
  - Selector dinámico de entrada de audio y enlace a la fuente de subtítulos.
  - Gestor de modelos Whisper con descarga directa desde repositorios HuggingFace, visualización de progreso y eliminación de archivos.
  - Controles de intervención inmediata: botón de purga de subtítulos y conmutador de pausa/reanudación.
---

## Arquitectura del Código

El proyecto está modularizado en componentes de procesamiento de audio, clientes de red, renderizado gráfico e interfaces de usuario:

```
obs-vocalis-captions/
├── CMakeLists.txt              # Configuración de compilación, dependencias y descarga de modelos
├── buildspec.json              # Metadatos del módulo para OBS Studio
├── cmake/                      # Scripts auxiliares para empaquetado y dependencias
│   └── download_models.cmake   # Descarga automatizada de modelos base y Silero VAD
├── data/
│   ├── icon.svg                # Recurso gráfico del plugin
│   └── locale/                 # Archivos de traducción de interfaz (.ini)
├── models/                     # Directorio de modelos binarios (.bin)
└── src/
    ├── plugin-main.c           # Punto de entrada y registro del módulo en libobs
    ├── ai_audio_filter.h/.cpp  # Filtro de audio OBS ("Traductor IA"), colas PCM y VAD
    ├── ai_subtitle_source.h/.cpp # Fuente de video OBS ("Subtítulos IA"), layouts y shaders
    ├── audio_processor.h/.cpp  # Fachada de inferencia local sobre whisper.cpp
    ├── remote_transcriber.h/.cpp # Cliente WebSocket asíncrono (Asio / WebSocket++ / Opus)
    ├── subtitle_animator.h/.cpp  # Temporización, mitigación de parpadeo y estados parciales
    └── ui/
        ├── translator_dock.h/.cpp            # Panel Dock integrado en la interfaz de OBS
        ├── translator_settings_dialog.h/.cpp # Diálogo de configuración global
        ├── whisper_model_manager.h/.cpp      # Descargador asíncrono y catálogo de modelos
        └── vector_icons.h                    # Generador de iconos vectoriales en memoria (Qt)
```

---

## Instalación y Configuración

### 1. Configuración del Filtro de Audio
1. En el panel **Mezclador de Audio** de OBS Studio, abra el menú contextual de la fuente de captura (micrófono o audio de escritorio) y seleccione **Filtros**.
2. En la sección **Filtros de audio**, presione el botón **`+`** y añada el filtro **Traductor IA**.
3. Seleccione el idioma de origen de la locución y el idioma de destino deseado.

### 2. Configuración de la Fuente de Salida
1. En el panel **Fuentes**, presione **`+`** y agregue una fuente **Subtítulos IA**.
2. Ajuste la plantilla visual o configure manualmente los parámetros de tipografía, dimensiones y colores.

### 3. Vinculación y Control desde el Dock
1. Acceda al menú superior de OBS: **Paneles (Docks)** -> **Traductor IA**.
2. En el panel:
   - Verifique que el micrófono activo esté seleccionado en **Micrófono / Entrada de Audio**.
   - En **Salida de Subtítulos**, seleccione la fuente de video creada en el paso 2.
   - Seleccione el motor de inferencia (Local o Servidor Remoto).
3. Al emitir señal de voz, el indicador de estado cambiará a color verde y los subtítulos se actualizarán en el lienzo de renderizado.

---

## Modos de Operación

### 1. Motor Local (whisper.cpp)

El motor local ejecuta modelos Whisper en formato GGML directamente en la máquina del usuario, sin requerir conexión a internet ni emitir tráfico de red externo.

- **Aceleración GPU:** Soporte para Vulkan en tarjetas gráficas compatibles.
- **Traducción Directa:** La arquitectura interna de Whisper permite transcripción en el idioma original y traducción directa hacia el inglés de forma nativa.
- **Configuración de Hilos:** Se recomienda asignar entre 2 y 4 hilos de CPU para mantener un equilibrio entre latencia y disponibilidad de recursos para la codificación de video.

### 2. Servidor Remoto (WebSocket + Opus)

Permite delegar la inferencia en un servidor dedicado o instancia en la nube. El audio del micrófono se captura, se re-muestrea a 16 kHz mono, se codifica en tramas Opus y se transmite por WebSockets.

- **Bajo Consumo de Ancho de Banda:** Flujo continuo de voz a ~24 kbps.
- **Compatibilidad con TLS:** Soporte para esquemas `ws://` y `wss://` con verificación de certificados e indicación de nombre de servidor (SNI).
- **Reconexión Automática:** Reintentos asíncronos programados mediante temporizadores no bloqueantes ante desconexiones de red.

---

## Especificación del Protocolo de Red

Para desarrolladores que deseen implementar un servidor compatible (por ejemplo, con Python, FastAPI, Faster-Whisper o vLLM), la especificación de comunicación es la siguiente:

### 1. Negociación y Parámetros de URL
La conexión inicial se realiza mediante una petición WebSocket HTTP GET incluyendo parámetros en la cadena de consulta (*query string*):

```
ws://servidor:puerto/ruta?token=TOKEN_AUTH&lang_in=es&lang_out=en&show_partial=true
```

| Parámetro | Tipo | Descripción |
| :--- | :--- | :--- |
| `token` | Cadena | Clave o token de autenticación (opcional). |
| `lang_in` | Cadena | Código ISO 639-1 del idioma de entrada (`es`, `en`, `auto`, etc.). |
| `lang_out` | Cadena | Código ISO 639-1 del idioma de salida (`en`, `es`, `original`, etc.). |
| `show_partial` | Booleano | `"true"` para solicitar hipótesis intermedias durante el habla. |

### 2. Estructura de Tramas de Audio (Cliente -> Servidor)
Los datos de voz se envían como mensajes binarios continuos estructurados con cabecera y tramas Opus:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   sentence_id (uint32, Little-Endian)         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| is_final (u8) |  len_1 (u16)  |          opus_frame_1 ...     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
| ...           |  len_2 (u16)  |          opus_frame_2 ...     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **Audio Base:** 16.000 Hz, 1 canal (Mono), tamaño de trama de 20 ms (320 muestras).
- **sentence_id (4 bytes):** Identificador secuencial de la frase en curso.
- **is_final (1 byte):** `0` para segmentos parciales continuos; `1` para el cierre de segmento por silencio.
- **Payload Opus:** Secuencia de bloques `[Longitud (uint16 LE)] + [Bytes codificados Opus]`.

### 3. Formato de Respuesta (Servidor -> Cliente)
El servidor debe responder con paquetes de texto codificados en JSON:

```json
{
  "sentence_id": 1,
  "text": "Texto transcrito o traducido por el modelo.",
  "is_final": true
}
```

- `sentence_id` *(entero)*: Identificador correlativo a la frase enviada por el cliente.
- `text` *(cadena)*: Cadena resultante para renderizado.
- `is_final` *(booleano)*: Indica si la transcripción es definitiva (`true`) o una aproximación parcial (`false`).

---

## Catálogo de Modelos Whisper

El gestor de modelos integrado en el panel Dock permite descargar y alternar entre los siguientes pesos en formato GGML:

| Identificador | Tamaño | Velocidad | Precisión | Escenario Recomendado |
| :--- | :--- | :--- | :--- | :--- |
| `ggml-tiny.bin` | ~75 MB | Muy Alta | Básica | Hardware limitado o donde prime la latencia sobre la exactitud |
| `ggml-base.bin` | ~142 MB | Alta | Equilibrada | Configuración estándar recomendada para uso general |
| `ggml-small.bin` | ~466 MB | Media-Alta | Alta | Alta precisión en español y traducción general |
| `ggml-medium.bin` | ~1.5 GB | Media | Muy Alta | Entornos con GPU dedicada |
| `ggml-large-v3-turbo.bin` | ~1.5 GB | Media-Alta | Estado del Arte | Balance óptimo entre precisión v3 y velocidad de inferencia |
| `ggml-large-v3.bin` | ~2.9 GB | Baja | Máxima | Máxima calidad sintáctica y vocabulario técnico |
| `ggml-*-q5_*.bin` | 57 - 547 MB | Alta | Alta | Modelos cuantizados con menor consumo de memoria RAM/VRAM |
| *Personalizado* | Variable | Variable | Variable | Archivos `.bin` locales compatibles con GGML |

---

## Compilación

### Requisitos de Compilación

- **CMake:** Versión 3.28 o superior.
- **OBS Studio:** Versión 30.0 o superior (código fuente o headers de desarrollo y precompilados `obs-deps`).
- **Qt6:** Componentes `Core`, `Widgets` y `Network`.
- **Compilador C++17:**
  - Windows: Visual Studio 2022 (MSVC toolset v143).
  - Linux: GCC 11+ o Clang 14+ con paquetes de desarrollo (`libobs-dev`, `qt6-base-dev`, `libopus-dev`, `libssl-dev`, `pkg-config`).
- **Dependencias Opcionales:**
  - Vulkan SDK para aceleración por GPU en whisper.cpp.
  - OpenSSL para compatibilidad con WebSockets cifrados (`wss://`).

### Instrucciones de Construcción

#### 1. Clonar el repositorio y submódulos
```bash
git clone --recursive https://github.com/josueru444/obs-vocalis-captions.git
cd obs-vocalis-captions
```

En caso de haber clonado sin `--recursive`:
```bash
git submodule update --init --recursive
```

#### 2. Configurar y compilar

**En Linux (Ubuntu / Debian / Fedora / Arch):**

Instalar dependencias necesarias (ejemplo en Ubuntu/Debian):
```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev libobs-dev libopus-dev libssl-dev pkg-config
```

Configurar y compilar:
```bash
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_QT=ON

cmake --build build --config Release -j$(nproc)
```

**En Windows (Visual Studio / Ninja):**
```powershell
cmake -B build_x64 -S . `
  -DCMAKE_BUILD_TYPE=Release `
  -DENABLE_QT=ON

cmake --build build_x64 --config Release
```

---

### Instalación Automática al Compilar

El sistema CMake está preparado para que **no tengas que copiar archivos manualmente**:

1. **Descarga automática de modelos IA:** Durante la configuración inicial de CMake (`cmake -B ...`), se descargarán automáticamente los modelos esenciales (`ggml-tiny.bin`, `ggml-base.bin`, `ggml-small.bin` y Silero VAD) directamente a la carpeta `models/`.
2. **Copia automática del plugin y recursos (Post-Build):**
   - **En Linux:** Al finalizar la compilación, CMake creará los directorios e instalará automáticamente el archivo `.so`, los modelos IA y las traducciones en:
     ```
     ~/.config/obs-studio/plugins/obs-vocalis-captions/
     ├── bin/64bit/obs-vocalis-captions.so
     └── data/
         ├── models/
         └── locale/
     ```
   - **En Windows:** Al compilar, CMake copiará automáticamente el archivo `.dll`, `.pdb`, modelos y traducciones a:
     ```
     C:/Program Files/obs-studio/
     ```

> Una vez finalice `cmake --build`, simplemente abre **OBS Studio** y el plugin estará instalado y listo para usarse.

---

## Diagnóstico y Resolución de Problemas

- **Los subtítulos no se visualizan al hablar:**
  - Confirme que la fuente `Subtítulos IA` esté seleccionada en el menú desplegable **Componente a usar** dentro de las propiedades del filtro o del panel Dock.
  - Verifique el estado de captura en el panel Dock. Si permanece en *Silencio*, reduzca el **Umbral de Silencio (RMS)** en las opciones avanzadas o verifique la ganancia del micrófono.
  - Compruebe que el modelo Whisper seleccionado haya finalizado su descarga o que el cliente WebSocket se encuentre en estado `Conectado`.

- **Sobrecarga de CPU o pérdida de fotogramas durante la transmisión:**
  - Reduzca el tamaño del modelo seleccionado a `Tiny` o `Base`.
  - Habilite la aceleración por hardware en las opciones del filtro (*Usar Tarjeta de Video (GPU)*) si dispone de soporte Vulkan.
  - Ajuste el número de hilos de cómputo en la configuración local de Whisper a un valor entre 2 y 4.

- **Fallo de conexión en servidores WebSocket seguros (`wss://`):**
  - Verifique que la compilación haya detectado OpenSSL. En entornos Windows, asegúrese de que las librerías dinámicas `libssl` y `libcrypto` estén presentes.
  - Si utiliza túneles de desarrollo como ngrok, el cliente incluye automáticamente cabeceras para omitir pantallas intermedias de confirmación (`ngrok-skip-browser-warning: true`).

---

## Licencia y Reconocimientos

Este software está licenciado bajo los términos de la **GNU General Public License v2.0** (GPL-2.0). Para más detalles, consulte el archivo [LICENSE](LICENSE).

### Proyectos Utilizados
- [OBS Studio](https://obsproject.com/) - Plataforma y API de plugins.
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) - Motor de inferencia en C/C++ para modelos Whisper.
- [Silero VAD](https://github.com/snakers4/silero-vad) - Detección neuronal de actividad de voz.
- [Opus Interactive Audio Codec](https://opus-codec.org/) - Códec de compresión de audio.
- [WebSocket++](https://github.com/zaphoyd/websocketpp) y [Asio C++ Library](https://think-async.com/Asio/) - Capa de transporte y red asíncrona.
