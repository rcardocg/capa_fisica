# Servidores local y Beagle

## Estado del documento

| Campo | Valor |
|---|---|
| Version | 0.8 (ACK/NACK y resincronizacion) |
| Fecha | 2026-11-16 |
| Alcance | Enviar mensajes con usuario y mostrar los recibidos en el HTML |
| Incluye | `/api/message`, `/api/inbox`, `/api/bits`, polling, nodo A en 7000, ACK/NACK |
| No incluye | Otros canales, historial persistente, ventana deslizante |
| Proxima ampliacion | Fase 6, conexion fisica y prueba real; fase 7 ya en software |

## Que hace este documento

Explica que hace cada servidor, como ejecutarlo y como comprobar que los bits
llegan al circuito. El servidor local corre en la PC y el servidor de la Beagle
corre en la BeagleBone. Se mantienen separados para copiar solo el de la Beagle
con `scp`.

## Archivos

| Archivo | Donde corre | Funcion |
|---|---|---|
| `servers/beagle/beagle_server.py` | BeagleBone | Recibe `BITS` y controla gpio44/gpio60 |
| `servers/local/local_server.py` | PC | Gateway HTTP: `/api/message`, `/api/inbox`, `/api/bits` |
| `servers/local/index.html` | PC | Textbox, boton, conversion y ventana de mensajes |
| `servers/local/translator.js` | PC | Convierte español a ASCII y ASCII a binario |
| `c/tools/phy_light_tx.c` | PC | Emisor C de mensajes en tramas PHY |
| `c/src/phy_tx.c` | PC | Construye una trama por byte y la envia al canal |
| `c/src/phy_channel.c` | PC | Interfaz comun de canal de transmision |
| `c/src/phy_tcp_channel.c` | PC | Canal TCP que envia bits al servidor Beagle |
| `c/phy_node` | Beagle A | Nodo del chat en el puerto `7000` desde la fase 3 |

Desde la fase 3, el camino del chat en A usa `c/phy_node` en el puerto `7000`.
La fase 4 conecta el servidor local con esa API. `beagle_server.py` queda como
herramienta de diagnostico de `BITS` en otro puerto, por ejemplo `7002`.

## Como ejecutar

### 1. Nodo de la Beagle A

Con hardware (necesita permiso sobre `/sys/class/gpio` y el ADC):

```bash
sudo ./c/phy_node --role A --port 7000 --trace
```

El nodo escucha la API en `7000`, transmite por luz y encola los mensajes
recibidos de B para el inbox. El servidor local se conecta a esa API.

`beagle_server.py` queda como alternativa de diagnostico para el emisor suelto
`phy_light_tx`:

```bash
sudo python3 servers/beagle/beagle_server.py --port 7002
python3 servers/beagle/beagle_server.py --dry-run
```

### 2. Servidor local en la PC

```bash
python3 servers/local/local_server.py
```

Por defecto escucha en `127.0.0.1:8080` y apunta al nodo A en `192.168.7.2:7000`.
Opciones utiles: `--beagle-host`, `--beagle-port`, `--port`.

### 3. Probar la conexion

```bash
curl http://127.0.0.1:8080/api/ping
```

Respuesta esperada:

```json
{"beagle": "ok", "response": "PONG"}
```

### 4. Enviar bits

```bash
curl -X POST http://127.0.0.1:8080/api/bits \
  -H 'Content-Type: application/json' \
  -d '{"bits":"1110"}'
```

Respuesta esperada:

```json
{"beagle": "ok", "bits": "1110", "response": "OK BITS 4"}
```

`bits` debe ser una cadena de `0` y `1` de hasta 128 caracteres. Si es
invalida, el servidor local responde `400`. Si el nodo no responde o falla
GPIO, responde `502`.

### 5. Enviar mensaje con usuario

```bash
curl -X POST http://127.0.0.1:8080/api/message \
  -H 'Content-Type: application/json' \
  -d '{"user":"ana","text":"hola"}'
```

El servidor local abre una conexion con el nodo A y envia
`SEND ana hola`. El nodo transmite primero el usuario con frames `HELLO` y
luego el texto con frames `DATA`. Respuesta esperada:

```json
{"beagle": "ok", "response": "OK SEND 0"}
```

`user` no puede tener espacios y admite hasta 63 caracteres. `text` no puede
estar vacio y admite hasta 128 caracteres. Si el nodo no responde, el servidor
local responde `502`.

### 6. Consultar el inbox

