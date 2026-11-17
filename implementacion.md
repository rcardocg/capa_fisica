# Implementación — Protocolo de Capa Física No Convencional

Documento de contexto y planificación por fases para el proyecto de Capa Física.
Su objetivo es que cualquier integrante pueda retomar el trabajo en otra sesión o
en otro equipo sin perder las decisiones tomadas.

- **Proyecto:** protocolo de comunicación de Capa Física no convencional.
- **Plataforma objetivo:** BeagleBone Black Rev C.
- **Desarrollo actual:** PC para gateway y pruebas; BeagleBone para el nodo físico.
- **Última actualización:** 2026-11-16.

---

## Índice

- [Estado actual](#estado-actual)
- [1. Cómo usar este documento](#1-cómo-usar-este-documento)
- [2. Contexto del proyecto](#2-contexto-del-proyecto)
- [3. Restricciones reales del hardware](#3-restricciones-reales-del-hardware)
- [4. Decisiones cerradas](#4-decisiones-cerradas)
- [5. Arquitectura de software](#5-arquitectura-de-software)
- [6. Especificación de la trama PHY](#6-especificación-de-la-trama-phy)
- [7. Capa física y canales](#7-capa-física-y-canales)
- [8. CRC-8](#8-crc-8)
- [9. Capa de presentación](#9-capa-de-presentación)
- [10. Capa de enlace de datos](#10-capa-de-enlace-de-datos)
- [11. Capa de aplicación: sala de chat web](#11-capa-de-aplicación-sala-de-chat-web)
- [12. Configuración](#12-configuración)
- [13. Plan por fases](#13-plan-por-fases)
- [14. Estrategia de pruebas](#14-estrategia-de-pruebas)
- [15. Implementación en C](#15-implementación-en-c)
- [16. Migración a BeagleBone](#16-migración-a-beaglebone)
- [17. Riesgos y mitigaciones](#17-riesgos-y-mitigaciones)
- [18. Bitácora de sesiones](#18-bitácora-de-sesiones)
- [19. Próximo paso](#19-próximo-paso)
- [Isaac — Cómo seguir con la parte física](#isaac)

---

<a name="estado-actual"></a>
## Estado actual (2026-11-16)

La implementación activa es C99 y cubre las fases 0 a 5 del plan de
[`fases.md`](fases.md). La parte física real es la fase 6 y queda a cargo de
Isaac; el detalle está en la sección [Isaac](#isaac).

| Fase | Estado |
|---|---|
| Fase 0 — Contratos y configuración | Completada |
| Fase 1 — Recepción genérica con canal falso | Completada |
| Fase 2 — Canales GPIO y nodo con enlace falso | Completada |
| Fase 3 — Identidad y relay | Completada |
| Fase 4 — Gateway de la PC y HTML | Completada |
| Fase 5 — Prueba integral de software | Completada |
| Fase 6 — Conexión física y prueba real | Pendiente (Isaac) |
| Fase 7 — ACK/NACK y retransmisión | Pendiente |

### Qué está funcionando

- Dos nodos C `phy_node`, uno para A y otro para B.
- Enlace de luz con reloj directo por GPIO, dos pulsos por bit y recepción por
  ADC.
- Trama PHY de 44 bits: SYN, SFD, CONTROL, SEQ, DATA y CRC.
- Identidad de usuario con frames `HELLO` antes de cada mensaje `DATA`.
- HTML con nombre de usuario, modos `Tramas PHY` y `Bits sin trama`, envío de
  mensajes y polling del inbox.
- Gateway HTTP en la PC que habla con la API TCP del nodo A.
- Enlace falso bidireccional y simulador A/B para pruebas sin hardware.
- Prueba integral de la fase 5 con acentos Latin-1, usuarios distintos, CRC
  corrupto, trama incompleta y medición de tiempo.

Alcance actual: enlace por luz. Sonido y vibración quedan fuera de esta
primera versión y no forman parte de la fase 6.

### Mapa de archivos principal

| Archivo | Función |
|---|---|
| `c/include/phy_protocol.h` | Trama, controles, CRC y presentación |
| `c/src/frame.c` | Construcción y validación de la trama de 44 bits |
| `c/src/crc8.c` | CRC-8 SMBUS, MAXIM-DOW, SAE-J1850 y AUTOSAR |
| `c/src/presentation.c` | UTF-8 y Latin-1 |
| `c/src/phy_gpio.c` | Acceso GPIO por sysfs |
| `c/src/phy_analog.c` | Lectura del ADC |
| `c/src/phy_gpio_tx_channel.c` | TX de luz con dos pulsos por bit |
| `c/src/phy_light_rx_channel.c` | RX de luz con reloj externo y ADC |
| `c/src/phy_node.c` | Hilos RX/TX, cola, eventos y API TCP |
| `c/tools/phy_node.c` | Binario del nodo para la BeagleBone |
| `c/src/phy_fake_link.c` | Enlace falso bidireccional |
| `c/tools/phy_sim.c` | Simulador A/B con APIs TCP |
| `servers/local/local_server.py` | Gateway HTTP de la PC |
| `servers/local/index.html` | Interfaz HTML del chat |
| `scripts/test_fase5.py` | Prueba integral de la fase 5 |
| `config/node_a.json` | Pines de A (referencia) |
| `config/node_b.json` | Pines de B (referencia) |

### Comandos actuales

```bash
make                         # compila phy_chat y phy_node
make test                    # pruebas C: protocolo, RX y nodo
make test-nodo               # A/B con enlace falso y API TCP
make prueba-integral         # prueba integral de la fase 5
make sim                     # simulador A/B para inspección manual
make nodo NODE_ROLE=A NODE_PORT=7000
```

La API del nodo y el gateway local usan el puerto `7000`.

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
- Implementación del nodo: C99, sin dependencias externas.
- APIs permitidas: POSIX disponibles en Debian Stretch.
- GPIO por sysfs como primera alternativa.
- ADC por las entradas IIO del kernel.
- Hilos de recepción, transmisión y API con `pthread`.
- No asumir versiones modernas de compilador ni de librerías.
- Verificar pines, pin mux y disponibilidad antes de conectar.
- Evaluar PRU-ICSS del AM3358 solo si la medición real lo justifica.
- Mantener separada la lógica del protocolo del acceso a GPIO/ADC.
- La migración debe poder ser offline.
- El gateway local de la PC usa Python 3 y se mantiene compatible con 3.5.3.
- No usar f-strings ni APIs modernas en el gateway.
- Separar dependencias de desarrollo y hardware.

Antes de la migración verificar en la BeagleBone:

```bash
uname -m
dpkg --print-architecture
gcc --version
make --version
ls -l /sys/class/gpio
ls -l /sys/bus/iio/devices
ls -l /sys/class/remoteproc
```

La prioridad es que el nodo funcione en el hardware existente, sin modernizar
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

Estado implementado en las fases 0 a 5: enlace por luz con 1 reloj y 1 dato
por sentido, `clock_hz=20`, identidad por `HELLO`, relay en A y gateway HTML.
El enlace de luz todavía no tiene ACK/NACK; TCP cubre PC ↔ A y la fase 7
agregará confiabilidad al enlace A ↔ B. Sonido, vibración y los tres carriles
quedan fuera de la primera versión.

---

## 5. Arquitectura de software

```text
capa_fisica/
├── README.md
├── implementacion.md
├── fases.md
├── Makefile
├── config/
│   ├── node_a.json
│   ├── node_b.json
│   └── config.sim.json          # esquema histórico
├── c/
│   ├── Makefile
│   ├── include/
│   ├── src/
│   ├── tools/
│   └── tests/
├── servers/
│   ├── local/
│   │   ├── local_server.py
│   │   ├── index.html
│   │   └── translator.js
│   └── beagle/
│       └── beagle_server.py     # diagnóstico de BITS
├── docs/
│   └── servidores.md
└── scripts/
    └── test_fase5.py
```

La estructura puede crecer, pero se debe mantener la separación por capas.

### Flujo de datos

Transmisión actual:

```text
Texto + usuario
  -> Presentación Latin-1
  -> frames HELLO (usuario) y DATA (texto)
  -> Trama PHY (SYN, SFD, CONTROL, SEQ, DATA, CRC)
  -> Canal TX de luz (dos pulsos por bit)
  -> LED y reloj GPIO
```

Recepción actual:

```text
Luz y reloj externo
  -> Muestreo ADC (s1, s2) por pulso
  -> Decodificación del par
  -> 44 bits por frame
  -> Validación SYN/SFD/CRC
  -> HELLO actualiza el usuario / DATA acumula el texto
  -> Evento MESSAGE
  -> Gateway local y HTML
```

ACK/NACK y retransmisión sobre luz no están implementados todavía; quedan
para la fase 7. TCP cubre la confiabilidad entre la PC y el nodo A.

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

Funciones implementadas:

- Pedir el nombre de usuario al iniciar.
- Ver mensajes recibidos con su usuario y origen.
- Escribir y enviar mensajes con usuario.
- Modo `Tramas PHY`: envía `HELLO` + `DATA` al nodo A.
- Modo `Bits sin trama`: envía bits crudos con `BITS`.
- Polling del inbox cada 800 ms.

Pendiente para fases posteriores: selección de codificación/CRC desde el HTML,
panel de estado y LOG, e inyección de fallas desde la interfaz. ACK/NACK y
retransmisión son fase 7.

Endpoints actuales:

| Método | Ruta | Función |
|---|---|---|
| GET | `/` | Página de chat |
| GET | `/translator.js` | Conversión español/ASCII/binario |
| GET | `/api/ping` | `PING` al nodo A |
| GET | `/api/inbox?since=N` | Mensajes recibidos (`MESSAGE`) |
| POST | `/api/message` | Envía `{"user":"ana","text":"hola"}` con `SEND` |
| POST | `/api/bits` | Envía bits crudos con `BITS` |
| POST/GET | `/api/frames` | Diagnóstico legado del emisor C |

Modelo de hilos:

- Servidor HTTP con `ThreadingHTTPServer`.
- Conexión TCP persistente y serializada hacia la API del nodo A.
- Nodo A: hilo de recepción, hilo de transmisión, cola y API TCP.
- El servidor web no accede directamente a GPIO.

---

## 12. Configuración

La configuración de referencia está en `config/node_a.json` y
`config/node_b.json`. El binario actual `phy_node` recibe pines, puerto y
umbrales por línea de comandos; los JSON documentan los valores esperados.

Ejemplo de A:

```json
{
  "node": "A",
  "role": "gateway",
  "reachable_from_pc": true,
  "tx": {
    "clock": { "gpio": 44, "pin": "P8_12", "direction": "out" },
    "data": { "gpio": 60, "pin": "P9_12", "medium": "luz" }
  },
  "rx": {
    "clock": { "gpio": 45, "pin": "P8_11", "direction": "in" },
    "data": { "ain": 0, "pin": "P9_39", "medium": "luz" }
  },
  "clock_hz": 20.0,
  "pulses_per_bit": 2,
  "encoding": "latin-1",
  "crc": "smbus",
  "api": { "host": "0.0.0.0", "port": 7000 }
}
```

En B se intercambian los GPIO de clock: TX usa gpio45/P8_11 y RX usa
gpio44/P8_12. El resto de los pines coincide.

`config/config.sim.json` conserva el esquema histórico de simulación de tres
carriles. El simulador actual `phy_sim` toma `--a-port`, `--b-port` y
`--clock-hz` por línea de comandos.

No se deben usar pines ocupados por HDMI, eMMC u otras funciones sin verificar
el pin mux.

---

## 13. Plan por fases

> Nota: este plan de 8 fases es el plan histórico inicial. El seguimiento
> activo de las fases 0 a 7 está en [`fases.md`](fases.md) y en la sección
> [Estado actual](#estado-actual). La fase 6 se detalla en [Isaac](#isaac).

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

### 14.1 Pruebas unitarias C

```bash
make test
```

Incluye:

- `test_protocol`: CRC, trama de 44 bits, SYN/SFD, presentación UTF-8 y
  Latin-1, relleno por carriles y vectores conocidos.
- `test_rx`: recepción con canal falso, frame válido, CRC alterado, SYN
  inválido, mensaje ASCII, mensaje Latin-1 y timeouts.
- `test_node`: dos nodos A/B con enlace falso, API TCP, `SEND`, `BITS`,
  identidad por `HELLO`, relay y recepción concurrente con transmisión.

### 14.2 Pruebas con enlace falso

```bash
make test-nodo
```

Verifica:

- `SEND` de A llega como mensaje a B.
- `SEND` de B llega como mensaje a A.
- `BITS` sigue enviando bits crudos.
- La recepción no se bloquea mientras el nodo transmite.

### 14.3 Prueba integral de la fase 5

```bash
make prueba-integral
```

El script `scripts/test_fase5.py` levanta `phy_sim` con A y B unidos por el
enlace falso, arranca el gateway HTTP y prueba:

- HTML -> A -> B.
- Terminal de B -> A -> HTML.
- Acentos Latin-1 en ambos sentidos.
- Usuarios distintos.
- Frame con CRC corrupto registrado sin romper el nodo.
- Trama incompleta sin generar mensaje y con recuperación posterior.
- Medición de tiempo con dos pulsos por bit y `clock_hz`.

### 14.4 Pruebas de hardware (fase 6)

En la BeagleBone:

```bash
make probe-luz SUDO=sudo PROBE_ARGS="--led --blink 0.5"
make rx-luz SUDO=sudo PROBE_ARGS="--pattern 1110 --clock-hz 2"
sudo ./c/phy_node --role A --port 7000 --trace
sudo ./c/phy_node --role B --port 7000 --trace
```

El procedimiento completo de cableado, calibración y criterios de aceptación
está en la sección [Isaac](#isaac).

### 14.5 Prueba de error intencional

La prueba integral inyecta un frame con CRC inválido y una trama incompleta
desde la API de B:

- El CRC inválido debe aparecer como `RX ... status=CRC invalido`.
- El nodo debe seguir respondiendo `PING`.
- Una trama incompleta no debe generar `MESSAGE`.
- Después de ambos errores, un mensaje válido debe seguir llegando.

### 14.6 Resultados actuales

```text
$ make test
OK: pruebas de CRC, trama y presentacion
OK: recepcion de tramas y mensajes
OK: nodo A/B con enlace falso y API TCP

$ make prueba-integral
OK: A -> B: mensaje en la terminal de B
OK: B -> A -> HTML: mensaje en el inbox
OK: A -> B conserva acentos Latin-1
OK: B -> A -> HTML conserva acentos Latin-1
OK: el tiempo medido respeta los dos pulsos por bit
OK: el nodo A registra el CRC invalido
OK: la trama incompleta no genera un mensaje
OK: prueba integral de la fase 5
```

---

## 15. Implementación en C

La implementación activa del nodo está en C99. Usa `pthread`, GPIO por sysfs,
ADC por IIO y no depende de bibliotecas externas. El camino del chat es:

```text
HTML -> servidor local -> API TCP del nodo A -> enlace de luz -> nodo B
nodo B -> enlace de luz -> nodo A -> POLL/inbox -> servidor local -> HTML
```

La variante `phy_chat` con transporte TCP queda como herramienta de
diagnóstico del protocolo.

### 15.1 Estructura

```text
c/
├── Makefile
├── include/
│   ├── phy_protocol.h
│   ├── phy_channel.h
│   ├── phy_rx_channel.h
│   ├── phy_rx.h
│   ├── phy_node.h
│   ├── phy_fake_link.h
│   ├── phy_gpio.h
│   ├── phy_analog.h
│   └── phy_net.h
├── src/
│   ├── crc8.c
│   ├── frame.c
│   ├── presentation.c
│   ├── phy_gpio.c
│   ├── phy_analog.c
│   ├── phy_gpio_tx_channel.c
│   ├── phy_light_rx_channel.c
│   ├── phy_rx.c
│   ├── phy_rx_channel.c
│   ├── phy_fake_rx_channel.c
│   ├── phy_fake_link.c
│   ├── phy_node.c
│   └── net.c
├── tools/
│   ├── phy_node.c
│   ├── phy_sim.c
│   └── phy_light_tx.c
└── tests/
    ├── test_protocol.c
    ├── test_rx.c
    └── test_node.c
```

- `phy_node.c`: nodo con hilo RX, cola TX, log de eventos, identidad y API TCP.
- `phy_gpio_tx_channel.c`: transmisión por LED con dos pulsos por bit.
- `phy_light_rx_channel.c`: recepción por ADC usando el reloj externo.
- `phy_fake_link.c`: enlace falso con temporización opcional de dos pulsos.
- `phy_sim.c`: levantamiento de A y B simulados con APIs TCP.
- `phy_rx.c`: sincronización, 44 bits, CRC y reconstrucción del mensaje.
- `chat.c`: emisor/receptor TCP de diagnóstico con ARQ stop-and-wait.

### 15.2 Compilación y uso

```bash
make                         # compila phy_chat y phy_node
make test                    # pruebas C: protocolo, RX y nodo
make test-nodo               # A/B con enlace falso y API TCP
make prueba-integral         # prueba integral de la fase 5
make sim                     # simulador A/B sin hardware
make nodo NODE_ROLE=A NODE_PORT=7000
```

Nodo real en la Beagle:

```bash
sudo ./c/phy_node --role A --port 7000 --trace
sudo ./c/phy_node --role B --port 7000 --trace
```

Gateway de la PC:

```bash
python3 servers/local/local_server.py
```

Variables y opciones principales: `NODE_ROLE`, `NODE_PORT`, `NODE_BIND`,
`ENCODING`, `CRC`, `--clock-hz`, `--on-level`, `--off-level`,
`--adc-samples` y `--adc-interval-ms`.

La variante `make emisor`/`make receptor` sigue disponible para probar
`phy_chat` por TCP sin hardware.

### 15.3 Paridad con la versión Python

La versión C reproduce el mismo comportamiento:

- Misma trama PHY de 44 bits y mismo relleno por carriles.
- Mismos CRC y mismos valores de verificación para `"123456789"`.
- Mismo control `DATA/ACK/NACK/...` y secuencia circular `0..15`.
- Stop-and-wait con timeout de 1 s, 3 reintentos y deduplicación.
- Trazas de SYN/SFD/CONTROL/SEQ/DATA/CRC, ACK/NACK, timeout y reintentos.
- Reconstrucción del mensaje completo al recibir `\n`.
- UTF-8 multibyte y Latin-1 con conversión para la terminal.
- Corrupción inducida con `CORRUPT_FRAME` y recuperación CRC -> NACK ->
  retransmisión -> ACK.

La diferencia está solo en el transporte TCP de prueba: Python usa registros
JSON y C usa líneas `F <carriles> <bits>`. Por eso, por ahora, un emisor C debe
hablar con un receptor C. La trama PHY transmitida es la misma.

### 15.4 Verificación histórica de `phy_chat`

```text
$ make test
OK: pruebas de CRC, trama y presentacion

$ make receptor
[RECEPTOR] MENSAJE COMPLETO (10 caracteres): 'hola mundo'

$ make emisor CORRUPT_FRAME=1
[EMISOR] TX ... CRC=0x1E CALC=0x1F CORRUPTO
[EMISOR] NACK recibido para SEQ=1
[EMISOR] REINTENTO 1/3 para SEQ=1
[EMISOR] ACK SEQ=1 OK
[RECEPTOR] MENSAJE COMPLETO (12 caracteres): 'Hola señor 😀'
```

También se verificó Latin-1 con `Mañana`: el emisor convierte UTF-8 a Latin-1,
transmite `0xF1` y el receptor lo vuelve a mostrar como UTF-8.

### 15.5 Integración con los Makefiles

Objetivos actuales del `Makefile` raíz:

| Objetivo | Función |
|---|---|
| `make` | Compila `phy_chat` y `phy_node` |
| `make test` | Pruebas C de protocolo, RX y nodo |
| `make test-nodo` | A/B con enlace falso |
| `make prueba-integral` | Prueba integral de la fase 5 |
| `make sim` | Simulador A/B con APIs TCP |
| `make nodo` | Inicia `phy_node` con `NODE_ROLE` y `NODE_PORT` |
| `make emisor` / `make receptor` | Diagnóstico TCP con `phy_chat` |

### 15.6 Verificación de la fase 5

```text
$ make prueba-integral
OK: nodo A simulado disponible
OK: nodo B simulado disponible
OK: gateway de la PC conectado al nodo A
OK: HTML envia el mensaje a A
OK: A -> B: mensaje en la terminal de B
OK: terminal de B envia el mensaje a A
OK: B -> A -> HTML: mensaje en el inbox
OK: HTML envia texto Latin-1
OK: A -> B conserva acentos Latin-1
OK: B -> A -> HTML conserva acentos Latin-1
OK: el tiempo medido respeta los dos pulsos por bit
OK: el nodo A registra el CRC invalido
OK: el nodo sigue vivo despues del CRC invalido
OK: la trama incompleta no genera un mensaje
OK: el nodo sigue entregando mensajes despues de la trama incompleta

OK: prueba integral de la fase 5
```

---

## 16. Migración a BeagleBone

Checklist:

- [x] Implementar el nodo en C99.
- [x] Probar A/B con enlace falso.
- [x] Probar el gateway HTML sin hardware.
- [ ] Copiar las fuentes a la Beagle A y a la Beagle B.
- [ ] Compilar directamente en cada Beagle con `make`.
- [ ] Verificar `gcc`, `make`, `/sys/class/gpio` y `/sys/bus/iio/devices`.
- [ ] Liberar los pines y revisar el pin mux.
- [ ] Probar el LED y el sensor de luz de cada equipo.
- [ ] Calibrar `--on-level` y `--off-level`.
- [ ] Ajustar `--clock-hz`, `--adc-samples` y `--adc-interval-ms`.
- [ ] Medir frecuencia y jitter reales.
- [ ] Documentar incompatibilidades y valores usados.

Comandos de diagnóstico:

```bash
uname -a
uname -m
dpkg --print-architecture
gcc --version
make --version
ls -l /sys/class/gpio
ls -l /sys/bus/iio/devices
ls -l /sys/class/remoteproc
```

El paso a paso de pines, cableado, calibración y prueba física está en la
sección [Isaac](#isaac).

---

## 17. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Timing de GPIO por sysfs es lento o irregular | `clock_hz` bajo (20 Hz), muestreo source-synchronous y medición real; PRU solo si se justifica |
| Diferencia entre la PC y Python 3.5.3 | Evitar APIs modernas en el gateway y probar con `python3` de la Beagle si se migra |
| ADC saturado o con ruido | Calibrar umbrales, usar promedio y revisar la referencia de 1.8 V |
| Medios analógicos inestables | Circuitos de acondicionamiento, comparadores y prueba por canal |
| Reloj directo requiere referencia de tierra | Unir GND de ambos equipos y verificar el cableado |
| Pines ocupados o pin mux incorrecto | Revisar `/sys/class/gpio` y `phy_node --trace` antes de integrar |
| Pérdida de sincronización | Reloj directo, dos pulsos por bit, SYN/SFD y reloj real medido |
| ACK/NACK lentos por stop-and-wait | No implementado todavía; queda para la fase 7 |
| Selección distinta de CRC/codificación | Configuración explícita y validación al iniciar |
| Repositorio git mezclado con el home | Mantener el proyecto aislado y agregar `.gitignore` |

---

## 18. Bitácora de sesiones

| Fecha | Avance | Pendiente |
|---|---|---|
| 2026-11-13 | Lectura de restricciones, decisiones de arquitectura, plan por fases y creación de `implementacion.md` | Estructura base, núcleo del protocolo y prueba terminal a terminal |
| 2026-11-13 | Implementados CRC-8, trama PHY, presentación UTF-8/Latin-1, bitstream por carriles, demo `terminal_chat.py` y 46 tests. Verificado en Python 3.14 local y Python 3.5 en contenedor. | Simulador de bus, sincronización por idle, transceiver, ARQ/ACK/NACK y LOG |
| 2026-11-13 | Agregados `Makefile.emisor` y `Makefile.receptor` para pruebas terminal a terminal, envío a archivo, FIFO y demostración de CRC inválido. | Integrar los Makefiles al flujo con simulador y enlace ARQ |
| 2026-11-13 | Agregado `Makefile` general con emisor/receptor interactivos por TCP, traza SYN/SFD/DATA/CRC, ACK/NACK, timeout, reintentos, deduplicación y corrupción inducida. Verificado `hola` carácter por carácter y recuperación CRC -> NACK -> retransmisión. | Reemplazar el transporte TCP de prueba por el simulador/GPIO de la Capa Física |
| 2026-11-13 | El receptor acumula el texto decodificado y reconstruye el mensaje completo al recibir `\n`, incluyendo UTF-8 multibyte y aviso de mensaje incompleto. Verificado con `hola mundo` y `Hola señor 😀`. | Extraer el acumulador de mensajes a la capa de aplicación reutilizable |
| 2026-11-14 | Portada la lógica de mensajes a C99 en `c/`: CRC, trama, presentación UTF-8/Latin-1, transporte TCP de prueba, ARQ stop-and-wait, logs y reconstrucción del mensaje. `make emisor`/`make receptor` usan C por defecto; Python queda como referencia. Verificado C-C con `hola mundo`, `Hola señor 😀` y Latin-1 `Mañana`, incluyendo corrupción -> NACK -> reintento -> ACK. | Implementar el backend GPIO y conectar el transporte al simulador/BeagleBone |
| 2026-11-15 | Fases 0 a 2: configuraciones A/B, `phy_rx` con canal falso, canal TX GPIO con dos pulsos por bit, canal RX de luz con clock externo y ADC, nodo `phy_node` con hilos RX/TX y API TCP, enlace falso bidireccional y pruebas A/B. | Probar identidad de usuario y relay |
| 2026-11-15 | Fase 3: usuario guardado en la conexión TCP, frames `HELLO` antes de cada mensaje, asociación del usuario en B y cola de mensajes recibidos en A para el inbox. `phy_node` reemplaza a `beagle_server.py` en el camino del chat. | Gateway HTTP y HTML |
| 2026-11-15 | Fase 4: gateway local contra la API del nodo, `POST /api/message`, `GET /api/inbox`, usuario en el HTML, polling y modos `Tramas PHY` / `Bits sin trama`. El puerto de trabajo quedó en `7000`. | Prueba integral sin hardware |
| 2026-11-16 | Fase 5: simulador `phy_sim` con enlace falso y temporización, `make prueba-integral`, pruebas HTML -> A -> B, B -> A -> HTML, Latin-1, usuarios distintos, CRC corrupto, trama incompleta y medición de tiempo con dos pulsos por bit. | Fase 6 física con Isaac |
| 2026-11-16 | Fase 7 software: resincronización RX por ventana móvil de 44 bits, ACK/NACK stop-and-wait en `phy_node`, deduplicación por SEQ, timeout/reintentos configurables y `--no-ack`. `make prueba-integral` sigue pasando; `test_node` cubre NACK y timeout. | Validar ACK/NACK sobre el enlace físico y corregir el receptor LDR/AIN0 |
| 2026-11-16 | Fase 8 software: handshake SYN / SYN-ACK / ACK al arrancar, control `SYN`/`SYN-ACK`, estados `disconnected`/`syn_sent`/`syn_received`/`connected`, reintento cada 1 s y `--no-handshake`. `test_node` y `test_fase5.py` pasan con handshake activo. | Validar handshake y ACK sobre el enlace físico y corregir el receptor LDR/AIN0 |

---

## 19. Próximo paso

Las fases 0 a 5 y la fase 7 en software están completas y verificadas sin
hardware. El próximo trabajo es la fase 6: corregir el receptor LDR/AIN0,
conectar las dos BeagleBone, calibrar el enlace de luz y validar ACK/NACK y el
chat real.

El paso a paso, pines, cableado, comandos de calibración y criterios de
aceptación están en la sección [Isaac](#isaac). La fase 7 (ACK/NACK y
retransmisión) queda para después de validar el enlace físico.

---

<a name="isaac"></a>
## Isaac — Cómo seguir con la parte física

Esta sección es la guía para la fase 6: conectar las dos BeagleBone, calibrar el
enlace de luz y validar el chat real. No hay que cambiar la lógica de tramas,
identidad, relay ni HTML si el hardware responde; el trabajo es de pines,
cableado, umbrales y tiempos.

### Alcance

- Enlace por luz entre Beagle A y Beagle B.
- Reloj directo por GPIO y dato por LED/ADC.
- Dos pulsos de reloj por bit.
- Chat PC ↔ A ↔ B usando `phy_node`, el gateway local y el HTML.
- Fuera de alcance: sonido, vibración, ACK/NACK y retransmisión.

### Pines asignados

| Equipo | Señal | GPIO / pin físico | Dirección |
|---|---|---|---|
| Beagle A | Clock TX | gpio44 / P8_12 | Salida |
| Beagle A | Clock RX | gpio45 / P8_11 | Entrada |
| Beagle A | Dato TX (LED) | gpio60 / P9_12 | Salida |
| Beagle A | Dato RX | AIN0 / P9_39 | Entrada ADC |
| Beagle B | Clock TX | gpio45 / P8_11 | Salida |
| Beagle B | Clock RX | gpio44 / P8_12 | Entrada |
| Beagle B | Dato TX (LED) | gpio60 / P9_12 | Salida |
| Beagle B | Dato RX | AIN0 / P9_39 | Entrada ADC |

Cableado:

```text
A P8_12 (gpio44 salida)  ->  B P8_12 (gpio44 entrada)
B P8_11 (gpio45 salida)  ->  A P8_11 (gpio45 entrada)

A P9_12 (gpio60 LED)     ->  B P9_39 (AIN0, sensor de luz)
B P9_12 (gpio60 LED)     ->  A P9_39 (AIN0, sensor de luz)

A GND                    ->  B GND
```

Alimentación y ADC:

- VDD_ADC: P9_32, 1.8 V.
- GND_ADC: P9_34.
- AIN0: P9_39.
- No aplicar 3.3 V ni 5 V al ADC.
- Unir las tierras de ambas placas; sin GND común el enlace no es confiable.
- Verificar la polaridad del LED y del fototransistor/fotodiodo.
- El LED debe iluminar el sensor del equipo contrario.

### 1. Copiar y compilar

Desde la PC, con la Beagle A accesible por `192.168.7.2`:

```bash
make -C c clean
ssh debian@192.168.7.2 'mkdir -p ~/capa_fisica'
scp -r c config Makefile README.md docs \
  debian@192.168.7.2:~/capa_fisica/
ssh debian@192.168.7.2 'cd ~/capa_fisica && make clean && make'
```

No copiar `c/phy_node` compilado en la PC: es `x86-64`. Compilar en cada
BeagleBone para generar el binario ARM.

Para B, repetir con su IP si hay acceso de red. Si B no es accesible por red,
copiar las fuentes por USB o el medio disponible.

### 2. Verificar el entorno

En cada Beagle:

```bash
uname -a
uname -m
gcc --version
make --version
ls -l /sys/class/gpio
ls -l /sys/bus/iio/devices
```

Si `/sys/class/gpio` no existe o no hay permisos, el nodo debe ejecutarse con
`sudo`. Si el ADC no aparece, revisar `/sys/bus/iio/devices` y el pin mux de
AIN0.

### 3. Levantar los nodos

En Beagle A:

```bash
cd ~/capa_fisica
sudo ./c/phy_node --role A --port 7000 --trace
```

En Beagle B:

```bash
cd ~/capa_fisica
sudo ./c/phy_node --role B --port 7000 --trace
```

Opciones útiles para calibrar:

| Opción | Defecto | Uso |
|---|---|---|
| `--clock-hz` | `20` | Pulsos de reloj por segundo |
| `--on-level` | `2100` | Umbral ADC para nivel encendido |
| `--off-level` | `1700` | Umbral ADC para nivel apagado |
| `--adc-samples` | `5` | Muestras promediadas por pulso |
| `--adc-interval-ms` | `2` | Espera entre muestras del ADC |

Los pines por defecto de `--role A` y `--role B` coinciden con la tabla.
`config/node_a.json` y `config/node_b.json` son la referencia de pines; el
binario actual recibe los valores por línea de comandos.

### 4. Calibrar LED, sensor y umbrales

Primero probar cada placa por separado:

```bash
make probe-luz SUDO=sudo PROBE_ARGS="--led --blink 0.5"
make rx-luz SUDO=sudo PROBE_ARGS="--pattern 1110 --clock-hz 2"
```

Estas herramientas de diagnóstico usan gpio44, gpio60 y AIN0 en la misma
placa. Usarlas con el enlace A-B desconectado o sobre la placa A para no
pisar el reloj de recepción de B.

Para calibrar el enlace real:

1. Encender y apagar el LED del equipo remoto.
2. Observar los valores del ADC en la placa receptora.
3. Elegir `--on-level` y `--off-level` entre los valores de luz y oscuridad.
4. Dejar una histéresis amplia para evitar ruido.
5. Empezar con `--clock-hz 20`. Si hay CRC inválidos, bajar a `10` o `5`.
6. Aumentar `--adc-samples` si hay ruido y el pulso alto lo permite.
7. Recordar que el tiempo de muestreo es aproximadamente
   `(adc_samples - 1) * adc_interval_ms` y debe entrar en el 40 % alto del pulso.

### 5. Probar el chat real

En la PC:

```bash
python3 servers/local/local_server.py
```

El gateway usa por defecto `192.168.7.2:7000`. Verificar:

```bash
curl http://127.0.0.1:8080/api/ping
```

Debe responder `{"beagle":"ok","response":"PONG"}`.

Abrir `http://127.0.0.1:8080/`, ingresar el usuario y enviar en modo
`Tramas PHY`.

En la terminal de B debería aparecer el usuario y el mensaje:

```text
[B] MESSAGE ... isaac hola
```

Para enviar desde la terminal de B hacia el HTML:

```bash
printf 'HELLO isaac\nSEND isaac hola desde B\n' | nc -q 1 127.0.0.1 7000
```

Luego el HTML debería mostrar el Mensaje en el inbox con usuario `isaac` y
origen `B`.

### 6. Criterios de aceptación de la fase 6

- A transmite y B recibe.
- B transmite y A recibe.
- El HTML muestra los mensajes de ambos equipos.
- El usuario correcto aparece en el mensaje.
- El nodo no se cae con un CRC inválido ni con una trama incompleta.
- No se modifica el protocolo, solo pines, umbrales y `clock_hz`.

### 7. Problemas frecuentes

| Síntoma | Revisar |
|---|---|
| `Connection refused` en la PC | El nodo no está en `7000` o no está corriendo |
| `No se pudo abrir GPIO` | Ejecutar con `sudo` y revisar `/sys/class/gpio` |
| `No se pudo abrir el receptor de luz` | Ruta del ADC, pin mux de AIN0 y permisos |
| ADC siempre en 0 o siempre al máximo | Polaridad, divisor, referencia de 1.8 V y GND común |
| CRC inválidos | Bajar `clock_hz`, ajustar umbrales y promediar más |
| No hay mensajes en B | Revisar que el LED remoto ilumine el sensor y el clock cruzado |
| El puerto no aparece | Verificar que el proceso no haya quedado en otro puerto con `ss -ltn` |

### 8. Qué registrar al terminar

Agregar una fila a la bitácora con:

- Pines y cableado usados.
- Valores finales de `on-level`, `off-level`, `clock_hz`, `adc-samples` y
  `adc-interval-ms`.
- Errores de CRC observados.
- Frecuencia y jitter medidos.
- Fotos o diagramas del montaje.
- Problemas de pin mux o pines ocupados.
- Confirmación de A -> B, B -> A y HTML en ambos sentidos.

La meta de la fase 6 es que el mismo software probado con `phy_sim` funcione
sobre la luz real sin tocar la lógica del protocolo.
