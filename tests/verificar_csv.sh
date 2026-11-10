#!/bin/bash
# verificar_csv.sh - Comprueba que un CSV creado por generadorCSV cumple el
# enunciado (pag. 8) y la Tabla 1. No necesita nada compilado: solo awk.
#
# Uso:
#     bash tests/verificar_csv.sh <archivo.csv> [total_esperado]
# total_esperado es el valor de -c con que se genero. Si se omite, se toma el
# numero de filas del archivo (entonces no se detecta que falten filas).
#
# Ejemplo:
#     ./generadorCSV -p FR -c 25 && bash tests/verificar_csv.sh encuestas_FR.csv 25
#
# Lo que comprueba (ver docs/diseno.md, seccion 3.4):
#   - encabezado exacto, 6 campos por fila y fin de linea \n (no \r\n)
#   - ID_Encuesta = 1, 2, 3, ... sin saltos
#   - Pais e ID_Pais iguales en todo el archivo y coherentes (ES=0, FR=1, IT=2)
#   - Sector e ID_Sector coherentes (Industria=0 ... Construccion=4)
#   - filas agrupadas por sector, en el orden 0..4
#   - Sentimiento con dos decimales, entre 0.00 y 100.00
#   - cada sector: minimo 2, maximo techo(30 % del total) y tope de la Tabla 1
#   - la suma de los cinco sectores == total
#
# Los numeros de la Tabla 1 y los ID_Pais se repiten aqui a proposito (es una
# prueba independiente del programa); si cambian en paises.c, cambienlos tambien.

if [ $# -lt 1 ] || [ $# -gt 2 ] || [ ! -r "$1" ]; then
    echo "Uso: bash $0 <archivo.csv> [total_esperado]" >&2
    exit 2
fi

# BINMODE=3 solo afecta a gawk en Windows (que si no, descarta el \r al leer);
# en Linux no hace nada.
LC_ALL=C awk -v BINMODE=3 -v esperado="${2:-}" '
BEGIN {
    FS = ","
    split("Industria Servicios Consumidores Comercio Construccion", nombre, " ")
    idpais["ES"] = 0; idpais["FR"] = 1; idpais["IT"] = 2
    split("1760 1430 2020 1080 290",   m, " "); for (i = 1; i <= 5; i++) tope["ES", i - 1] = m[i]
    split("3850 4240 1670 2860 24020", m, " "); for (i = 1; i <= 5; i++) tope["FR", i - 1] = m[i]
    split("5440 3100 2000 1560 1000",  m, " "); for (i = 1; i <= 5; i++) tope["IT", i - 1] = m[i]
    fallos = 0; filas = 0; ultimo = 0; pais = ""; crlf = 0
}
function falla(msg)   { printf "FALLO (linea %d): %s\n", NR, msg; fallos++ }
function falla_g(msg) { printf "FALLO: %s\n", msg; fallos++ }

NR == 1 {
    if ($0 ~ /\r$/ && !crlf) { crlf = 1; falla("el archivo usa \\r\\n, debe ser solo \\n") }
    sub(/\r$/, "")
    if ($0 != "ID_Encuesta,ID_Pais,Pais,Sector,ID_Sector,Sentimiento")
        falla("encabezado incorrecto")
    next
}
{
    if ($0 ~ /\r$/ && !crlf) { crlf = 1; falla("el archivo usa \\r\\n, debe ser solo \\n") }
    sub(/\r$/, ""); $0 = $0          # reanaliza los campos sin el \r
    filas++
    if (NF != 6) { falla("se esperaban 6 campos y hay " NF); next }
    if ($1 !~ /^[0-9]+$/ || $1 + 0 != filas) falla("ID_Encuesta " $1 " (esperado " filas ")")
    if (pais == "") {
        pais = $3; idp = $2
        if (!(pais in idpais))      falla("pais desconocido: " pais)
        else if (idpais[pais] != $2) falla("ID_Pais " $2 " no corresponde a " pais)
    } else if ($3 != pais || $2 != idp) {
        falla("Pais/ID_Pais cambian dentro del archivo")
    }
    s = $5
    if (s !~ /^[0-4]$/) { falla("ID_Sector fuera de 0..4: " s); next }
    s = s + 0
    if ($4 != nombre[s + 1]) falla("Sector " $4 " no coincide con ID_Sector " s)
    if (s < ultimo) falla("filas no agrupadas por sector (ID_Sector " s " despues de " ultimo ")")
    ultimo = s
    cuenta[s]++
    if ($6 !~ /^[0-9]+\.[0-9][0-9]$/) falla("Sentimiento sin formato 0.00: " $6)
    else if ($6 + 0 > 100)            falla("Sentimiento mayor que 100: " $6)
}
END {
    if (filas == 0) falla_g("el archivo no tiene encuestas")
    total = (esperado != "") ? esperado + 0 : filas
    if (esperado != "" && filas != total)
        falla_g("hay " filas " filas y se esperaban " total)
    tope30 = int((total * 30 + 99) / 100)          # techo del 30 %
    suma = 0
    for (s = 0; s < 5; s++) {
        c = cuenta[s] + 0
        suma += c
        if (c < 2)       falla_g(nombre[s + 1] " tiene " c " encuestas (minimo 2)")
        if (c > tope30)  falla_g(nombre[s + 1] " tiene " c " encuestas (maximo 30 % = " tope30 ")")
        if ((pais, s) in tope && c > tope[pais, s])
            falla_g(nombre[s + 1] " tiene " c " encuestas (Tabla 1 de " pais ": maximo " tope[pais, s] ")")
    }
    if (suma != total) falla_g("los sectores suman " suma " y el total es " total)
    if (fallos > 0) { printf "%d fallo(s)\n", fallos; exit 1 }
    printf "OK: %s, %d encuestas (", pais, filas
    for (s = 0; s < 5; s++) printf "%s%s=%d", (s ? ", " : ""), nombre[s + 1], cuenta[s]
    printf "; tope 30 %% = %d)\n", tope30
    exit 0
}
' "$1"
