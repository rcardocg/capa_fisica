# Implementación — Protocolo de Capa Física No Convencional

Documento de contexto y planificación por fases para el proyecto de Capa Física.
Su objetivo es que cualquier integrante pueda retomar el trabajo en otra sesión o
en otro equipo sin perder las decisiones tomadas.

- **Proyecto:** protocolo de comunicación de Capa Física no convencional.
- **Plataforma objetivo:** BeagleBone Black Rev C.
- **Desarrollo inicial:** equipo de escritorio, pruebas de terminal a terminal.
- **Última actualización:** 2026-11-13.

---

## 1. Cómo usar este documento

Al iniciar una sesión:

1. Leer `README.md` (restricciones reales del hardware).
2. Leer este documento completo, especialmente las decisiones cerradas y la
   sección de estado.
3. Revisar la bitácora y el próximo paso.
4. Ejecutar las pruebas descritas en la sección de pruebas.
5. Actualizar la bitácora al terminar la sesión.

Al finalizar una sesión, agregar una fila a la bitácora con:

- Fecha.
- Fase o módulo trabajado.
- Qué quedó terminado.
- Qué quedó pendiente.
- Problemas encontrados.

---

## 2. Contexto del proyecto

Se debe fabricar e implementar un protocolo de comunicación de Capa Física no
convencional. La BeagleBone Black actúa como unidad de procesamiento. La
información se transmite mediante dispositivos físicos construidos por el
estudiante, y el resultado se visualiza como texto en una sala de chat.

La implementación debe integrar, dentro de un mismo programa, las capas:

- Aplicación: sala de chat mediante Web Server.
- Presentación: UTF-8 completo y Extended ASCII.
- Enlace de datos: CRC, ACK/NACK, retransmisión y recuperación.
- Física: medios no convencionales, canales, reloj, sincronización, trama.

Las capas son una separación lógica y funcional; no son programas separados.

### Entregables principales

- Dos equipos funcionales con comunicación bidireccional.
- De 3 a 8 canales físicos por sentido, incluyendo un canal de reloj.
- Mínimo 3 medios físicos reales no convencionales.
- Trama PHY con SYN, SFD, Control, DATA y CRC como mínimo.
- DATA y CRC de exactamente 8 bits por Frame.
- Cuatro algoritmos CRC-8 seleccionables.
- UTF-8 de rango completo (1, 2, 3 y 4 bytes) y Extended ASCII.
- ACK/NACK y retransmisión funcionales.
- Sincronización y reconstrucción de frames en recepción.
- LOG de frames, CRC, ACK/NACK, errores y retransmisiones.
- Web Server / sala de chat funcional.
- Prueba de error intencional: CRC -> NACK -> retransmisión.
- Código fuente y documentación técnica en ZIP para el GES.
- Calificación presencial; la entrega por GES no tiene nota.

---

## 3. Restricciones reales del hardware

Tomadas de `README.md`:

- Plataforma objetivo: BeagleBone Black Rev C.
- Sistema operativo: Debian GNU/Linux 9 Stretch.
- Kernel: 4.14.108-ti-r119.
- Python 3: 3.5.3 obligatorio en runtime.
- Python 2: no utilizar.
- No usar f-strings.
- No usar dataclasses.
- No usar sintaxis o APIs exclusivas de versiones modernas de Python.
- Priorizar Standard Library.
- No asumir compatibilidad de las últimas versiones de paquetes.
- Fijar versiones de dependencias externas si se agregan.
- Evaluar Adafruit_BBIO 1.1.0 como primera alternativa GPIO.
- Verificar compatibilidad de GPIO y pines antes de asumir disponibilidad.
- Considerar pin mux.
- Evaluar PRU-ICSS del AM3358 como alternativa determinista.
- Mantener separada la interfaz Python de la implementación GPIO/PRU.
- No implementar PRU hasta justificarlo con pruebas de temporización.
- La migración debe poder ser offline.
- No copiar el entorno virtual del escritorio.
- Crear el venv en la BeagleBone.
- Preparar wheels ARM compatibles con Python 3.5 si se usan dependencias.
- Separar dependencias de desarrollo y hardware.

Antes de la migración verificar en la BeagleBone:

```bash
uname -m
dpkg --print-architecture
python3 --version
pip3 --version
ls /dev/gpiochip* 2>/dev/null
ls /sys/class/remoteproc 2>/dev/null
```

La prioridad es que el backend funcione en el hardware existente, sin modernizar
el sistema operativo innecesariamente.

---

## 4. Decisiones cerradas