```bash
curl 'http://127.0.0.1:8080/api/inbox?since=0'
```

El servidor consulta `POLL 0` al nodo de A y devuelve solo los eventos
`MESSAGE`:

```json
{
  "beagle": "ok",
  "messages": [
    {"id": 7, "user": "bob", "text": "hola", "source": "B"}
  ],
  "next": 18
}
```

La pagina guarda `next` y lo usa como `since` en el siguiente polling.

### 7. Interfaz web

Con el servidor local arriba, abrir:

```text
http://127.0.0.1:8080/
```

La pagina tiene un campo para escribir un mensaje en español, un selector de
modo, un boton `Enviar` y una ventana con los mensajes.

Modos:

- `Tramas PHY`: pide el usuario al iniciar, envia el mensaje con
  `POST /api/message` y el nodo A lo transmite con `HELLO` + `DATA`.
- `Bits sin trama`: conserva el flujo anterior, convierte el texto con `ord`
  y envia el binario a `POST /api/bits`.
- `Tramas al LED (diagnóstico)`: ejecuta el emisor C con `POST /api/frames` y
  muestra en vivo las lineas `[TX]` y los bits enviados a la Beagle.

La pagina hace polling de `GET /api/inbox?since=N` cada 800 ms y muestra los
mensajes recibidos desde B con su origen y usuario. En modo bits se mantiene el
limite de 128 bits (16 caracteres).

En ambos casos `translator.js` muestra los apartados:

```text
Español → ASCII
h:104  o:111  l:108  a:97

ASCII → Binario
01101000 01101111 01101100 01100001
```

En modo bits, el binario se envia sin espacios. Cada caracter ocupa 8 bits, por
eso el limite actual de 128 bits permite hasta 16 caracteres. Los caracteres
con codigo mayor a 255 se rechazan con un mensaje de error.

En modo tramas, el servidor local envia el usuario y el texto con
`POST /api/message` al nodo de A. El nodo arma los frames `HELLO` y `DATA` con
Latin-1 y los transmite por luz. La pagina consulta `/api/inbox` para mostrar
los mensajes que llegan desde B.

### 8. Emisor C de tramas al LED

El emisor C arma una trama PHY por cada byte del mensaje y envia los 44 bits
al servidor de la Beagle. La Beagle solo comparte esos bits por gpio44/gpio60;
no conoce la trama.

```text
Emisor C (PC) --BITS 44 bits--> Servidor Beagle --dos pulsos--> LED
```

Ejecucion:

```bash
# En la Beagle A
sudo ./c/phy_node --role A --port 7000 --trace

# En la PC
make tx-luz HOST=192.168.7.2 MSG="hola"
```

Sin `MSG` el emisor queda interactivo leyendo mensajes por stdin:

```bash
./c/phy_light_tx --host 192.168.7.2 --port 7000
```

Salida tipica:

```text
[TX] frame=1/5 control=DATA seq=0 data=0x68 'h' crc=0x1F
[TX] wire=10101010101010101101000000000110100000011111
```

Cada byte genera un frame de 44 bits: SYN, SFD, CONTROL, SEQ, DATA y CRC.
No hay ACK/NACK ni receptor en esta etapa. Se mantienen dos pulsos de reloj
por bit. Con `--clock-hz 20` cada frame tarda unos 4.4 segundos.

La logica reutilizable esta separada del canal:

- `phy_tx.c`: mensaje -> bytes -> tramas -> bits.
- `phy_channel_t`: interfaz comun con `send_bits`.
- `phy_tcp_channel.c`: implementacion actual que habla con la Beagle.

Para agregar sonido o vibracion se implementa otro canal con la misma
interfaz, sin duplicar la construccion de tramas.

### 9. Tramas desde la pagina

Este endpoint lo usa el modo
`Tramas al LED (diagnóstico)` del HTML; el camino del chat usa
`POST /api/message`. El servidor local expone dos endpoints:

| Metodo | Ruta | Funcion |
|---|---|---|
| `POST` | `/api/frames` | Inicia un trabajo con `{"text":"hola"}` |
| `GET` | `/api/frames?job=1&since=0` | Devuelve lineas nuevas del trabajo |

`POST /api/frames` responde `202` con `{"job": 1}`. El servidor ejecuta el
emisor C con Latin-1 y guarda su salida en un hilo.

`GET /api/frames` responde:

```json
{
  "job": 1,
  "lines": ["[TX] frame=1/5 ..."],
  "next": 9,
  "done": false,
  "ok": null
}
```

