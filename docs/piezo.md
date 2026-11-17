# Receptor piezoeléctrico acústico

Este documento describe la incorporación de un segundo medio físico de
recepción, basado en un disco piezoeléctrico, sin reemplazar ni modificar el
enlace óptico existente.

La implementación es aditiva:

- El enlace óptico sigue siendo el medio por defecto.
- El nuevo canal se selecciona con `--rx-medium piezo`.
- El protocolo, las tramas, ACK/NACK, handshake y la API TCP no cambian.
- La transmisión acústica es opcional y se selecciona con
  `--tx-medium piezo`.

---

## 1. Conexión eléctrica del piezoeléctrico

El disco piezoeléctrico es un transductor capacitivo de alta impedancia. No se
conecta directamente al ADC: hay que descargarlo, protegerlo, amplificarlo,
detectar la envolvente y limitar la señal a 0..1.8 V.

### 1.1 Etapa recomendada

```text
                 R1 1MΩ
   ┌──────────────/\/\/──────────────┐
   │                                 │
 PIEZO+ ──┬── R2 10kΩ ── C1 100nF ── nodo A
          │                              │
 PIEZO- ──┴──────────────────────── GNDA │
                                          │
                     R3 100kΩ ── VREF 1.8 V
                     R4 100kΩ ── GNDA
                     C2 100nF ── GNDA
                     D1 BAT54S ── nodo A a VREF (cátodo a VREF)
                     D2 BAT54S ── GNDA a nodo A (cátodo a nodo A)
                                          │
                                     (+) op-amp
                                      MCP6001 / OPA344
                                      alimentado a 1.8 V
                                      (no desde P9_32)
                                          │
                     R5 1kΩ ── (-)       │
                     R6 3.9kΩ ── C3 10µF ── GNDA
                                          │
                                      salida op-amp
                                          │
                                      D3 BAT54S
                                          │
                     R7 100kΩ ── GNDA    │
                     C4 100nF ── GNDA    │
                                          │
                     R8 1kΩ ─────────── AIN1 (P9_40)
                                          │
                     D4 BAT54S doble: salida a GNDA y a VREF 1.8 V
```

Notas de diseño:

- `R1 = 1 MΩ` en paralelo con el disco proporciona la ruta de descarga y la
  referencia de continua. Sin ella, la carga del piezo puede quedar flotando.
- `R2` limita la corriente de entrada y junto con `C1` forma un pasa-altos.
- `R3`/`R4` polarizan la señal en `VREF/2 = 0.9 V`. La etapa debe quedar
  centrada para aprovechar todo el rango del ADC.
- `D1` y `D2` son un BAT54S doble. No alcanza con un Zener de 1.8 V: un Zener
  tiene tolerancia, capacidad parásita, corriente de fuga y puede recortar por
  encima de 1.8 V. La protección real es `R2` + diodos Schottky a las dos
  rieles, con corriente limitada.
- El amplificador operacional debe ser rail-to-rail y alimentarse con una
  fuente de 1.8 V regulada. **No usar P9_32 como alimentación del op-amp**:
  P9_32 es la referencia del ADC y no debe cargarse con la corriente de un
  amplificador.
- `R5`/`R6`/`C3` dan ganancia de continua unitaria y ganancia de alterna
  aproximada de `1 + R6/R5 = 4.9`. Así la salida queda centrada en 0.9 V y las
  ráfagas acústicas se amplifican sin desplazar el punto de operación.
- `D3`, `R7` y `C4` forman un detector de pico o envolvente. Es necesario
  porque el ADC por sysfs se lee mucho más lento que la portadora acústica.
- `R8` y `D4` son la última barrera antes de AIN1. La señal que llega al ADC
  debe quedar estrictamente entre 0 y 1.8 V.
- Agregar `100 nF` de desacople en la alimentación del op-amp y `10 µF` si la
  fuente es compartida.
- Unir GNDA de la etapa analógica con GND de la BeagleBone. No conectar la
  señal del piezo a 3.3 V ni a 5 V.

### 1.2 Protección estricta de la entrada

La entrada del ADC no debe recibir tensiones fuera de 0..1.8 V. La secuencia
de protección recomendada es:

1. Resistencia serie de 10 kΩ o mayor para limitar la corriente.
2. Diodos Schottky a GNDA y a VREF 1.8 V.
3. Amplificador con alimentación de 1.8 V, de modo que su salida no pueda
   superar el riel.
