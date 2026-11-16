CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -Iinclude
BIN = phy_chat
NODE_BIN = phy_node
SIM_BIN = phy_sim
TEST_BIN = test_protocol
TEST_RX_BIN = test_rx
TEST_NODE_BIN = test_node
LIGHT_BIN = light_probe
FLIPFLOP_BIN = flipflopT
RX_TEST_BIN = light_rx_test
TX_BIN = phy_light_tx

CORE_SOURCES = src/crc8.c src/frame.c src/presentation.c
CHAT_SOURCES = $(CORE_SOURCES) src/net.c src/chat.c
CHANNEL_SOURCES = src/phy_gpio.c src/phy_analog.c src/phy_sample.c
TX_SOURCES = $(CORE_SOURCES) src/net.c src/phy_channel.c src/phy_tcp_channel.c src/phy_tx.c tools/phy_light_tx.c
NODE_CORE_SOURCES = $(CORE_SOURCES) src/net.c src/phy_channel.c src/phy_rx_channel.c src/phy_rx.c src/phy_sample.c src/phy_node.c
NODE_TOOL_SOURCES = $(NODE_CORE_SOURCES) src/phy_gpio.c src/phy_analog.c src/phy_gpio_tx_channel.c src/phy_light_rx_channel.c tools/phy_node.c
NODE_TEST_SOURCES = $(NODE_CORE_SOURCES) src/phy_fake_link.c tests/test_node.c
SIM_SOURCES = $(NODE_CORE_SOURCES) src/phy_fake_link.c tools/phy_sim.c

HOST ?= 127.0.0.1
BIND ?= 127.0.0.1
PORT ?= 5000
BEAGLE_PORT ?= 7000
NODE_ROLE ?= A
NODE_BIND ?= 0.0.0.0
NODE_PORT ?= 7000
NODE_ARGS ?=
MSG ?=
ENCODING ?= utf-8
CRC ?= smbus
LANES ?= 3
TIMEOUT ?= 1.0
RETRIES ?= 3
SEQ_START ?= 0
CORRUPT_FRAME ?= -1
CORRUPT_BIT ?= 43
QUIET ?=
QUIET_FLAG = $(if $(QUIET),--quiet,)

.PHONY: all emisor receptor test test-rx test-node sim node probe-luz flipflop rx-luz tx-luz clean ayuda

all: $(BIN) $(NODE_BIN)

$(BIN): $(CHAT_SOURCES) include/phy_protocol.h include/phy_net.h
	$(CC) $(CFLAGS) -o $@ $(CHAT_SOURCES)

$(NODE_BIN): $(NODE_TOOL_SOURCES) include/phy_node.h include/phy_gpio_tx_channel.h include/phy_light_rx_channel.h include/phy_rx.h include/phy_rx_channel.h include/phy_gpio.h include/phy_analog.h include/phy_protocol.h
	$(CC) $(CFLAGS) -pthread -o $@ $(NODE_TOOL_SOURCES)

$(TEST_NODE_BIN): $(NODE_TEST_SOURCES) include/phy_fake_link.h include/phy_node.h include/phy_net.h include/phy_protocol.h
	$(CC) $(CFLAGS) -pthread -o $@ $(NODE_TEST_SOURCES)

$(SIM_BIN): $(SIM_SOURCES) include/phy_fake_link.h include/phy_node.h include/phy_net.h include/phy_protocol.h
	$(CC) $(CFLAGS) -pthread -o $@ $(SIM_SOURCES)

sim: $(SIM_BIN)

$(TEST_BIN): tests/test_protocol.c $(CORE_SOURCES) include/phy_protocol.h
	$(CC) $(CFLAGS) -o $@ tests/test_protocol.c $(CORE_SOURCES)

RX_SOURCES = $(CORE_SOURCES) src/phy_rx_channel.c src/phy_fake_rx_channel.c src/phy_rx.c src/phy_sample.c

$(TEST_RX_BIN): tests/test_rx.c $(RX_SOURCES) include/phy_rx.h include/phy_rx_channel.h include/phy_fake_rx_channel.h include/phy_sample.h include/phy_protocol.h
	$(CC) $(CFLAGS) -o $@ tests/test_rx.c $(RX_SOURCES)

$(LIGHT_BIN): tools/light_probe.c
	$(CC) $(CFLAGS) -o $@ tools/light_probe.c

$(FLIPFLOP_BIN): tools/flipflopT.c
	$(CC) $(CFLAGS) -o $@ tools/flipflopT.c

$(RX_TEST_BIN): tools/light_rx_test.c $(CHANNEL_SOURCES) include/phy_gpio.h include/phy_analog.h include/phy_sample.h
	$(CC) $(CFLAGS) -o $@ tools/light_rx_test.c $(CHANNEL_SOURCES)

