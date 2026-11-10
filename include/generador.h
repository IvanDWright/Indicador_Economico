#ifndef GENERADOR_H
#define GENERADOR_H

#include "paises.h"

/*
 * generador.h:
 * Las dos partes del generadorCSV: decidir cuantas encuestas lleva cada sector
 * y escribir el archivo.
 */

/* Reparte total encuestas entre los 5 sectores al azar: minimo 2 por sector,
 * maximo el 30% del total y sin pasar los maximos de la Tabla 1. Deja el
 * resultado en cuentas. Retorna 0, o -1 si no hay reparto posible. */
int repartir_sectores(long total, const struct pais_info *pais,
                      long cuentas[NUM_SECTORES]);

/* Crea el archivo CSV con el encabezado y cuentas[s] encuestas de cada sector
 * s, con sentimientos al azar. Retorna 0, o -1 si no se pudo escribir. */
int escribir_csv(const char *ruta, const struct pais_info *pais,
                 const long cuentas[NUM_SECTORES]);

#endif
