RESTRICCIONES REALES DEL HARDWARE:

Plataforma objetivo: BeagleBone Black Rev C.
Sistema operativo: Debian GNU/Linux 9 Stretch.
Kernel: 4.14.108-ti-r119.
Implementación: C99, sin dependencias externas.
Compilador objetivo: GCC disponible en Debian Stretch.

Compatibilidad:
- Todo el código debe compilar y ejecutarse en la BeagleBone Black.
- Usar C99 y APIs POSIX disponibles en Debian Stretch.
- No depender de bibliotecas externas para el núcleo del protocolo.
- Mantener el acceso a GPIO aislado del protocolo y de la aplicación.
- No asumir versiones modernas de compilador ni de librerías.

Hardware:
- Evaluar el acceso GPIO por sysfs como primera alternativa.
- Verificar compatibilidad antes de instalar libgpiod u otra biblioteca.
- No asumir que todos los pines están disponibles.
- Considerar la configuración pin mux.
- Evaluar PRU-ICSS del AM3358 como alternativa para operaciones deterministas.
- Mantener separada la lógica del protocolo de la implementación GPIO y PRU.
- No implementar PRU hasta justificarlo mediante pruebas de temporización.

Despliegue:
- Debian Stretch puede no tener repositorios originales disponibles.
- La migración debe poder realizarse offline.
- Compilar directamente en la BeagleBone o copiar el binario compilado para ARM.
- Separar dependencias de desarrollo y hardware.

Antes de implementar:
- Verificar uname -m y dpkg --print-architecture.
- Verificar la versión de gcc y make.
- Verificar la disponibilidad de GPIO y remoteproc.
- Documentar las incompatibilidades detectadas.

La prioridad es producir un backend que funcione en el hardware existente, no modernizar innecesariamente el sistema operativo.
# capa_fisica

## Sonda del primer canal: luz

Programa de verificación del canal óptico y de los pines que se usarán en la
implementación. Lee el ADC de la BeagleBone, muestra únicamente los cambios de
luz y, opcionalmente, hace parpadear el LED del emisor para probar el enlace
emisor/receptor.

### Pines asignados

| Función | Pin BeagleBone | Recurso |
|---|---|---|
| Receptor de luz | P9_39 | AIN0 |
| LED del emisor | P9_12 | GPIO1_28 / gpio60 |
| Alimentación ADC | P9_32 | VDD_ADC 1.8 V |
| Masa analógica | P9_34 | GNDA |

La entrada ADC tolera como máximo 1.8 V. No conectar 3.3 V ni 5 V al ADC.

### Conexionado del emisor

```text
+3.3V ──[R_LED]── Ánodo LED Cátodo ── C
                                     2N3904
P9_12 ──[4.7kΩ]── B                   E ── GND
```

- LED rojo de 10 mA: `R_LED` de 100–120 Ω.
- LED infrarrojo de 20 mA: `R_LED` de 100 Ω.
- Masa común entre BeagleBone, transistor y fuente del LED.

### Conexionado del receptor

```text
+1.8V (P9_32) ──[LDR]──┬──[1kΩ]── B
                      │           2N3904
                    [10kΩ]         C ──┬── AIN0 (P9_39)
                      │                │
                     GND             [10kΩ]
                                       │
                                     +1.8V (P9_32)

E ── GND
```

El LDR forma un divisor con la resistencia de 10 kΩ. Cuando aumenta la luz, el
transistor conduce y el ADC lee un valor bajo. La señal queda invertida.

El LDR es lento: sirve para validar el canal a baja velocidad. Para 100 Hz
conviene reemplazarlo por un fotodiodo o fototransistor.

### Compilar y ejecutar en la BeagleBone

```bash
make clean
make
make probe-luz SUDO=sudo
```

La sonda detecta si el LED está **presente o ausente** comparando la luz del
ADC con el LED apagado y encendido. La luz ambiente constante no cuenta porque
la detección es diferencial. Para hacer la prueba hay que encender y apagar el
LED con `--led`.