$(TX_BIN): $(TX_SOURCES) include/phy_channel.h include/phy_tcp_channel.h include/phy_tx.h include/phy_net.h include/phy_protocol.h
	$(CC) $(CFLAGS) -o $@ $(TX_SOURCES)

probe-luz: $(LIGHT_BIN)
	@./$(LIGHT_BIN) $(PROBE_ARGS)

flipflop: $(FLIPFLOP_BIN)
	@./$(FLIPFLOP_BIN) $(PROBE_ARGS)

rx-luz: $(RX_TEST_BIN)
	@./$(RX_TEST_BIN) $(PROBE_ARGS)

tx-luz: $(TX_BIN)
	@./$(TX_BIN) --host $(HOST) --port $(BEAGLE_PORT) --encoding $(ENCODING) \
		--crc $(CRC) --seq-start $(SEQ_START) $(if $(MSG),--message "$(MSG)",)

node: $(NODE_BIN)
	@./$(NODE_BIN) --role $(NODE_ROLE) --bind $(NODE_BIND) --port $(NODE_PORT) \
		--encoding $(ENCODING) --crc $(CRC) $(NODE_ARGS)

emisor: $(BIN)
	@./$(BIN) connect --host $(HOST) --port $(PORT) --encoding $(ENCODING) \
		--crc $(CRC) --lanes $(LANES) --timeout $(TIMEOUT) --retries $(RETRIES) \
		--seq-start $(SEQ_START) --corrupt-frame $(CORRUPT_FRAME) \
		--corrupt-bit $(CORRUPT_BIT) $(QUIET_FLAG)

receptor: $(BIN)
	@./$(BIN) serve --bind $(BIND) --port $(PORT) --encoding $(ENCODING) \
		--crc $(CRC) --lanes $(LANES) --timeout $(TIMEOUT) --retries $(RETRIES) \
		--seq-start $(SEQ_START) $(QUIET_FLAG)

test: $(TEST_BIN) $(TEST_RX_BIN) $(TEST_NODE_BIN)
	@./$(TEST_BIN)
	@./$(TEST_RX_BIN)
	@./$(TEST_NODE_BIN)

test-rx: $(TEST_RX_BIN)
	@./$(TEST_RX_BIN)

test-node: $(TEST_NODE_BIN)
	@./$(TEST_NODE_BIN)

test-integral: $(SIM_BIN)
	@python3 ../scripts/test_fase5.py --sim ./$(SIM_BIN)

clean:
	rm -f $(BIN) $(NODE_BIN) $(SIM_BIN) $(TEST_BIN) $(TEST_RX_BIN) $(TEST_NODE_BIN) \
		$(LIGHT_BIN) $(FLIPFLOP_BIN) $(RX_TEST_BIN) $(TX_BIN)

ayuda:
	@printf '%s\n' \
		'Objetivos:' \
		'  make                  compila phy_chat y phy_node' \
		'  make emisor           emisor interactivo (HOST, PORT, ...)' \
		'  make receptor         receptor interactivo (BIND, PORT, ...)' \
		'  make test             pruebas de protocolo, recepcion y nodo' \
		'  make test-rx          pruebas de phy_rx con canal falso' \
		'  make test-node        prueba A/B con enlace falso y API TCP' \
		'  make sim              levanta A/B simulados con API TCP' \
		'  make test-integral    prueba integral de la fase 5' \
		'  make node             inicia phy_node (NODE_ROLE, NODE_PORT, ...)' \
		'  make light_probe      compila la sonda del canal de luz' \
		'  make probe-luz        ejecuta la sonda (PROBE_ARGS=...)' \
		'  make flipflop         demodula el bit con flip-flop (PROBE_ARGS=...)' \
		'  make rx-luz           prueba patrones con clock externo (PROBE_ARGS=...)' \
		'  make tx-luz           envia tramas al LED via la Beagle (MSG=...)' \
		'  make clean' \
		'' \
		'Ejemplo en dos terminales:' \
		'  make receptor' \
		'  make emisor' \
		'' \
		'Variables:' \
		'  HOST=127.0.0.1 BIND=127.0.0.1 PORT=5000 BEAGLE_PORT=7000' \
		'  NODE_ROLE=A NODE_BIND=0.0.0.0 NODE_PORT=7000 NODE_ARGS=...' \
		'  ENCODING=utf-8 CRC=smbus LANES=3 TIMEOUT=1.0 RETRIES=3' \
		'  CORRUPT_FRAME=-1 CORRUPT_BIT=43 QUIET=1 MSG=hola'
