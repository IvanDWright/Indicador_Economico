#ifndef ISE_H
#define ISE_H

#include "comun.h"

/*
 * ise.h:
 * Calculo del Indicador de Sentimiento Economico. Este modulo solo hace
 * cuentas: no lee archivos, no imprime y no usa hilos ni semaforos, por eso se
 * puede probar por separado.
 */

/* Lo que se va acumulando de un pais mientras llegan sus encuestas. Se guardan
 * la suma y la cuenta por separado para dividir una sola vez al final. */
struct acum_pais {
    double suma[NUM_SECTORES];   /* suma de los sentimientos de cada sector */
    long   cuenta[NUM_SECTORES]; /* cuantas encuestas llegaron de cada sector */
};

/* Recomendacion de inversion segun el valor del ISE. */
enum recomendacion { REC_VENDER, REC_NEUTRO, REC_COMPRAR };

/* Pone en cero las sumas y las cuentas de un pais. */
void acum_iniciar(struct acum_pais *a);

/* Suma una encuesta a su sector. Retorna 0, o -1 si el sector no existe. */
int acum_agregar(struct acum_pais *a, int id_sector, double sentimiento);

/* Calcula el promedio de cada sector (0.0 si no tuvo encuestas). Retorna
 * cuantos sectores tuvieron al menos una encuesta. */
int promedios_sector(const struct acum_pais *a, double prom[NUM_SECTORES]);

/* ISE de un pais: los promedios por sector ponderados con PESO_SECTOR. */
double ise_pais(const double prom[NUM_SECTORES]);

/* ISE global: promedio simple de los ISE de los n paises (0.0 si n <= 0). */
double ise_global(const double ise_paises[], int n);

/* Clasifica un ISE en Vender (> 55), Comprar (< 45) o Neutro. */
enum recomendacion clasificar(double ise);

/* Texto de una recomendacion: "Vender", "Neutro" o "Comprar". */
const char *nombre_recomendacion(enum recomendacion r);

#endif
