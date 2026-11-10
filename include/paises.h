#ifndef PAISES_H
#define PAISES_H

#include "comun.h"

/*
 * paises.h:
 * Datos fijos del enunciado: los paises con sus maximos de la Tabla 1, y los
 * nombres y pesos de los 5 sectores.
 */

/* Una fila de la Tabla 1. */
struct pais_info {
    const char *sigla;                    /* "ES", "FR" o "IT" */
    int         id;                       /* columna ID_Pais del CSV */
    int         max_encuestas;            /* maximo de encuestas del pais */
    int         max_sector[NUM_SECTORES]; /* maximo de cada sector */
};

/* Nombre y peso de cada sector, en el orden de ID_Sector (0 a 4). */
extern const char  *NOMBRE_SECTOR[NUM_SECTORES];
extern const double PESO_SECTOR[NUM_SECTORES];

/* Buscan un pais por su sigla o por su id. Retornan un apuntador a la fila de
 * la tabla, o NULL si no existe. El apuntador es de la tabla interna: no se
 * libera ni se modifica. */
const struct pais_info *buscar_pais(const char *sigla);
const struct pais_info *pais_por_id(int id);

#endif