El emisor escribe su salida en un hilo y el progreso se consulta con
`since=next`. Cuando `done` es verdadero, `ok` indica si el emisor termino
bien.

Opciones nuevas del servidor local:

| Opcion | Defecto | Uso |
|---|---|---|
| `--emitter` | `c/phy_light_tx` | Binario del emisor C |
| `--emitter-encoding` | `latin-1` | Codificacion usada por el emisor |

La Beagle no ejecuta nada de esto: solo recibe `BITS` y mueve el LED.

## Protocolo TCP del nodo

| Comando | Respuesta | Uso |
|---|---|---|
| `PING` | `PONG` | Verificar conexion |
| `HELLO <usuario>` | `OK HELLO <usuario>` | Guardar el usuario de la conexion |
| `BITS <secuencia>` | `OK BITS <n>` | Enviar bits crudos por el canal de luz |
| `SEND <usuario> <texto>` | `OK SEND <id>` | Enviar usuario con `HELLO` y texto con `DATA` |
| `POLL <since>` | eventos y `NEXT <n>` | Consultar mensajes y progreso |
| `STATUS` | `STATUS ...` | Estado del nodo |
| `QUIT` | `BYE` | Cerrar la conexion |
| otro | `ERR unknown` | Comando no reconocido |

## Comportamiento del envio

- El nodo mantiene un hilo de recepcion y una cola de transmision.
- Cada `SEND` transmite primero el usuario con frames `HELLO` y luego el
  texto con frames `DATA`.
- El LED se pone al valor del bit y se mantienen dos pulsos de reloj por bit.
- Cada pulso dura `1 / clock_hz`; el 40 % inicial esta en alto.
- Con `20 Hz`, cada frame de 44 bits tarda unos 4.4 segundos.
- En `/api/bits`, el nodo responde cuando acepta el trabajo en la cola.
- En `/api/message`, el nodo responde `OK SEND` cuando acepta el mensaje.
- El nodo atiende una conexion de API a la vez.

## ACK/NACK y retransmision

- Cada frame `HELLO` y `DATA` espera un `ACK` del receptor.
- Si el receptor detecta CRC invalido, envia `NACK` con el `SEQ` recibido.
- Si el emisor recibe `NACK` o vence el timeout, retransmite el mismo frame.
- El receptor acepta una sola vez cada `SEQ`; si recibe un duplicado, reenvia
  `ACK` sin volver a entregar el mensaje.
- El receptor se resincroniza buscando `SYN`/`SFD` en una ventana movil, sin
  asumir que empieza en el bit 0 de una trama.
- `BITS` crudo no participa del ACK/NACK. Si se usa mientras el chat tiene ACK
  activo, puede desalinear el seguimiento de `SEQ`; usar `--no-ack` en ambos
  nodos para pruebas de diagnostico.
- Ambos nodos deben usar la misma configuracion de ACK. Un nodo con ACK activo
  espera confirmacion y se bloquea si el otro usa `--no-ack`.

Opciones:

| Opcion | Defecto | Uso |
|---|---|---|
| `--ack-timeout-ms N` | automatico | Espera de ACK/NACK por frame |
| `--ack-retries N` | `3` | Reintentos por frame |
| `--no-ack` | desactivado | Desactiva ACK/NACK en el nodo |

El timeout automatico es `2 * frame_ms + 1000`, con
`frame_ms = 44 * 2 * 1000 / clock_hz`.

## Exportar con scp

Se copia el binario del nodo a la Beagle A:

```bash
scp c/phy_node debian@192.168.7.2:~/cc8/
```

Si se usa el servidor de diagnostico, tambien se copia su carpeta:

```bash
scp -r servers/beagle debian@192.168.7.2:~/cc8/
```

## Prueba integral sin hardware

La fase 5 levanta A y B con el enlace falso y prueba todo el recorrido:

```bash
make prueba-integral
```

Incluye:

- HTML -> A -> B.
- Terminal de B -> A -> HTML.
- Acentos Latin-1 en ambos sentidos.
- Usuarios distintos.
- Frame con CRC corrupto, verificando que el nodo sigue vivo.
- Trama incompleta, verificando que no genera mensaje y el nodo se recupera.
- Medicion de tiempo con dos pulsos por bit y `clock_hz`.

El simulador se puede levantar aparte para inspeccion manual:

```bash
make sim
```

## Pendiente

- Fase 6: conexion fisica y prueba real.
- Fase 7: ACK/NACK y retransmision.
- Reutilizar el envio para sonido y vibracion.
