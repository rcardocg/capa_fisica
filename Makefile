MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TOOL := $(MAKEFILE_DIR)tools/terminal_chat.py

PYTHON ?= python3
ENCODING ?= utf-8
CRC ?= smbus
LANES ?= 3
HOST ?= 127.0.0.1
BIND ?= 127.0.0.1
PORT ?= 5000
TIMEOUT ?= 1.0
RETRIES ?= 3
QUIET ?=
CORRUPT_FRAME ?=
CORRUPT_BIT ?=

QUIET_ARG := $(if $(strip $(QUIET)),--quiet,)
CORRUPT_ARG := $(if $(strip $(CORRUPT_FRAME)),--corrupt-frame $(CORRUPT_FRAME),)
CORRUPT_BIT_ARG := $(if $(strip $(CORRUPT_BIT)),--corrupt-bit $(CORRUPT_BIT),)
COMMON := --encoding $(ENCODING) --crc $(CRC) --lanes $(LANES)

.PHONY: emisor receptor prueba test ayuda

emisor:
	@$(PYTHON) $(TOOL) connect --host $(HOST) --port $(PORT) $(COMMON) --timeout $(TIMEOUT) --retries $(RETRIES) $(CORRUPT_ARG) $(CORRUPT_BIT_ARG) $(QUIET_ARG)

receptor:
	@$(PYTHON) $(TOOL) serve --host $(BIND) --port $(PORT) $(COMMON) $(QUIET_ARG)

prueba:
	@$(PYTHON) -m unittest discover -s tests -v

test: prueba

ayuda:
	@printf '%s\n' 'Chat interactivo del protocolo PHY'
	@printf '%s\n' ''
	@printf '%s\n' 'Uso en dos terminales:'
	@printf '%s\n' '  Terminal 1: make receptor'
	@printf '%s\n' '  Terminal 2: make emisor'
	@printf '%s\n' ''
	@printf '%s\n' 'En la terminal del emisor escribí un mensaje y presioná Enter.'
	@printf '%s\n' 'El receptor muestra el texto carácter por carácter.'
	@printf '%s\n' 'Ambas terminales muestran SYN, SFD, CONTROL, DATA, CRC, ACK/NACK,'
	@printf '%s\n' 'timeouts y reintentos.'
	@printf '%s\n' ''
	@printf '%s\n' 'Objetivos:'
	@printf '%s\n' '  make emisor                  Inicia el emisor interactivo'
	@printf '%s\n' '  make receptor                Inicia el receptor interactivo'
	@printf '%s\n' '  make prueba                  Ejecuta los tests'
	@printf '%s\n' '  make ayuda                   Muestra esta ayuda'
	@printf '%s\n' ''
	@printf '%s\n' 'Variables:'
	@printf '%s\n' '  PYTHON=$(PYTHON)'
	@printf '%s\n' '  ENCODING=$(ENCODING)'
	@printf '%s\n' '  CRC=$(CRC)'
	@printf '%s\n' '  LANES=$(LANES)'
	@printf '%s\n' '  HOST=$(HOST)'
	@printf '%s\n' '  BIND=$(BIND)'
	@printf '%s\n' '  PORT=$(PORT)'
	@printf '%s\n' '  TIMEOUT=$(TIMEOUT)'
	@printf '%s\n' '  RETRIES=$(RETRIES)'
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