```bash
make probe-luz SUDO=sudo PROBE_ARGS="--led --blink 0.5"
```

### Opciones

```text
--led             enciende y apaga el LED para hacer la prueba
--blink HZ        frecuencia de parpadeo (defecto 1.0 Hz)
--help            ayuda
```

La sonda usa siempre AIN0 (P9_39, con VDD_ADC en P9_32 y GNDA en P9_34) y
controla el LED en gpio60 (P9_12). Sin `--led` el LED no se enciende y la
detección da `0`.

### Salida esperada

Imprime `1` o `0` en cada **medio ciclo de reloj**. La detección compara el
promedio de luz de la fase que termina con el de la fase anterior:

```text
[   2.000 s] 1  (fase=ON avg=3238 dif=+2640)
[   2.500 s] 1  (fase=OFF avg=598 dif=+2640)
[   3.000 s] 1  (fase=ON avg=3235 dif=+2637)
[   3.500 s] 1  (fase=OFF avg=601 dif=+2634)
```

El primer resultado aparece cuando se completa el historial de tres contrastes
(alrededor de dos ciclos); a partir de ahí se actualiza en cada medio ciclo.

- `1` = LED presente.
- `0` = LED ausente.

`1` exige que los últimos tres medios ciclos muestren el mismo sentido y una
diferencia mínima de 200 cuentas. Diferencias chicas o de signo variable se
consideran ruido y dan `0`.

Si el ADC no aparece en `/sys/bus/iio/devices`, revisar la ruta con:

```bash
ls /sys/bus/iio/devices
find /sys -name 'in_voltage0_raw' 2>/dev/null
```

## Prueba de secuencias con clock externo

`c/tools/light_rx_test.c` genera un patrón de luz y lo recibe usando un reloj
externo en **gpio44**. Cada bit dura dos
medios ciclos de reloj y se toman dos muestras por bit: una en cada pulso. El
par `(s1, s2)` corrige una muestra mala por residual o por tiempo de encendido.

Módulos reutilizables para otros canales:

| Archivo | Función |
|---|---|
| `c/src/phy_gpio.c` | Exportar/configurar/leer/escribir GPIO por sysfs |
| `c/src/phy_analog.c` | Buscar y leer entradas ADC de la BeagleBone |
| `c/src/phy_sample.c` | Clasificar nivel con histéresis y decodificar el par |

Ejecución:

```bash
make rx-luz SUDO=sudo PROBE_ARGS="--pattern 1110 --clock-hz 2"
make rx-luz SUDO=sudo PROBE_ARGS="--pattern 1010 --clock-hz 5 --bits 40"
```

Opciones:

```text
--pattern BITS   secuencia de 0 y 1 (defecto 1110)
--clock-hz HZ    frecuencia de los pulsos de reloj (defecto 2.0)
--bits N         termina después de N bits (0 = infinito)
--help           ayuda
```

Salida por bit:

```text
[   1.000 s] bit=  0 esperado=1 s1=1 s2=1 recibido=1 OK
[   2.000 s] bit=  1 esperado=1 s1=1 s2=1 recibido=1 OK
[   3.000 s] bit=  2 esperado=1 s1=1 s2=1 recibido=1 OK
[   4.000 s] bit=  3 esperado=0 s1=0 s2=0 recibido=0 OK
```

Polaridad: `on` = valor alto del ADC (≈3238), `off` = valor bajo (≈598).

## Receptor piezoeléctrico acústico (segundo medio)

Se agregó un receptor acústico con disco piezoeléctrico sin reemplazar el enlace
óptico. Usa **AIN1 / P9_40** y conserva AIN0/P9_39 para el sensor de luz. El
canal implementa la misma interfaz `phy_rx_channel_t`, así que las tramas,
ACK/NACK, handshake y la API TCP no cambian.