4. Resistencia serie de 1 kΩ y segundo par de Schottky junto al ADC.
5. Verificar con osciloscopio que en AIN1 nunca haya picos fuera de rango
   antes de conectar la BeagleBone.

Un Zener de 1.8 V directo no es suficiente: puede conducir antes o después de
1.8 V, tiene capacidad que degrada los flancos y no limita la corriente por sí
solo.

### 1.3 Transmisión acústica opcional

Para el sentido inverso se puede usar el mismo tipo de disco como emisor:

```text
P9_14 (gpio50) ── R9 1kΩ ── B
                             2N3904 / MOSFET
+3.3 V ── R10 100Ω ── C5 10µF ── colector/drenador
                                       │
                                     PIEZO
                                       │
                                     GND
```

- El GPIO conmuta una portadora de 1 kHz durante la fase alta del reloj.
- Un bit 1 genera una ráfaga; un bit 0 deja silencio.
- El reloj sigue siendo la línea externa compartida del enlace.
- Para frecuencias altas o temporización más estable, evaluar PWM o PRU. Con
  sysfs la portadora de 1 kHz es un compromiso práctico.

---

## 2. Pines ocupados y disponibles

### 2.1 Ocupados por el enlace óptico actual (no tocar)

| Pin | Recurso | Uso |
|---|---|---|
| P8_12 | GPIO44 | Reloj |
| P8_11 | GPIO45 | Reloj |
| P9_12 | GPIO60 | Dato TX (LED) |
| P9_39 | AIN0 | Dato RX óptico |
| P9_32 | VDD_ADC 1.8 V | Referencia/alimentación del ADC |
| P9_34 | GNDA | Masa analógica |

### 2.2 Canales analógicos del conector P9

| Pin | Canal | Estado | Uso propuesto |
|---|---|---|---|
| P9_39 | AIN0 | Ocupado | Receptor óptico |
| P9_40 | AIN1 | Libre | **Receptor piezoeléctrico** |
| P9_37 | AIN2 | Libre | Alternativa |
| P9_38 | AIN3 | Libre | Alternativa |
| P9_33 | AIN4 | Libre | Alternativa con precaución |
| P9_36 | AIN5 | Libre | Alternativa |
| P9_35 | AIN6 | Libre | Alternativa |
| P9_32 | VDD_ADC | Reservado | No usar como señal |
| P9_34 | GNDA | Reservado | No usar como señal |

### 2.3 Pin digital libre para el emisor piezo opcional

| Pin | GPIO | Estado | Uso propuesto |
|---|---|---|---|
| P9_14 | GPIO50 | Libre | Dato TX piezo (defecto) |
| P9_16 | GPIO51 | Libre | Alternativa |
| P8_16 | GPIO46 | Libre | Alternativa |

Antes de usarlo verificar el pin mux:

```bash
config-pin p9.14 gpio
# o, si no existe config-pin, revisar el device tree overlay y exportar gpio50
echo 50 > /sys/class/gpio/export
```

---

## 3. Justificación del canal ADC seleccionado

Se elige **AIN1 / P9_40**:

- Es distinto de AIN0, que queda reservado para el receptor óptico.
- Está junto a AIN0 y GNDA, lo que simplifica el ruteo analógico y la masa.
- Es un canal ADC dedicado del AM3358, sin otra función de datos del enlace.
- No es P9_32 ni P9_34, que son alimentación y referencia.
- Su ruta IIO es `in_voltage1_raw` y se puede verificar sin cambiar el kernel:

```bash
ls /sys/bus/iio/devices
cat /sys/bus/iio/devices/iio:device0/in_voltage1_raw
```

El ADC es de 12 bits y acepta 0..1.8 V. Un conteo equivale aproximadamente a
`1.8 V / 4096 = 0.44 mV`. La impedancia de entrada no es infinita, por eso la
etapa incluye un buffer/amplificador y una resistencia serie de 1 kΩ.

AIN4/P9_33 se deja como alternativa porque en algunas configuraciones o capes
puede estar afectado por funciones del sistema; AIN1 es la opción más directa.

---

## 4. Arquitectura de integración con el protocolo existente

La integración sigue las mismas capas del enlace óptico:

