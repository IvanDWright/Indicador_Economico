CC      = gcc
CFLAGS  = -std=c89 -pedantic -Wall -Wextra -MMD -MP -D_POSIX_C_SOURCE=200809L -Iinclude
LDFLAGS = -pthread

SRC_COMUN     = $(wildcard src/comun/*.c)
SRC_SERVIDOR  = $(wildcard src/servidor/*.c)
SRC_AGENTE    = $(wildcard src/agente/*.c)
SRC_GENERADOR = $(wildcard src/generador/*.c)

OBJ_COMUN     = $(SRC_COMUN:.c=.o)
OBJ_SERVIDOR  = $(SRC_SERVIDOR:.c=.o)
OBJ_AGENTE    = $(SRC_AGENTE:.c=.o)
OBJ_GENERADOR = $(SRC_GENERADOR:.c=.o)

BINARIOS = servidorISE agentePais generadorCSV

.PHONY: all clean

all: $(BINARIOS)

servidorISE: $(OBJ_SERVIDOR) $(OBJ_COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

agentePais: $(OBJ_AGENTE) $(OBJ_COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

generadorCSV: $(OBJ_GENERADOR) $(OBJ_COMUN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# -MMD genera un .d por cada .o con sus cabeceras: si cambia un .h, make
# recompila solo lo afectado.
-include $(wildcard src/*/*.d)

clean:
	rm -f $(BINARIOS) src/*/*.o src/*/*.d
