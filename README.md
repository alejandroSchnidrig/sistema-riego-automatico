# Sistema de Riego Automático

Sistema de riego automático controlado de forma inalámbrica mediante un **ESP32**
y una aplicación web embebida. Gestiona 8 válvulas solenoides y una bomba de agua (simuladas con LEDs
en las maquetas), organizando la zona de riego en sectores
independientes con programas configurables.

El sistema **no usa sensores**: el comportamiento es 100 % determinístico y se
ejecuta por horario, secuencia y tiempos preconfigurados.

> Proyecto académico de la materia **Construcción de Sistemas** — Universidad
> Nacional de Tres de Febrero (UNTREF), Ingeniería en Computación.

---

## Características

- **Control inalámbrico** vía Wi-Fi: el ESP32 levanta un Access Point y sirve la
  interfaz web; se accede desde celular, tablet o PC sin cables.
- **8 sectores independientes**, cada uno con su válvula, y una **bomba** central.
- **Programas como árbol de riego + caudal:** cada programa es un árbol de nodos;
  cada nodo riega un sector y cuelga de un padre o es raíz. El caudal gobierna
  cuántos sectores riegan en paralelo (`Σ caudal ≤ caudal de la bomba`); los que
  no entran esperan en una cola FIFO.
- **Programación horaria** con RTC (hora de inicio, días de la semana, ciclo
  único o repetición).
- **Control manual** de cada sector y **parada de emergencia** inmediata.
- **Estado en tiempo real** en la web por polling.
- **Persistencia** de la configuración en la memoria flash del ESP32 (LittleFS).
- **Dominio testeable sin hardware** gracias a una capa HAL y tests nativos.

---

## Hardware

| Componente        | Detalle                                                    |
|-------------------|------------------------------------------------------------|
| Microcontrolador  | ESP32 (Wi-Fi + Bluetooth integrado)                        |
| Válvulas          | 8 solenoides → simuladas con LEDs — GPIO 13, 14, 16, 17, 32, 33, 25, 26 |
| Bomba             | 1 bomba central → simuladas con LED — GPIO 27                                  |
| RTC               | DS1302 por bit-banging — CLK → GPIO18, DAT → GPIO19, RST → GPIO21 |
| Persistencia      | Flash interna del ESP32 (LittleFS)                         |

> Los pines, polaridades y credenciales están centralizados en
> [`src/config/Config.h`](src/config/Config.h).

---

## Stack tecnológico

- **Firmware:** C++ sobre el framework Arduino, compilado con **PlatformIO**.
- **Comunicación:** Wi-Fi en modo Access Point + servidor HTTP embebido.
- **API:** REST sobre HTTP (claves JSON en español, parser hand-rolled sin ArduinoJson).
- **Interfaz:** una sola página HTML/CSS/JS *vanilla*, embebida en el firmware.
- **Persistencia:** JSON en LittleFS.
- **Tests:** Unity, ejecutables en el entorno `native` de PlatformIO (sin hardware).

---

## Estructura del proyecto

```
/
├── platformio.ini              # Configuración de PlatformIO (env esp32dev y native)
├── src/
│   ├── main.cpp                # Punto de entrada (setup/loop, cableado de objetos)
│   ├── config/                 # Constantes y pines (Config.h)
│   ├── core/                   # Abstracciones HAL (Arduino, HAL, RTC, Storage)
│   ├── domain/                 # Dominio de negocio (Valve, Pump, Sector,
│   │                           #   ProgramNode, Program, IrrigationSystem)
│   ├── esp32/                  # Implementación HAL real para ESP32
│   ├── pages/                  # UI: index.html (fuente) → index_html.h (generado)
│   ├── scheduler/              # Programación horaria (Scheduler, RTCManager)
│   ├── storage/                # Persistencia en LittleFS (StorageManager)
│   └── web/                    # Servidor HTTP + API REST (WebServer, ApiHandler, JsonHelpers)
└── test/                       # Tests Unity nativos + mocks
    ├── test_domain/            # Tests relacionados al dominio general del sistema
    ├── test_rtc/               # Tests relacionados al modulo RTC
    └── test_storage/           # Tests relacionados al control de almacenamiento
```

