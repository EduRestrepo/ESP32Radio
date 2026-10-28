# 📻 TinyRadio - Radio de Internet con ESP32-C3 Mini

Un reproductor de radio por internet moderno y completo para **ESP32-C3 Mini** con interfaz web interactiva, sistema de favoritos, filtrado por géneros musicales, **portal cautivo automático** y **reproducción dual** (en el navegador o en altavoz físico mediante DAC I2S).

---

## 📶 Funcionamiento del Portal Cautivo para WiFi

Cuando el ESP32-C3 se enciende y no tiene ninguna red WiFi guardada (o si tu router está apagado / fuera de alcance):

1. **El ESP32 activa automáticamente un punto de acceso (SoftAP)**:
   - **Nombre de red (SSID)**: `TinyRadio-Setup`
   - **Seguridad**: Red abierta *(sin contraseña)*
   - **IP del dispositivo**: `192.168.4.1`

2. **Detección Automática (Portal Cautivo)**:
   - Gracias al servidor DNS interno (`DNSServer`) y a las rutas de detección integradas (`/generate_204` para Android, `/hotspot-detect.html` para iPhone/iPad/Mac, y `/ncsi.txt` para Windows), al conectarte a la red `TinyRadio-Setup` tu teléfono o PC mostrará de inmediato la notificación:
     > **"Iniciar sesión en la red WiFi"** / **"Sign in to network"**
   - Al pulsar la notificación, se abre directamente la pantalla de configuración sin necesidad de escribir la IP manualmente.

3. **Escaneo y Conexión**:
   - La pantalla muestra las redes WiFi disponibles en tu casa con su nivel de señal.
   - Seleccionas tu red, introduces la contraseña y haces clic en **Conectar ESP32**.
   - El ESP32 guarda los datos en su memoria flash LittleFS, se reinicia y se conecta automáticamente a tu red doméstica. A partir de ese momento queda accesible en tu red local en `http://tinyradio.local`.

---

## ⚡ ¿Puedo conectar un altavoz directamente al ESP32-C3 sin DAC?

> [!WARNING]
> **No se recomienda conectar un altavoz de 4Ω u 8Ω directamente a los pines del ESP32-C3.**
> 
> 1. **Riesgo de sobrecorriente**: Un altavoz de 8Ω a 3.3V demandaría más de **400 mA** de corriente ($I = \frac{3.3\text{V}}{8\Omega} = 0.412\text{A}$). Los pines GPIO del ESP32-C3 tienen un límite de **20 a 40 mA**. Conectarlo directo puede dañar el pin o reiniciar el chip continuamente (*brownout*).
> 2. **Sin DAC analógico integrado**: El ESP32-C3 (núcleo RISC-V) no tiene DAC analógico interno (a diferencia del antiguo ESP32 clásico).

### Las 2 Soluciones Implementadas:
- **Audio Web (Inmediato, sin comprar nada)**: Escuchas cualquier emisora desde los altavoces de tu móvil, tablet o PC a través del reproductor web integrado sincronizado.
- **Altavoz Físico (Calidad Hi-Fi Digital)**: Conectar un módulo **MAX98357A** (~$1.50 - $2.50 USD/€), que incluye un DAC digital I2S y un amplificador Clase D de 3W para conectar directamente cualquier altavoz.

---

## 🔌 Pines de Conexión (con Módulo DAC MAX98357A)

| Pin MAX98357A | Pin ESP32-C3 Mini | Función |
| :--- | :--- | :--- |
| **BCLK** | **GPIO 6** | Bit Clock I2S |
| **LRC** (WS) | **GPIO 7** | Left/Right Clock I2S |
| **DIN** | **GPIO 8** | Data In I2S |
| **VIN** | **5V** (o VBUS) | Alimentación (5V recomendado para 3W) |
| **GND** | **GND** | Masa común |
| **SPK+ / SPK-**| **Altavoz (4Ω - 8Ω)** | Bornes del altavoz |

*(Pines configurables en [`include/config.h`](file:///c:/Users/ERQ/Desarrollos/TinyRadio/include/config.h))*.

---

## 🚀 Pasos para Compilar y Cargar con PlatformIO (VS Code)

1. Abre la carpeta del proyecto en **VS Code**.
2. Conecta tu ESP32-C3 por USB.
3. En la barra lateral izquierda, abre la pestaña de **PlatformIO** (icono de la hormiga):
   - Ve a `esp32-c3-devkitm-1` &rarr; `Platform` y pulsa:
     👉 **Upload Filesystem Image** *(sube la interfaz web a LittleFS)*.
   - En `General`, pulsa:
     👉 **Upload** *(compila y sube el firmware)*.
4. Conéctate a la red `TinyRadio-Setup`, configura tu WiFi en el portal cautivo ¡y listo!