| Tema | Decisión |
|---|---|
| Canales por sentido | 4 canales: 1 reloj + 3 carriles de datos |
| Medios de datos | Luz, sonido y vibración |
| Canal de reloj | GPIO directo, dedicado |
| Cantidad de medios distintos | 3 medios no convencionales reales |
| Arquitectura del enlace | Full-duplex: N canales TX y N canales RX por equipo |
| Servidor web | Solo Standard Library: `http.server` |
| Extended ASCII | Latin-1 (ISO-8859-1) |
| Codificación por defecto | UTF-8 |
| CRC por defecto | SMBUS |
| Trama PHY | SYN 16 + SFD 4 + CONTROL 4 + SEQ 4 + DATA 8 + CRC 8 |
| Tamaño de trama | 44 bits base; 45 bits en línea con 3 carriles por 1 bit de relleno |
| Reloj inicial | 100 Hz, configurable |
| Timeout de enlace inicial | 1000 ms, configurable |
| Reintentos máximos iniciales | 3, configurable |
| Secuencia | 4 bits, circular 0..15 |
| ARQ | Stop-and-wait con ACK/NACK, timeout, reintentos y deduplicación |
| Delimitador de mensaje del demo | `\n` a nivel de aplicación |
| Destino de pruebas iniciales | Terminal a terminal, sin Web ni medio físico todavía |

Notas:

- El reloj directo por GPIO fue aceptado explícitamente. Los tres medios no
  convencionales se usan en los tres carriles de datos.
- Cada equipo debe tener su propio conjunto de 4 canales TX y 4 canales RX.
- El enlace es punto a punto y bidireccional.
- La selección de CRC y codificación debe coincidir en ambos extremos. Una
  negociación automática queda como mejora opcional.

---

## 5. Arquitectura de software

```text
capa_fisica/
├── README.md
├── implementacion.md
├── config/
│   ├── config.sim.json
│   └── config.beagle.json
├── capa_fisica/
│   ├── __init__.py
│   ├── config.py
│   ├── protocol/
│   │   ├── crc.py
│   │   ├── frame.py
│   │   ├── presentation.py
│   │   └── protocol_log.py
│   ├── phy/
│   │   ├── bitstream.py
│   │   ├── sync.py
│   │   └── transceiver.py
│   ├── link/
│   │   └── arq.py
│   ├── transport/
│   │   ├── base.py
│   │   ├── sim.py
│   │   ├── gpio_sysfs.py
│   │   └── gpio_adafruit.py
│   └── app/
│       ├── chat_service.py
│       └── web_server.py
├── web/
│   ├── index.html
│   ├── app.js
│   └── style.css
├── tools/
│   ├── terminal_chat.py
│   ├── gpio_probe.py
│   └── check_py35.py
├── tests/
│   ├── test_crc.py
│   ├── test_frame.py
│   ├── test_presentation.py
│   ├── test_bitstream.py
│   ├── test_link.py
│   └── test_e2e.py
├── docs/
│   ├── arquitectura.md
│   ├── trama.md
│   ├── medios_fisicos.md
│   ├── gpio.md
│   └── pruebas.md
└── scripts/
    ├── test.sh
    └── package.sh
```

La estructura puede crecer, pero se debe mantener la separación por capas.

### Flujo de datos

Transmisión:

```text
Texto de usuario
  -> Presentación (UTF-8 o Latin-1)
  -> bytes de 8 bits
  -> Enlace (SEQ, ACK/NACK, reintentos)
  -> Trama PHY (SYN, SFD, CONTROL, SEQ, DATA, CRC)
  -> Bitstream y carriles
  -> Transporte (simulación o GPIO)
  -> Medio físico
```

Recepción:

```text
Medio físico
  -> Transporte
  -> Muestreo de carriles y reloj
  -> Reconstrucción de bits
  -> Búsqueda de SYN/SFD
  -> Frame y verificación CRC
  -> Enlace (ACK/NACK, deduplicación)
  -> Presentación (decodificación incremental)
  -> Texto en la interfaz
```

---

## 6. Especificación de la trama PHY

### 6.1 Campos

| Campo | Bits | Valor / uso |
|---|---:|---|
| SYN | 16 | `0xAAAA`, patrón de sincronización |
| SFD | 4 | `0xD`, inicio real de la trama |
| CONTROL | 4 | tipo de frame |
| SEQ | 4 | campo `[FIELD]` de secuencia ARQ |
| DATA | 8 | exactamente un byte de usuario |
| CRC | 8 | CRC-8 seleccionable |
| PAD | variable | ceros hasta completar múltiplo de carriles |

Tamaño base: `16 + 4 + 4 + 4 + 8 + 8 = 44 bits`.

Con 3 carriles de datos:

```text
44 / 3 = 14.67 -> 15 ciclos de reloj
15 * 3 = 45 bits en línea
45 - 44 = 1 bit de relleno
```

El relleno se agrega al final, después del CRC, y el receptor lo descarta.

### 6.2 Orden de bits

- Cada campo se serializa **MSB primero**.
- Los campos se concatenan en este orden:
  `SYN -> SFD -> CONTROL -> SEQ -> DATA -> CRC -> PAD`.