```text
Adquisición analógica     phy_analog.c        lee in_voltage1_raw
        │
Acondicionamiento DSP     phy_piezo_dsp.c     baseline, envolvente, umbral
        │
Canal RX (HAL)            phy_piezo_rx_channel.c
        │                  implementa phy_rx_channel_ops_t
        │
Detección de eventos      phy_sample.c        histéresis y par (s1,s2)
        │
Decodificación de trama   phy_rx.c            SYN/SFD/CRC sin cambios
        │
Protocolo                 phy_node.c          HELLO/DATA/ACK/NACK sin cambios
        │
Aplicación                API TCP y HTML      sin cambios
```

Archivos nuevos:

| Archivo | Función |
|---|---|
| `c/include/phy_piezo_dsp.h` + `c/src/phy_piezo_dsp.c` | Línea base, envolvente y detección |
| `c/include/phy_piezo_rx_channel.h` + `c/src/phy_piezo_rx_channel.c` | Canal RX piezo, misma interfaz que luz |
| `c/include/phy_piezo_tx_channel.h` + `c/src/phy_piezo_tx_channel.c` | Canal TX piezo opcional |
| `c/tests/test_piezo.c` | Pruebas del DSP sin hardware |

La pieza clave es que `phy_piezo_rx_channel_t` implementa exactamente
`phy_rx_channel_ops_t`:

- `wait_idle`: espera reloj en bajo.
- `recv_sample`: espera flanco de subida, muestrea AIN1 y clasifica.
- `close`: libera el GPIO de reloj.

Por eso `phy_rx_frame`, `phy_rx_message`, `phy_node` y todo lo superior no
saben si el bit vino de luz o de sonido. El medio se intercambia en el arranque
del nodo:

```bash
# Enlace óptico actual, sin cambios
sudo ./c/phy_node --role A --rx-medium light --tx-medium light

# Recepción acústica con el enlace óptico como transmisor
sudo ./c/phy_node --role A --rx-medium piezo --tx-medium light

# Enlace acústico completo
sudo ./c/phy_node --role A --rx-medium piezo --tx-medium piezo
```

Opciones nuevas:

| Opción | Defecto | Función |
|---|---:|---|
| `--rx-medium light\|piezo` | `light` | Medio de recepción |
| `--tx-medium light\|piezo` | `light` | Medio de transmisión |
| `--piezo-ain N` | `1` | Canal ADC del piezo; debe ser distinto de AIN0 |
| `--piezo-gpio GPIO` | `50` | GPIO de dato TX del piezo |
| `--piezo-carrier-hz N` | `1000` | Portadora del emisor acústico |
| `--piezo-on-level N` | `120` | Umbral de envolvente para 1 |
| `--piezo-off-level N` | `60` | Umbral de envolvente para 0 |
| `--piezo-baseline-shift N` | `6` | Divisor del seguimiento de línea base |

El simulador y las pruebas de protocolo no cambian. El DSP se prueba con
muestras sintéticas mediante `make test-piezo`, y el resto sigue usando
`phy_fake_rx_channel` y `phy_fake_link`.

---

## 5. Diferencias entre recepción óptica y acústica

| Aspecto | Óptica (AIN0) | Acústica (AIN1) |
|---|---|---|
| Transductor | LDR o fototransistor | Disco piezoeléctrico |
| Naturaleza | Intensidad de luz, casi continua | Presión acústica alterna |
| Polarización | Divisor con 1.8 V | Bias a 0.9 V y acople AC |
| Señal útil | Nivel absoluto de luz | Envolvente o desviación respecto de la línea base |
| Umbrales | `on_level=2100`, `off_level=1700` | `piezo_on_level=120`, `piezo_off_level=60` |
| Acondicionamiento | Comparador o divisor | 1 MΩ, protección, amplificador, detector de envolvente |
| Ruido dominante | Luz ambiente | Ruido acústico, vibración, zumbido de 50/60 Hz |
| Ancho de banda | Bajo, limitado por el LDR | Depende de la portadora y del detector |
| Deriva | Cambia con luz ambiente | Cambia con temperatura y bias |
| Protección | Limitada por el divisor | Explícita a 0 y 1.8 V en varias etapas |
| Calibración | Con LED encendido/apagado | Con silencio y ráfaga de prueba |

La diferencia más importante es que la luz se puede clasificar por nivel
absoluto, mientras que el piezo entrega una señal alterna que hay que
rectificar o detectar por envolvente. Por eso el canal acústico usa
`phy_piezo_monitor_t`, que calcula la desviación máxima respecto de una línea
base y solo actualiza esa línea base cuando el nivel está por debajo del umbral
de ruido.

---

