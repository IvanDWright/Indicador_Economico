#ifndef COMUN_H
#define COMUN_H

/*
 * comun.h:
 * Constantes que comparten los tres programas (servidorISE, agentePais y
 * generadorCSV). Cada valor sale del enunciado, y estan en un solo lugar para
 * que cambiar uno no obligue a buscarlo por todo el codigo.
 */

/* Cantidad maxima de agentes que puede atender el servidor (pag. 3). */
#define MAX_AGENTES            4
/* Sectores economicos: Industria, Servicios, Consumidores, Comercio y
 * Construccion (pag. 1). */
#define NUM_SECTORES           5
/* Bytes que ocupa la sigla de un pais: 2 letras mas el '\0' del final. */
#define TAM_SIGLA              3
/* Tiempo maximo en segundos que el servidor atiende a los agentes (pag. 3). */
#define TIMEOUT_SERVIDOR_SEG   60
/* Cada cuantas encuestas enviadas el agente muestra su progreso (pag. 4). */
#define LOTE_PROGRESO          500
/* Minimo de encuestas que el generador le da a cada sector (pag. 8). */
#define MIN_ENCUESTAS_SECTOR   2
/* Maximo de encuestas por sector, en porcentaje del total del pais (pag. 8). */
#define MAX_PORCENTAJE_SECTOR  30
/* Umbrales de la recomendacion (pag. 2): con ISE mayor a 55 es Vender, con ISE
 * menor a 45 es Comprar, y entre 45 y 55 (incluidos) es Neutro. */
#define UMBRAL_VENDER          55.0
#define UMBRAL_COMPRAR         45.0

/* Identificador de cada sector (columna ID_Sector del CSV). Empieza en 0
 * porque se usa directamente como indice de los arreglos por sector, como
 * suma[], cuenta[], PESO_SECTOR[] y NOMBRE_SECTOR[]. */
enum sector {
    SECTOR_INDUSTRIA = 0,
    SECTOR_SERVICIOS,
    SECTOR_CONSUMIDORES,
    SECTOR_COMERCIO,
    SECTOR_CONSTRUCCION
};

#endif