- Los bits se agrupan en bloques de `K` bits, donde `K = N - 1`.
- `K` es la cantidad de carriles de datos; el reloj no transporta datos.
- El primer bit del bloque va al carril 0.
- Si el último bloque queda incompleto, se completa con ceros.

Ejemplo conceptual con `K = 3`:

```text
bits:    b0 b1 b2 | b3 b4 b5 | b6 b7 b8 | ...
carril:   0  1  2 |  0  1  2 |  0  1  2 | ...
```

### 6.3 Tipos de Control

| Valor | Nombre | Uso |
|---:|---|---|
| `0x0` | DATA | byte de usuario |
| `0x1` | ACK | confirma un SEQ recibido |
| `0x2` | NACK | rechaza un SEQ recibido |
| `0x3` | HELLO | presentación/versión del protocolo |
| `0x4` | CONFIG | configuración de codificación/CRC |
| `0x5` | STATUS | estado o contadores |
| `0x6`-`0xE` | Reservado | futuras extensiones |
| `0xF` | ABORT | abortar o reiniciar enlace |

Los frames ACK y NACK usan `DATA = 0x00` y `SEQ = secuencia confirmada o
rechazada`. Todos los frames conservan DATA de 8 bits y CRC de 8 bits.

### 6.4 Cobertura del CRC

El CRC se calcula sobre los 16 bits formados por:

```text
CONTROL (4 bits) + SEQ (4 bits) + DATA (8 bits)
```

En bytes:

```text
byte_0 = (CONTROL << 4) | SEQ
byte_1 = DATA
CRC    = CRC8(byte_0, byte_1)
```

SYN, SFD y PAD no participan del CRC.

### 6.5 Validación en recepción

1. Buscar el patrón SYN.
2. Verificar el SFD inmediatamente después.
3. Recolectar exactamente 44 bits.
4. Reconstruir CONTROL, SEQ, DATA y CRC.
5. Calcular el CRC con el algoritmo configurado.
6. Comparar con el CRC recibido.
7. Si coincide, entregar el frame al enlace.
8. Si no coincide, generar NACK y registrar el error.

---

## 7. Capa física y canales

### 7.1 Canales por sentido

Cada sentido de comunicación usa 4 canales:

| Canal | Tipo | Medio | Función |
|---:|---|---|---|
| 0 | Reloj | GPIO directo | Pulso de muestreo |
| 1 | Dato 0 | Luz | Bit del bloque |
| 2 | Dato 1 | Sonido | Bit del bloque |
| 3 | Dato 2 | Vibración | Bit del bloque |

Cada equipo implementa:

- 4 canales TX: reloj + luz + sonido + vibración.
- 4 canales RX: reloj + luz + sonido + vibración.

Total: 8 líneas GPIO por equipo, más actuadores y sensores.

### 7.2 Medios físicos

| Medio | Transmisión | Recepción | Interfaz a GPIO |
|---|---|---|---|
| Luz | LED o emisor IR | Fototransistor o fotodiodo | Comparador o pull-up |
| Sonido | Buzzer o emisor ultrasónico | Micrófono o receptor ultrasónico | Amplificador + comparador |
| Vibración | Motor de vibración o piezo | Piezo, acelerómetro o sensor de vibración | Acondicionamiento + comparador |

Cada receptor debe entregar niveles compatibles con GPIO: LOW (0) y HIGH (1).

### 7.3 Reloj y muestreo

- El reloj es una línea directa de GPIO.
- Protocolo source-synchronous: el transmisor coloca los datos y luego genera
  el pulso de reloj.
- El receptor muestrea los datos en el flanco de subida del reloj.
- Entre frames, el reloj permanece en bajo durante al menos 2 ciclos.
- El estado idle tiene reloj en bajo y datos en 0.
- El receptor espera el idle antes de armar la recepción de un frame nuevo.

### 7.4 Temporización inicial

Fórmula:

```text
ciclos_por_frame = ceil(44 / K)
duracion_frame_ms = ciclos_por_frame * 1000 / clock_hz
```

Con `N = 4`, `K = 3`, `clock_hz = 100`:

```text
ciclos_por_frame = 15
duracion_frame_ms = 150 ms
```

Un intercambio DATA + ACK tarda aproximadamente 300 ms más procesamiento, es
decir, del orden de 3 caracteres por segundo. Si la temporización del GPIO lo
permite, se puede subir la frecuencia y documentar el valor final.

### 7.5 Sincronización y resincronización

- El receptor mantiene un registro de desplazamiento con los bits recibidos.
- Busca el patrón `SYN + SFD`.
- Al encontrarlo, recolecta los 44 bits del frame.
- Si el CRC falla, descarta el frame y vuelve a buscar sincronización.
- El intervalo idle entre frames permite recuperar el alineamiento después de
  una pérdida de flanco o de una trama incompleta.
