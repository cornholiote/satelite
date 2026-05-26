# Ambrosio Satélite (ReSpeaker XVF3800 + XIAO ESP32S3)

Firmware ESPHome para el satélite de voz. Detecta la wakeword "oye ambrosio"
localmente, streamea el audio vía TCP al [Ambrosierver](https://github.com/cornholiote/ambrosierver)
y reacciona con feedback visual en el anillo de LEDs.

## Hardware

- [Seeed Studio ReSpeaker XMOS XVF3800 4-Mic Array with XIAO ESP32S3](https://www.seeedstudio.com/ReSpeaker-XVF3800-4-Mic-Array-With-XIAO-ESP32S3-p-6489.html)

## Componentes principales

- **`micro_wake_word`**: detecta "oye ambrosio" localmente con un modelo TFLite (~50KB).
- **`audio_streamer`** (componente C++ custom en `components/`): tras la wakeword
  abre una conexión TCP al server, streamea PCM 16kHz 16-bit y la cierra al
  detectar silencio (VAD por energía).
- **LEDs SK6812 (12 anillo)**: control vía I2C al chip XMOS XVF3800
  (efecto 3 = single color, color/brillo modulados por C++).
- **ESPHome native API**: control bidireccional desde Ambrosierver
  (rename, blink_color, leds_off) con encryption.
- **Captive portal + web server**: si el WiFi falla, expone un portal de
  configuración. Una vez conectado, web UI en `http://<name>.local`.

## Feedback visual

| Situación | Color |
|---|---|
| Wakeword detectada → escuchando | 🔵 Azul respirando |
| Sin conexión con Ambrosierver | 🔴 Rojo × 2 |
| Intent incompleta | 🟠 Naranja × 2 |
| Intent OK, se va a ejecutar | 🟢 Verde × 2 |

## Setup

### 1. Clonar

```bash
git clone https://github.com/cornholiote/satelite.git
cd satelite
```

### 2. Crear `secrets.yaml`

```bash
cp secrets.yaml.example secrets.yaml
$EDITOR secrets.yaml
```

Rellena:
- WiFi de tu red doméstica
- Encryption key (la **misma** que usa Ambrosierver)
- IP del servidor Ambrosierver en tu LAN

### 3. Instalar ESPHome (si no lo tienes)

```bash
pip install esphome
```

### 4. Compilar y flashear

Primera vez (por USB):

```bash
esphome run respeaker.yaml --device /dev/cu.usbmodemXXXX
```

Sucesivas veces puedes hacer OTA por WiFi:

```bash
esphome run respeaker.yaml
```

### 5. Configurar el nombre del satélite

Tras el primer arranque, entra en `http://respeaker-xvf3800.local` (o por IP)
y cambia el campo "Nombre del Ambrosio" al área donde está (`despacho`,
`salón`, `cocina`, etc.). Eso es lo que se envía a Ambrosierver como
identificador y se usa como área por defecto si el comando no especifica una.

Alternativamente puedes renombrarlo desde la web de Ambrosierver.

## Estructura

```
.
├── respeaker.yaml              # config ESPHome principal
├── secrets.yaml.example        # template (copiar a secrets.yaml)
├── secrets.yaml                # local, gitignored
├── oye_ambrosio.json           # metadata del modelo wakeword
├── oye_ambrosio.tflite         # modelo TFLite
└── components/
    └── audio_streamer/         # componente C++ custom
        ├── __init__.py         # schema ESPHome
        ├── audio_streamer.h
        └── audio_streamer.cpp  # mic + VAD + TCP + header con nombre
```
