#!/bin/bash
# prueba_ise.sh - Prueba de extremo a extremo con UN agente (Francia).
#
# Caso: data/encuestas_FR_ejemplo.csv tiene un solo dato por sector, con los
# promedios del enunciado (40.25, 60.70, 58.44, 52.13, 30.12). El ISE correcto
# es 50.1105 y la recomendacion Neutro.
#
# Ejecutar desde la raiz del repositorio, dentro de Linux/WSL:
#     make && bash tests/prueba_ise.sh
#
# AJUSTEN el grep de abajo al formato real de su reporte.

set -u

PIPE=/tmp/ise_fifo_prueba
CSV=data/encuestas_FR_ejemplo.csv
SALIDA=$(mktemp)

for exe in ./servidorISE ./agentePais; do
    [ -x "$exe" ] || { echo "Falta $exe: ejecute make primero"; exit 2; }
done

# 1. El servidor se levanta PRIMERO (crea el FIFO) y en segundo plano.
./servidorISE -b 4 -p "$PIPE" -a 1 > "$SALIDA" 2>&1 &
SERVIDOR=$!

# 2. Esperar a que el FIFO exista (maximo ~5 s). No es un sleep fijo: se
#    comprueba la condicion real.
for _ in $(seq 1 50); do
    [ -p "$PIPE" ] && break
    sleep 0.1
done
[ -p "$PIPE" ] || { echo "FALLO: el servidor no creo el FIFO"; kill "$SERVIDOR" 2>/dev/null; exit 1; }

# 3. Lanzar el agente y esperar a ambos.
./agentePais -n "$CSV" -p "$PIPE"
wait "$SERVIDOR"
CODIGO=$?

cat "$SALIDA"

# 4. Comprobaciones.
FALLOS=0
[ "$CODIGO" -eq 0 ] || { echo "FALLO: el servidor termino con codigo $CODIGO"; FALLOS=1; }
grep -q "50.11" "$SALIDA" || { echo "FALLO: no aparece el ISE 50.11"; FALLOS=1; }
grep -qi "neutro" "$SALIDA" || { echo "FALLO: la recomendacion deberia ser Neutro"; FALLOS=1; }
[ ! -e "$PIPE" ] || { echo "FALLO: el servidor no borro el FIFO"; FALLOS=1; }

rm -f "$SALIDA"
if [ "$FALLOS" -eq 0 ]; then echo "OK"; else exit 1; fi