- Se deben registrar eventos de sincronización perdida y recuperada en el LOG.

---

## 8. CRC-8

Se implementa un motor CRC-8 genérico con presets. Todos son seleccionables.

| Algoritmo | Poly | Init | RefIn | RefOut | XorOut | Check (`"123456789"`) |
|---|---:|---:|---|---|---:|---:|
| SMBUS | `0x07` | `0x00` | No | No | `0x00` | `0xF4` |
| MAXIM-DOW | `0x31` | `0x00` | Sí | Sí | `0x00` | `0xA1` |
| SAE-J1850 | `0x1D` | `0xFF` | No | No | `0xFF` | `0x4B` |
| AUTOSAR | `0x2F` | `0xFF` | No | No | `0xFF` | `0xDF` |

Los valores check ya fueron verificados con un cálculo local.

Requisitos:

- Implementar los cuatro algoritmos.
- Exponer una función común de cálculo.
- Permitir seleccionar el algoritmo por configuración y por interfaz.
- Probar cada algoritmo con el vector check.
- Registrar en el LOG qué algoritmo se usó.
- Ambos extremos deben usar el mismo algoritmo.

---

## 9. Capa de presentación

### 9.1 UTF-8

- Implementar codificación y decodificación UTF-8 de rango completo.
- Soportar caracteres de 1, 2, 3 y 4 bytes.
- Un carácter puede ocupar varios frames PHY.
- La decodificación debe ser incremental: un carácter puede quedar incompleto
  entre dos frames.
- Mantener un buffer de bytes incompletos en recepción.
- No asumir que cada frame contiene un carácter completo.
- Registrar errores de decodificación sin romper el enlace.

### 9.2 Extended ASCII

- Usar Latin-1 (ISO-8859-1).
- Cada byte se corresponde con un carácter de 0x00 a 0xFF.
- La decodificación es directa, sin buffer multibyte.
- Si el usuario intenta enviar un carácter fuera de Latin-1, se debe informar el
  error y no enviar datos corruptos.

### 9.3 Delimitación de mensajes

Para las pruebas iniciales de terminal a terminal y para la sala de chat se usa
el byte `\n` (0x0A) como delimitador de mensaje a nivel de aplicación.

- La interfaz de chat enviará mensajes de una línea.
- El receptor acumula texto decodificado hasta recibir `\n`.
- Al recibir el delimitador, muestra el mensaje completo.
- Si más adelante se requiere texto multilínea, se agregará un control de
  longitud o un frame de inicio/fin de mensaje.

---

## 10. Capa de enlace de datos

Implementación inicial: stop-and-wait ARQ.

### 10.1 Transmisor

1. Tomar un byte de la cola de datos.
2. Asignar el SEQ actual.
3. Construir el frame DATA y calcular CRC.
4. Enviar el frame por PHY.
5. Esperar ACK o NACK con el mismo SEQ.
6. Si llega ACK, avanzar SEQ y continuar.
7. Si llega NACK, retransmitir el mismo frame.
8. Si vence el timeout, retransmitir el mismo frame.
9. Si se supera el máximo de reintentos, reportar error a la aplicación.

### 10.2 Receptor

1. Recibir un frame del PHY.
2. Verificar CRC.
3. Si el CRC es inválido, enviar NACK y no entregar datos.
4. Si el frame es válido y es DATA:
   - Si el SEQ es el esperado, entregar el byte, actualizar el esperado y
     enviar ACK.
   - Si el SEQ es el último entregado, es un duplicado: enviar ACK sin
     entregar el byte otra vez.
   - Si el SEQ no corresponde, enviar NACK.
5. Los frames ACK y NACK no se confirman con otro ACK.

### 10.3 Parámetros

- Secuencia de 4 bits, circular.
- Timeout inicial: 1000 ms.
- Reintentos máximos iniciales: 3.
- Ambos configurables.
- El timeout debe ser mayor que la duración de DATA + ACK + margen.
- Registrar timeouts, reintentos, duplicados y errores de CRC.

### 10.4 Prueba de error intencional

Secuencia esperada:

```text
Frame DATA -> CRC inválido -> NACK -> retransmisión -> CRC válido -> ACK
```

En simulación se invertirá un bit en el primer intento. En hardware se
interferirá físicamente el medio (por ejemplo, tapando el sensor de luz o
bloqueando la vibración) y se capturará el LOG.

---

## 11. Capa de aplicación: sala de chat web

- Servidor HTTP con Standard Library.
- Sin frameworks externos.
- Sin CDN ni recursos remotos.
- Interfaz HTML/CSS/JS servida localmente.
- Comunicación con el backend mediante polling HTTP.

Funciones mínimas:

- Ver mensajes recibidos.
- Escribir y enviar mensajes.
- Seleccionar codificación: UTF-8 o Latin-1.
- Seleccionar CRC: SMBUS, MAXIM-DOW, SAE-J1850 o AUTOSAR.
- Ver estado del enlace.
- Ver LOG del protocolo.
- Ver contadores de frames, errores, ACK, NACK y retransmisiones.
- En modo simulación, activar inyección de errores.

Endpoints previstos:

| Método | Ruta | Función |
|---|---|---|
| GET | `/` | Página de chat |
| GET | `/api/messages` | Mensajes nuevos |
| POST | `/api/send` | Enviar mensaje |
| GET | `/api/status` | Estado y contadores |
| GET | `/api/log` | Eventos del protocolo |
| POST | `/api/config` | Cambiar codificación/CRC |
| POST | `/api/fault` | Inyectar error en simulación |

Modelo de hilos:

- Hilo del servidor HTTP.
- Hilo receptor del enlace.
- Hilo transmisor o cola de transmisión.
- Colas `queue.Queue` para mensajes y eventos.
- El servidor web no debe acceder directamente a GPIO.

---

## 12. Configuración

Archivo JSON, sin dependencias externas.

Ejemplo para simulación:

```json
{
  "node_id": "A",
  "transport": "sim",
  "phy": {
    "data_lanes": 3,
    "clock_hz": 100,
    "sample_edge": "rising",
    "idle_gap_cycles": 2
  },
  "protocol": {
    "encoding": "utf-8",
    "crc": "smbus",
    "timeout_ms": 1000,
    "max_retries": 3
  },
  "tx": {
    "clock": {"pin": "SIM_CLK", "medium": "gpio-directo"},
    "lanes": [
      {"lane": 0, "medium": "luz", "pin": "SIM_TX0"},
      {"lane": 1, "medium": "sonido", "pin": "SIM_TX1"},
      {"lane": 2, "medium": "vibracion", "pin": "SIM_TX2"}
    ]
  },
  "rx": {
    "clock": {"pin": "SIM_CLK", "medium": "gpio-directo"},
    "lanes": [
      {"lane": 0, "medium": "luz", "pin": "SIM_RX0"},
      {"lane": 1, "medium": "sonido", "pin": "SIM_RX1"},
      {"lane": 2, "medium": "vibracion", "pin": "SIM_RX2"}
    ]
  },
  "web": {
    "host": "0.0.0.0",
    "port": 8080
  }
}
```

Los pines reales de la BeagleBone se definirán en la fase de hardware y se
documentarán en `docs/gpio.md`. No se deben usar pines ocupados por HDMI, eMMC u
otras funciones sin verificar el pin mux.

---

## 13. Plan por fases

### Fase 0 — Especificación y andamiaje

- [x] Leer restricciones del README.
- [x] Cerrar decisiones de arquitectura.
- [x] Crear `implementacion.md`.
- [x] Crear estructura del proyecto.
- [x] Crear configuración de simulación.
- [x] Crear `tools/check_py35.py`.
- [x] Crear esqueleto de tests.

**Criterio:** el paquete importa, la configuración valida y los tests se
descubren.

### Fase 1 — Núcleo del protocolo

- [x] CRC-8 con SMBUS, MAXIM-DOW, SAE-J1850 y AUTOSAR.
- [x] Frame con SYN, SFD, CONTROL, SEQ, DATA y CRC.
- [x] Serialización y deserialización de campos.
- [x] Presentación UTF-8 incremental.
- [x] Presentación Latin-1.
- [x] Tests unitarios con vectores y casos de borde.

**Criterio:** todos los tests de CRC, trama y presentación pasan.

### Fase 2 — Capa física y simulador

- [x] Serialización a carriles.
- [x] Relleno por carril.
- [ ] Máquina de estados SYN/SFD.
- [ ] Resincronización por idle.
- [ ] Simulador de bus con reloj y latencia.
- [ ] Inyección de bit invertido y flanco perdido.
- [ ] Matriz de pruebas para N = 3..8.

**Criterio:** loopback correcto para todos los anchos de canal.

### Fase 3 — Enlace de datos

- [ ] ARQ stop-and-wait.
- [ ] ACK/NACK.
- [ ] Timeout y reintentos.
- [ ] Deduplicación.
- [ ] Secuencia circular.
- [ ] LOG estructurado.
- [ ] Prueba CRC -> NACK -> retransmisión -> ACK.

**Criterio:** entrega única aun con ACK perdido o duplicado.

### Fase 4 — Sala de chat y Web Server

- [ ] Servicio de chat.
- [ ] Servidor HTTP.
- [ ] Interfaz web.
- [ ] Selector de codificación y CRC.
- [ ] Panel de estado y LOG.
- [ ] Pruebas de integración HTTP.

**Criterio:** dos nodos simulados intercambian mensajes UTF-8 y Latin-1.