## 6. Ruido, umbrales, muestreo y sincronización

### 6.1 Ruido

- Zumbido de 50/60 Hz: apantallar el cable del piezo y mantenerlo lejos de
  fuentes de alimentación.
- Vibración mecánica: el disco detecta golpes y pasos. Fijarlo con espuma o
  silicona y usar una ventana de muestreo sincronizada con el reloj.
- Ruido de alta impedancia: el 1 MΩ y el cable largo forman una antena.
  Usar cable corto y blindado, y soldar la malla a GNDA en un solo extremo.
- Ruido de la fuente: desacoplar el op-amp y no alimentarlo desde P9_32.
- Diafonía con el LED: separar físicamente el LED del disco y usar el reloj
  para muestrear solo durante la ráfaga.

### 6.2 Umbrales

- Calibrar primero con silencio: leer `in_voltage1_raw` y registrar la
  desviación de ruido.
- Fijar `off_level` por encima del ruido medido y `on_level` con al menos el
  doble de margen.
- La histéresis de `phy_sample_classify` evita que un valor intermedio cambie
  el bit.
- Si el enlace es inestable, subir `on_level`/`off_level` o aumentar
  `adc_samples` para promediar más.
- La línea base se recalibra sola solo cuando el nivel está por debajo de
  `off_level`; no calibrar mientras haya ruido acústico constante.

### 6.3 Muestreo

- El ADC por sysfs es lento. Por eso el hardware incluye detector de
  envolvente: el ADC lee una señal casi continua y no la portadora.
- `adc_samples` y `adc_interval_ms` definen la ventana de integración. La
  ventana debe caber dentro de la fase alta del reloj:
  `settle_ms + (adc_samples - 1) * adc_interval_ms < high_ms`.
- Con `clock_hz=20`, la fase alta es de unos 20 ms. Por defecto se toman 5
  muestras cada 2 ms, que caben con margen.
- Si se sube `clock_hz`, reducir `adc_samples` o usar un detector de
  envolvente más rápido.
- La portadora del emisor debe ser mayor que la frecuencia de muestreo para
  que la envolvente sea estable, pero no tan alta que el op-amp o el detector
  no la sigan.

### 6.4 Sincronización

- El reloj sigue siendo externo y source-synchronous, igual que en el enlace
  óptico. El receptor acústico espera el flanco de subida y muestrea después
  de `settle_ms`.
- El emisor piezo genera la ráfaga durante la fase alta del reloj; el receptor
  solo escucha en esa ventana.
- Si se comparten los mismos GPIO de reloj, el enlace óptico y el acústico no
  pueden recibir tramas distintas al mismo tiempo con un solo `rx_channel`.
  Para eso haría falta un canal compuesto que muestree ambos ADC y elija el de
  mayor calidad por frame. La implementación actual selecciona un medio por
  arranque, que es suficiente para validar el segundo medio sin romper el
  primero.
- La deriva de reloj entre equipos no se corrige por software: se mantiene el
  esquema de dos pulsos por bit y la resincronización por ventana móvil de 44
  bits de `phy_rx`.
- Para mayor determinismo, evaluar PRU-ICSS; no es necesario para la primera
  validación.

### 6.5 Problemas esperados y ajustes

| Problema | Ajuste |
|---|---|
| AIN1 siempre en 0 o saturado | Revisar bias, alimentación del op-amp y protección |
| Bits invertidos | Revisar polaridad del detector de envolvente |
| Lecturas inestables | Aumentar `adc_samples`, bajar `clock_hz`, mejorar blindaje |
| No detecta ráfagas | Subir ganancia, acercar el piezo o bajar `piezo_on_level` |
| Detecta ruido ambiente | Subir `piezo_on_level` y `piezo_off_level` |
| Falla el CRC | Reducir `clock_hz`, revisar sincronización y ventana de muestreo |
| El enlace óptico deja de funcionar | Verificar que se arrancó con `--rx-medium light --tx-medium light` |

---

## 7. Compatibilidad

- C99, sin dependencias externas.
- Sin cambios en `phy_protocol.h`, `frame.c`, `crc8.c`, `phy_rx.c` ni
  `phy_node.c`.
- El gateway Python no se modifica; sigue siendo compatible con Python 3.5.3.
- Debian GNU/Linux 9 Stretch: solo se usan POSIX, sysfs, IIO y GPIO.
- El enlace óptico conserva sus pines, umbrales y comportamiento por defecto.