El código usa una capa **HAL**: `core/` define las interfaces y `esp32/` las
implementa contra el hardware real. Esto permite compilar y testear el dominio en
el entorno `native` sin necesidad de un ESP32.

---

## Dominio de negocio

| Entidad            | Responsabilidad |
|--------------------|-----------------|
| `Valve`            | Una válvula solenoide: abre/cierra y reporta su estado. |
| `Pump`             | La bomba central: enciende/apaga y reporta su estado. |
| `Sector`           | Zona de riego (id 1..8): posee su válvula. |
| `ProgramNode`      | Nodo del árbol de un programa: sector, tiempo de riego, retardo, padre y caudal. |
| `Program`          | Programa completo como árbol de nodos + hora, días y ciclo. |
| `IrrigationSystem` | Fachada que coordina todo y contiene el motor de ejecución árbol + caudal. |

El motor avanza con cadencia de 1 s usando `millis()` (nunca `delay()`), y en cada
paso recalcula qué válvulas y bomba deben estar activas según los sectores que
riegan, sus retardos y la cañería (ancestros que dejan pasar el agua).

---

## Puesta en marcha

### Requisitos

- [PlatformIO](https://platformio.org/) (CLI o extensión de VS Code).
- Una placa ESP32 (o el entorno `native` para correr los tests sin hardware).

### Compilar y flashear

```bash
pio run                                        # Compilar (env esp32dev)
pio run --target upload                        # Compilar y flashear al ESP32
pio device monitor                             # Monitor serie (115200 baud)
pio run --target upload && pio device monitor  # Flash + monitor
```

### Usar el sistema

1. Una vez flasheado, el ESP32 crea una red Wi-Fi:
   - **SSID:** `Riego-ESP32`
   - **Contraseña:** `riego12345`
2. Conectarse a esa red desde el celular/PC.
3. Abrir en el navegador: **`http://192.168.4.1`**.
4. Configurar programas, sectores y caudal desde la interfaz.

> Credenciales e IP están definidas en `src/config/Config.h`.

### Tests (sin hardware)

Para ejecutar los tests unitarios con el entorno `native` de PlatformIO es necesario disponer de un compilador nativo de C/C++. En **Windows**, la opción recomendada por el equipo es **MinGW**, que incluye GCC (GNU Compiler Collection).

```bash
pio test -e native            # Tests Unity del dominio, RTC y storage
```

> Si se utiliza la extensión de PlatformIO para VS Code, debería aparecer una pestaña de **Testing** en la barra lateral izquierda. Desde allí también es posible ejecutar los tests.

## API REST

| Método   | Endpoint          | Descripción |
|----------|-------------------|-------------|
| GET      | `/estado`         | Estado actual: activos, pendientes, cola, completados y bomba. |
| GET      | `/programas`      | Caudal de la bomba + todos los programas (formato árbol). |
| POST     | `/configuracion`  | Acciones: guardar programa, borrar, ejecutar, fijar caudal. |
| GET      | `/control`        | `?type=sector&id=N&state=0\|1` — encender/apagar un sector manual. |
| POST     | `/parada`         | Parada manual inmediata. |
| GET/POST | `/rtc`            | Leer / fijar la hora del RTC. |

Las claves JSON están en español (contrato con la UI); los miembros C++ en inglés.
Toda la serialización/parsing vive en `web/ApiHandler` + `web/JsonHelpers`.

### Ejemplo — `GET /programas`

```json
{
  "caudalBomba": 20,
  "programas": [
    {
      "id": 1, "horaInicio": "07:00", "dias": 62, "ciclico": false,
      "nodos": [
        { "sectorId": 1, "tiempoRiego": 15, "retardo": 0, "padre": null, "caudal": 12 },
        { "sectorId": 2, "tiempoRiego": 12, "retardo": 3, "padre": 1,    "caudal": 6  }
      ]
    }
  ]
}
```

---

## Integrantes

Di Leo Tomás · Massimino Agustín · Chavez Matías · Schnidrig Alejandro ·
Iannuzzi Gianluca · Biscardi Maximiliano

---

## Licencia

Proyecto académico — UNTREF, Construcción de Sistemas.