```bash
# Recepción acústica; el enlace óptico sigue disponible
sudo ./c/phy_node --role A --port 7000 --rx-medium piezo --piezo-ain 1 --trace

# Enlace acústico completo opcional (dato TX en P9_14 / gpio50)
sudo ./c/phy_node --role A --port 7000 --rx-medium piezo --tx-medium piezo --trace
```

Opciones principales:

| Opción | Defecto | Función |
|---|---:|---|
| `--rx-medium light\|piezo` | `light` | Medio de recepción |
| `--tx-medium light\|piezo` | `light` | Medio de transmisión |
| `--piezo-ain N` | `1` | Canal ADC del piezo, distinto de AIN0 |
| `--piezo-gpio GPIO` | `50` | Dato TX del piezo (P9_14) |
| `--piezo-carrier-hz N` | `1000` | Portadora del emisor acústico |
| `--piezo-on-level N` | `120` | Umbral de envolvente para 1 |
| `--piezo-off-level N` | `60` | Umbral de envolvente para 0 |

La etapa analógica, la tabla completa de pines, la justificación de AIN1 y los
detalles de ruido, umbrales y sincronización están en
[`docs/piezo.md`](docs/piezo.md).

Pruebas del DSP sin hardware:

```bash
make test-piezo
```

## Servidores local y Beagle

La conexion PC-Beagle esta separada en dos programas independientes:

| Archivo | Donde corre | Funcion |
|---|---|---|
| `c/phy_node` | Beagle A | Nodo del chat; API TCP en el puerto `7000` |
| `servers/beagle/beagle_server.py` | BeagleBone | Diagnostico: `PING` y `BITS`; controla gpio44/gpio60 |
| `servers/local/local_server.py` | PC | Gateway HTTP hacia `phy_node` en `7000` |
| `servers/local/index.html` | PC | Interfaz web del chat |
| `servers/local/translator.js` | PC | Español a ASCII y ASCII a binario |

```bash
# En Beagle A (con hardware)
sudo ./c/phy_node --role A --port 7000 --trace

# En la PC
python3 servers/local/local_server.py

# Interfaz web: pide el usuario y conserva los modos
# Tramas PHY y Bits sin trama
# http://127.0.0.1:8080/
```

El servidor local habla con la API del nodo y expone:

| Metodo | Ruta | Funcion |
|---|---|---|
| `GET` | `/api/ping` | `PING` al nodo |
| `GET` | `/api/inbox?since=N` | Mensajes recibidos desde B por polling |
| `POST` | `/api/message` | Envia `{"user":"ana","text":"hola"}` con `SEND` |
| `POST` | `/api/bits` | Envia bits crudos con `BITS` |
| `POST/GET` | `/api/frames` | Diagnostico del emisor C con polling en vivo |

```bash
curl -X POST http://127.0.0.1:8080/api/message \
  -H 'Content-Type: application/json' -d '{"user":"ana","text":"hola"}'

curl 'http://127.0.0.1:8080/api/inbox?since=0'
```

En modo `Tramas PHY` la pagina envia el mensaje con usuario al nodo de A, que
lo transmite por luz y lo entrega a B. En modo `Bits sin trama` conserva el
envio de bits crudos con `/api/bits`. El modo `Tramas al LED (diagnóstico)`
ejecuta el emisor C con `/api/frames` y muestra en vivo las lineas `[TX]`. El
inbox se consulta periodicamente para mostrar en el HTML los mensajes que
llegan desde B con su usuario y origen.

`beagle_server.py` queda como herramienta de diagnostico en otro puerto, por ejemplo `7002`
para el emisor suelto `phy_light_tx`.

## Emisor C de tramas

El emisor C arma una trama PHY por byte y envia los 44 bits al servidor de la
Beagle, que solo se encarga de compartirlos por el canal de luz.

```bash
# En la Beagle A
sudo ./c/phy_node --role A --port 7000 --trace

# En la PC: envia un mensaje
make tx-luz HOST=192.168.7.2 MSG="hola"

# O modo interactivo
./c/phy_light_tx --host 192.168.7.2 --port 7000
```