### Fase 5 — Validación local integral

- [ ] Prueba end-to-end con dos nodos.
- [ ] Error intencional en simulación.
- [ ] Demo de terminal.
- [ ] Script de empaquetado ZIP.
- [ ] Evidencia de pruebas.

**Criterio:** checklist local completo.

### Fase 6 — Migración a BeagleBone

- [ ] Backend GPIO sysfs.
- [ ] Backend opcional Adafruit_BBIO.
- [ ] `gpio_probe.py`.
- [ ] Mapeo de pines y pin mux.
- [ ] Medición de período y jitter.
- [ ] Decisión sobre PRU con evidencia.
- [ ] Migración offline.

**Criterio:** tests y backend funcionan en la BeagleBone.

### Fase 7 — Medios físicos y dos equipos

- [ ] Circuitos de luz.
- [ ] Circuitos de sonido.
- [ ] Circuitos de vibración.
- [ ] Prueba canal por canal.
- [ ] Full-duplex.
- [ ] Interferencia física real.
- [ ] Recuperación CRC -> NACK -> retransmisión.

**Criterio:** dos equipos funcionales con chat bidireccional.

### Fase 8 — Documentación y entrega

- [ ] Arquitectura.
- [ ] Trama.
- [ ] Medios y diagramas de conexión.
- [ ] GPIO y pin mux.
- [ ] Temporización.
- [ ] Resultados de pruebas.
- [ ] Limitaciones.
- [ ] ZIP para GES.

**Criterio:** entrega completa y defendible.

---

## 14. Estrategia de pruebas

### 14.1 Pruebas unitarias locales

```bash
python3 -m unittest discover -s tests -v
```

Cubren:

- CRC con vector `"123456789"`.
- Trama: campos, orden de bits, cobertura CRC, ACK/NACK.
- Presentación: ASCII, acentos, emojis, chino, Latin-1, bytes inválidos.
- Bitstream: carriles, relleno, reconstrucción, búsqueda de SYN/SFD.
- Enlace: ACK, NACK, timeout, reintentos, duplicados.
- Integración: dos nodos simulados.

### 14.2 Compatibilidad Python 3.5

```bash
python3 tools/check_py35.py
```

El script debe:

- Parsear el código con gramática de Python 3.5.
- Detectar f-strings.
- Detectar dataclasses.
- Detectar sintaxis moderna.
- Listar importaciones incompatibles conocidas.

Si hay red disponible:

```bash
docker run --rm -v "$PWD:/app" -w /app python:3.5 \
  python -m unittest discover -s tests -v
```

Si no hay red, usar el chequeo estático y evitar APIs modernas.

### 14.3 Pruebas de terminal a terminal

Las primeras pruebas no usan Web ni hardware. Validan el recorrido:

```text
texto -> bytes -> frames -> carriles -> frames -> bytes -> texto
```

Pipeline:

```bash
printf 'Hola, mundo\n' | \
  python3 tools/terminal_chat.py tx --encoding utf-8 --crc smbus | \
  python3 tools/terminal_chat.py rx --encoding utf-8 --crc smbus
```

Dos terminales usando un FIFO:

```bash
mkfifo /tmp/phy_link
python3 tools/terminal_chat.py rx --encoding utf-8 --crc smbus < /tmp/phy_link
python3 tools/terminal_chat.py tx --encoding utf-8 --crc smbus > /tmp/phy_link
```

Casos iniciales:

- ASCII simple.
- Acentos y eñe.
- Emoji de 4 bytes.
- Caracteres chinos de 3 bytes.
- Latin-1.
- Frame corrupto: debe fallar CRC y reportar error.
- Verificación de relleno con 3 carriles.

### 14.4 Pruebas en BeagleBone

```bash
python3 -m unittest discover -s tests -v
python3 tools/gpio_probe.py --config config/config.beagle.json
python3 tools/terminal_chat.py --help
```

Luego:

```bash
python3 run.py --config config/config.beagle.json
```

### 14.5 Prueba de error físico

1. Iniciar chat.
2. Enviar un mensaje.
3. Interferir el medio de un carril.
4. Verificar en el LOG: CRC_FAIL, NACK, RETRY, ACK.
5. Confirmar que el mensaje se entrega una sola vez.

### 14.6 Resultados de la sesión actual

Implementado y verificado:

- Paquete `capa_fisica` con módulos de CRC, trama, presentación y bitstream.
- CRC-8 con los cuatro algoritmos y vectores check.
- Trama de 44 bits con SYN, SFD, CONTROL, SEQ, DATA y CRC.
- Relleno de carriles para `K = 2..7`.
- Búsqueda básica de SYN/SFD en un flujo de bits.
- UTF-8 incremental de 1 a 4 bytes y Latin-1.
- Herramienta `tools/terminal_chat.py` con modos `tx`, `rx` y `loopback`.
- Inyección de error para demostrar CRC inválido.
- 46 tests unitarios y de integración.

