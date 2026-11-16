MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
C_DIR := $(MAKEFILE_DIR)c

ENCODING ?= utf-8
CRC ?= smbus
LANES ?= 3
HOST ?= 127.0.0.1
BIND ?= 127.0.0.1
PORT ?= 5000
BEAGLE_PORT ?= 7000
NODE_ROLE ?= A
NODE_BIND ?= 0.0.0.0
NODE_PORT ?= 7000
NODE_ARGS ?=
MSG ?=
TIMEOUT ?= 1.0
RETRIES ?= 3
SEQ_START ?= 0
CORRUPT_FRAME ?= -1
CORRUPT_BIT ?= 43
QUIET ?=
PROBE_ARGS ?=
SUDO ?=

C_MAKE := $(MAKE) --no-print-directory -C $(C_DIR)

.PHONY: all c emisor receptor nodo prueba test test-nodo prueba-integral test-integral probe-luz rx-luz tx-luz clean ayuda

all: c

c:
	@$(C_MAKE) all

emisor:
	@$(C_MAKE) emisor HOST=$(HOST) PORT=$(PORT) ENCODING=$(ENCODING) \
		CRC=$(CRC) LANES=$(LANES) TIMEOUT=$(TIMEOUT) RETRIES=$(RETRIES) \
		SEQ_START=$(SEQ_START) CORRUPT_FRAME=$(CORRUPT_FRAME) \
		CORRUPT_BIT=$(CORRUPT_BIT) QUIET=$(QUIET)

receptor:
	@$(C_MAKE) receptor BIND=$(BIND) PORT=$(PORT) ENCODING=$(ENCODING) \
		CRC=$(CRC) LANES=$(LANES) TIMEOUT=$(TIMEOUT) RETRIES=$(RETRIES) \
		SEQ_START=$(SEQ_START) QUIET=$(QUIET)

prueba:
	@$(C_MAKE) test

test: prueba

nodo:
	@$(C_MAKE) node NODE_ROLE=$(NODE_ROLE) NODE_PORT=$(NODE_PORT) \
		NODE_BIND=$(NODE_BIND) ENCODING=$(ENCODING) CRC=$(CRC) NODE_ARGS='$(NODE_ARGS)'

test-nodo:
	@$(C_MAKE) test-node

prueba-integral:
	@$(C_MAKE) test-integral

test-integral: prueba-integral

probe-luz:
	@$(C_MAKE) light_probe
	@$(SUDO) $(C_DIR)/light_probe $(PROBE_ARGS)

rx-luz:
	@$(C_MAKE) light_rx_test
	@$(SUDO) $(C_DIR)/light_rx_test $(PROBE_ARGS)

tx-luz:
	@$(C_MAKE) tx-luz HOST=$(HOST) BEAGLE_PORT=$(BEAGLE_PORT) \
		ENCODING=$(ENCODING) CRC=$(CRC) SEQ_START=$(SEQ_START) MSG='$(MSG)'

clean:
	@$(C_MAKE) clean

ayuda:
	@printf '%s\n' 'Chat interactivo del protocolo PHY (C99)'
	@printf '%s\n' ''
	@printf '%s\n' 'Uso en dos terminales:'
	@printf '%s\n' '  Terminal 1: make receptor'
	@printf '%s\n' '  Terminal 2: make emisor'
	@printf '%s\n' ''
	@printf '%s\n' 'En la terminal del emisor escribí un mensaje y presioná Enter.'
	@printf '%s\n' 'El receptor muestra el texto y reconstruye el mensaje completo.'
	@printf '%s\n' 'Ambas terminales muestran SYN, SFD, CONTROL, DATA, CRC, ACK/NACK,'
	@printf '%s\n' 'timeouts y reintentos.'
	@printf '%s\n' ''
	@printf '%s\n' 'Objetivos:'
	@printf '%s\n' '  make emisor                  Inicia el emisor interactivo'
	@printf '%s\n' '  make receptor                Inicia el receptor interactivo'
	@printf '%s\n' '  make prueba                  Ejecuta los tests C'
	@printf '%s\n' '  make nodo                    Inicia phy_node (NODE_ROLE=A|B)'
	@printf '%s\n' '  make test-nodo               Prueba A/B con enlace falso'
	@printf '%s\n' '  make prueba-integral         Prueba integral de la fase 5'
	@printf '%s\n' '  make probe-luz               Prueba el canal de luz (ADC P9_39)'
	@printf '%s\n' '  make probe-luz SUDO=sudo     Igual, con permisos de GPIO/ADC'
	@printf '%s\n' '  make probe-luz PROBE_ARGS="--led --blink 0.5"'
	@printf '%s\n' '  make rx-luz                  Prueba patrones de luz con clock gpio44'
	@printf '%s\n' '  make rx-luz SUDO=sudo PROBE_ARGS="--pattern 1110 --clock-hz 2"'
	@printf '%s\n' '  make tx-luz MSG=hola         Envia tramas al LED via la Beagle'
	@printf '%s\n' '  make clean                   Borra los binarios'
	@printf '%s\n' '  make ayuda                   Muestra esta ayuda'
	@printf '%s\n' ''
	@printf '%s\n' 'Variables:'
	@printf '%s\n' '  ENCODING=$(ENCODING)'
	@printf '%s\n' '  CRC=$(CRC)'
	@printf '%s\n' '  LANES=$(LANES)'
	@printf '%s\n' '  HOST=$(HOST)'
	@printf '%s\n' '  BIND=$(BIND)'
	@printf '%s\n' '  PORT=$(PORT)'
	@printf '%s\n' '  BEAGLE_PORT=$(BEAGLE_PORT)'
	@printf '%s\n' '  NODE_ROLE=$(NODE_ROLE)'
	@printf '%s\n' '  NODE_BIND=$(NODE_BIND)'
	@printf '%s\n' '  NODE_PORT=$(NODE_PORT)'
	@printf '%s\n' '  NODE_ARGS=$(NODE_ARGS)'
	@printf '%s\n' '  MSG=$(MSG)'
	@printf '%s\n' '  TIMEOUT=$(TIMEOUT)'
	@printf '%s\n' '  RETRIES=$(RETRIES)'
	@printf '%s\n' '  SEQ_START=$(SEQ_START)'
	@printf '%s\n' '  QUIET=$(QUIET)'
	@printf '%s\n' '  CORRUPT_FRAME=$(CORRUPT_FRAME)'
	@printf '%s\n' '  CORRUPT_BIT=$(CORRUPT_BIT)'
	@printf '%s\n' ''
	@printf '%s\n' 'Ejemplos:'
	@printf '%s\n' '  make emisor CRC=autosar LANES=3'
	@printf '%s\n' '  make emisor CORRUPT_FRAME=1'
	@printf '%s\n' '  make receptor PORT=5001'
	@printf '%s\n' '  make emisor PORT=5001'
	@printf '%s\n' '  make receptor BIND=0.0.0.0'
	@printf '%s\n' '  make emisor HOST=192.168.1.50'
	@printf '%s\n' '  make receptor ENCODING=latin-1'
	@printf '%s\n' '  make emisor ENCODING=latin-1'
	@printf '%s\n' '  make tx-luz MSG="hola" CRC=autosar'
	@printf '%s\n' '  make tx-luz MSG="mañana" ENCODING=latin-1'
