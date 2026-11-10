#ifndef LECTOR_CSV_H
#define LECTOR_CSV_H

#include "comun.h"

/*
 * lector_csv.h:
 * Lectura del archivo de encuestas del agente. Deja todas las encuestas en un
 * arreglo en memoria para enviarlas despues por el pipe nominal.
 */

/* Una encuesta leida del CSV. La sigla y el ID_Pais no se guardan aqui porque
 * son los mismos en todas las filas del archivo. */
struct encuesta {
    int    id_encuesta; /* columna ID_Encuesta */
    int    id_sector;   /* columna ID_Sector, de 0 a 4 */
    double sentimiento; /* columna Sentimiento, de 0 a 100 */
};

/* Lee el CSV completo y reserva el arreglo con malloc. Deja el arreglo en
 * *encuestas, la cantidad en *n, y la sigla y el id del pais en sigla e
 * id_pais. Retorna 0, o -1 si el archivo no existe o tiene algun dato
 * invalido (en ese caso *encuestas queda en NULL). */
int leer_csv(const char *ruta, struct encuesta **encuestas, long *n,
             char sigla[TAM_SIGLA], int *id_pais);

/* Libera el arreglo que reservo leer_csv. */
void liberar_encuestas(struct encuesta *encuestas);

#endif