Resultados:

```text
python3 -m unittest discover -s tests -v
Ran 46 tests in 0.695s
OK

python3 tools/check_py35.py
OK: 15 files are compatible with Python 3.5 grammar
```

También se ejecutaron los 46 tests dentro de un contenedor con Python 3.5:

```text
podman run --rm -v "$PWD:/app:Z" -w /app python:3.5 \
  python -m unittest discover -s tests -v
Ran 46 tests in 0.453s
OK
```

Prueba terminal a terminal exitosa:

```text
printf 'Hola, señor 😀\n' | \
  python3 tools/terminal_chat.py tx --encoding utf-8 --crc autosar --lanes 3 | \
  python3 tools/terminal_chat.py rx --encoding utf-8 --crc autosar --lanes 3
```

La salida reconstruida fue `Hola, señor 😀`.

Prueba de error:

```text
printf 'abc\n' | \
  python3 tools/terminal_chat.py tx --crc smbus --lanes 3 --corrupt-frame 1 --quiet | \
  python3 tools/terminal_chat.py rx --crc smbus --lanes 3 --quiet
```

El receptor reportó `CRC mismatch` y descartó el byte corrupto. La salida fue
`ac` (el byte `b` se perdió). Esta prueba es la base para el futuro NACK y la
retransmisión de la Fase 3.

### 14.7 Makefile general interactivo

El archivo `Makefile` permite levantar emisor y receptor en dos terminales y
escribir mensajes desde el teclado.

Terminal 1, receptor:

```bash
make receptor
```

Terminal 2, emisor:

```bash
make emisor
```

En la terminal del emisor se escribe el mensaje y se presiona Enter. El emisor
envía el texto byte por byte; el receptor lo muestra carácter por carácter.

Al recibir el delimitador `\n`, el receptor reconstruye el mensaje completo y
lo informa explícitamente:

```text
[RECEPTOR] TEXTO recibido='h'
[RECEPTOR] TEXTO recibido='o'
[RECEPTOR] TEXTO recibido='l'
[RECEPTOR] TEXTO recibido='a'
[RECEPTOR] TEXTO recibido='\n'
[RECEPTOR] MENSAJE COMPLETO (4 caracteres): 'hola'
```

Si la conexión termina antes del delimitador, se informa como mensaje
incompleto.

Ambas terminales muestran la traza del protocolo:

- SYN y SFD.
- CONTROL, SEQ y DATA.
- Cálculo y verificación del CRC.
- ACK y NACK.
- Timeouts.
- Reintentos.
- Duplicados.
- Errores de conexión.

Ejemplo de error intencional para ver CRC -> NACK -> retransmisión:

```bash
make emisor CORRUPT_FRAME=1
```

El receptor detecta el CRC inválido, responde NACK, el emisor retransmite y el
mensaje se entrega una sola vez.

Variables del `Makefile`:

- `PYTHON`
- `ENCODING`
- `CRC`
- `LANES`
- `HOST`
- `BIND`
- `PORT`
- `TIMEOUT`
- `RETRIES`
- `QUIET`
- `CORRUPT_FRAME`
- `CORRUPT_BIT`

Para dos computadoras:

```bash
make receptor BIND=0.0.0.0
```

```bash
make emisor HOST=192.168.1.50
```

El socket TCP se usa solamente como transporte de prueba local. La Capa Física
real seguirá siendo la de medios no convencionales y GPIO.

### 14.8 Makefiles de archivo y FIFO

También se incluyen dos Makefiles auxiliares para pruebas no interactivas:

- `Makefile.emisor`
- `Makefile.receptor`

Ayuda:

```bash
make -f Makefile.emisor ayuda
make -f Makefile.receptor ayuda
```

Enviar un mensaje a archivo:

```bash
make -f Makefile.emisor archivo \
  MENSAJE="Hola, señor" \
  SALIDA=frames.jsonl
```

Recibir desde archivo:

```bash
make -f Makefile.receptor recibir ENTRADA=frames.jsonl
```

Ver las tramas generadas:

```bash
make -f Makefile.emisor tramas MENSAJE="Hola"
```

Probar un error de CRC:

```bash
make -f Makefile.emisor archivo \
  MENSAJE="abc" \
  SALIDA=frames_bad.jsonl \
  CORRUPT_FRAME=1

make -f Makefile.receptor recibir ENTRADA=frames_bad.jsonl
```

Dos terminales usando FIFO:

```bash
make -f Makefile.receptor escuchar FIFO=/tmp/phy_link
```

```bash
make -f Makefile.emisor interactivo > /tmp/phy_link
```