La construccion de mensajes y tramas esta en C y es independiente del canal:

| Archivo | Funcion |
|---|---|
| `c/src/phy_tx.c` | Mensaje a bytes, tramas y bits |
| `c/src/phy_channel.c` | Interfaz comun `send_bits` por canal |
| `c/src/phy_tcp_channel.c` | Canal TCP hacia el servidor Beagle |

Esta herramienta no incluye ACK/NACK ni receptor: arma y transmite las tramas,
pero no espera confirmacion. El chat de la interfaz web usa `phy_node` y
`/api/message`; este emisor queda como diagnostico.


## Nodo C `phy_node` y prueba A/B

La fase 2 agrega el nodo completo en C, con hilo de recepcion, cola de
transmision y API TCP. Para probar A y B sin hardware se usa un enlace falso
bidireccional en memoria.

```bash
make test-nodo
```

La prueba levanta dos nodos en la misma PC, conectados por el enlace falso, y
verifica:

- `SEND` de A llega como mensaje a B.
- `SEND` de B llega como mensaje a A.
- `BITS` sigue enviando bits crudos.
- La recepcion sigue funcionando mientras el nodo transmite.

La fase 5 agrega la prueba integral con gateway HTTP, enlace falso y
temporizacion:

```bash
make prueba-integral
```

Cubre HTML -> A -> B, B -> A -> HTML, acentos Latin-1, usuarios distintos,
CRC corrupto, trama incompleta y el tiempo con dos pulsos por bit. El
simulador tambien se puede levantar solo con `make sim`.

El binario `c/phy_node` usa el enlace de luz real por GPIO/ADC:

```bash
# Beagle A: clock TX gpio44, clock RX gpio45, LED gpio60, ADC AIN0
./c/phy_node --role A --port 7000 --trace

# Beagle B: clock TX gpio45, clock RX gpio44, LED gpio60, ADC AIN0
./c/phy_node --role B --port 7000 --trace
```

La API TCP acepta `PING`, `HELLO <usuario>`, `BITS <bits>`,
`SEND <usuario> <texto>`, `POLL <since>`, `STATUS` y `QUIT`. `POLL` devuelve
eventos `MESSAGE`, `TX`, `RX`, `ACK`, `NACK`, `TIMEOUT` y `ERROR`, y termina
con `NEXT <n>`.

La fase 7 agrega ACK/NACK stop-and-wait sobre el enlace de luz:

- Cada frame `HELLO` y `DATA` espera un `ACK`.
- Si llega `NACK` o vence el timeout, se retransmite el mismo `SEQ`.
- El receptor descarta duplicados y reenvía `ACK`.
- `BITS` crudo sigue sin ACK; usarlo con `--no-ack` si se mezcla con el chat.
- Opciones: `--ack-timeout-ms N`, `--ack-retries N`, `--no-ack`.
- Ambos nodos deben usar la misma configuracion de ACK.

La fase 8 agrega el handshake de conexion SYN / SYN-ACK / ACK:

- A inicia el handshake y B responde.
- Antes de estar conectados, los mensajes se encolan y se envian al conectar.
- Si la otra Beagle no esta lista, A reintenta cada segundo.
- `--no-handshake` desactiva el handshake para diagnostico o compatibilidad.
- `STATUS` muestra `state=disconnected|syn_sent|syn_received|connected`.

```

La fase 3 agrega la identidad y el relay:

- `HELLO` guarda el usuario de la conexion TCP.
- Cada `SEND` transmite primero el usuario con frames `HELLO` y despues el
  texto con frames `DATA`.
- El receptor asocia ese usuario al siguiente `MESSAGE`.
- En A, los mensajes recibidos de B quedan en el log de `POLL` para que la PC
  los consulte.
- El chat de A usa `phy_node` en el puerto `7000`; `beagle_server.py` queda
  como herramienta de diagnostico de `BITS`.

Detalle de ejecucion, opciones, protocolo y pendientes en
[`docs/servidores.md`](docs/servidores.md).