Variables disponibles: `PYTHON`, `ENCODING`, `CRC`, `LANES`, `SEQ_START`,
`QUIET`, `MENSAJE`, `SALIDA`, `ENTRADA`, `FIFO` y `CORRUPT_FRAME`.

---

## 15. Migración a BeagleBone

Checklist:

- [ ] Confirmar arquitectura y versión de Python.
- [ ] Verificar repositorios disponibles.
- [ ] Crear venv en la BeagleBone.
- [ ] No copiar el venv del escritorio.
- [ ] Preparar paquetes offline si hacen falta.
- [ ] Verificar Adafruit_BBIO 1.1.0 si se usa.
- [ ] Verificar permisos de GPIO.
- [ ] Configurar pin mux.
- [ ] Probar entradas y salidas una por una.
- [ ] Medir frecuencia y jitter reales.
- [ ] Documentar incompatibilidades.

Comandos de diagnóstico:

```bash
uname -a
uname -m
dpkg --print-architecture
python3 --version
pip3 --version
ls -l /sys/class/gpio
ls -l /dev/gpiochip*
ls -l /sys/class/remoteproc
```

El backend GPIO debe quedar detrás de una interfaz, de modo que el simulador y
la BeagleBone sean intercambiables sin tocar las capas superiores.

---

## 16. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Timing de GPIO en Python es lento o irregular | Reloj lento inicial, muestreo source-synchronous, medición real, PRU solo si se justifica |
| Diferencia Python 3.14 local vs 3.5.3 objetivo | `check_py35.py`, evitar APIs modernas, Docker 3.5 si hay red |
| Medios analógicos inestables | Circuitos de acondicionamiento, comparadores, pruebas por canal |
| Interferencia entre canales | Medios distintos, separación física, blindaje y pruebas |
| Reloj directo requiere referencia de tierra | Documentar conexión, usar interfaz adecuada y probar con ambos equipos |
| Pines ocupados o pin mux incorrecto | Inventario de pines y `gpio_probe.py` antes de integrar |
| Pérdida de sincronización | Idle gap, SYN/SFD, resincronización y LOG |
| ACK/NACK lentos por stop-and-wait | Aceptable para chat; subir reloj si el hardware lo permite |
| Selección distinta de CRC/codificación | Configuración explícita y validación al iniciar |
| Repositorio git mezclado con el home | Mantener el proyecto aislado y agregar `.gitignore` |

---

## 17. Bitácora de sesiones

| Fecha | Avance | Pendiente |
|---|---|---|
| 2026-11-13 | Lectura de restricciones, decisiones de arquitectura, plan por fases y creación de `implementacion.md` | Estructura base, núcleo del protocolo y prueba terminal a terminal |
| 2026-11-13 | Implementados CRC-8, trama PHY, presentación UTF-8/Latin-1, bitstream por carriles, demo `terminal_chat.py` y 46 tests. Verificado en Python 3.14 local y Python 3.5 en contenedor. | Simulador de bus, sincronización por idle, transceiver, ARQ/ACK/NACK y LOG |
| 2026-11-13 | Agregados `Makefile.emisor` y `Makefile.receptor` para pruebas terminal a terminal, envío a archivo, FIFO y demostración de CRC inválido. | Integrar los Makefiles al flujo con simulador y enlace ARQ |
| 2026-11-13 | Agregado `Makefile` general con emisor/receptor interactivos por TCP, traza SYN/SFD/DATA/CRC, ACK/NACK, timeout, reintentos, deduplicación y corrupción inducida. Verificado `hola` carácter por carácter y recuperación CRC -> NACK -> retransmisión. | Reemplazar el transporte TCP de prueba por el simulador/GPIO de la Capa Física |
| 2026-11-13 | El receptor acumula el texto decodificado y reconstruye el mensaje completo al recibir `\n`, incluyendo UTF-8 multibyte y aviso de mensaje incompleto. Verificado con `hola mundo` y `Hola señor 😀`. | Extraer el acumulador de mensajes a la capa de aplicación reutilizable |

---

## 18. Próximo paso

El chat interactivo por TCP ya demuestra recuperación CRC -> NACK ->
retransmisión. La Fase 2 y la Fase 3 continúan así:

1. Extraer el ARQ stop-and-wait del modo interactivo a un módulo reutilizable
   de Capa de Enlace.
2. Implementar el simulador de bus con reloj, latencia e idle.
3. Implementar la máquina de estados de recepción SYN/SFD con resincronización.
4. Implementar el transceiver TX/RX sobre el simulador.
5. Conectar el ARQ al simulador en lugar del socket TCP.
6. Agregar LOG estructurado persistente.
7. Repetir la prueba terminal a terminal usando el simulador y demostrar
   CRC -> NACK -> retransmisión -> ACK.

La meta verificable inmediata es que el mismo comportamiento observado por TCP
funcione sobre el simulador de la Capa Física y luego sobre GPIO.
