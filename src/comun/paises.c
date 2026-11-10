#include "paises.h"
#include <stddef.h>
#include <string.h>

/* Nombres de los 5 sectores economicos en orden de ID_Sector (0 a 4). */
const char *NOMBRE_SECTOR[NUM_SECTORES] = {
    "Industria", "Servicios", "Consumidores", "Comercio", "Construccion"};

/* Pesos de cada sector para la media ponderada del ISE: Industria 40%,
 * Servicios 30%, Consumidores 20%, Comercio 5% y Construccion 5%. */
const double PESO_SECTOR[NUM_SECTORES] = {0.40, 0.30, 0.20, 0.05, 0.05};

/* Tabla 1 del enunciado con los datos fijos de cada pais:
 * sigla, id, max_encuestas y maximos por sector (Industria a Construccion). */
static const struct pais_info PAISES[] = {
    {"ES", 0, 6580, {1760, 1430, 2020, 1080, 290}},
    {"FR", 1, 36640, {3850, 4240, 1670, 2860, 24020}},
    {"IT", 2, 13100, {5440, 3100, 2000, 1560, 1000}}};

/* Cantidad de filas de la tabla PAISES: el tamano total del arreglo en bytes
 * dividido entre el tamano de una fila. Asi no hay que cambiar un numero a
 * mano si se agrega otro pais. */
#define NUM_PAISES ((int)(sizeof(PAISES) / sizeof(PAISES[0])))

/*
 * buscar_pais:
 * Busca un pais en la tabla PAISES a partir de su sigla de texto.
 *
 * Recibe:
 * - const char *sigla: cadena de texto con la sigla del pais ("ES", "FR" o
 *   "IT").
 *
 * Variables creadas:
 * - int i: indice para recorrer las filas de la tabla PAISES.
 *
 * Paso a paso:
 * 1. Primero se revisa si sigla es NULL; si lo es, retorna NULL.
 * 2. Despues se recorre la tabla PAISES con un ciclo for comparando cada
 *    PAISES[i].sigla con sigla usando strcmp.
 * 3. Si encuentra coincidencia (strcmp == 0), retorna la direccion &PAISES[i].
 *
 * Retorna:
 * - Apuntador constante a la fila del pais en la tabla, o NULL si no existe.
 */
const struct pais_info *buscar_pais(const char *sigla) {
  int i;

  if (sigla == NULL) {
    return NULL;
  }
  for (i = 0; i < NUM_PAISES; i++) {
    if (strcmp(PAISES[i].sigla, sigla) == 0) {
      return &PAISES[i];
    }
  }
  return NULL;
}

/*
 * pais_por_id:
 * Busca un pais en la tabla PAISES a partir de su identificador numerico.
 *
 * Recibe:
 * - int id: numero entero de ID_Pais (0 para ES, 1 para FR, 2 para IT).
 *
 * Variables creadas:
 * - int i: indice para recorrer las filas de la tabla PAISES.
 *
 * Paso a paso:
 * 1. Primero recorre la tabla PAISES con un ciclo for.
 * 2. En cada fila revisa si PAISES[i].id == id; si coincide, retorna la
 *    direccion &PAISES[i].
 *
 * Retorna:
 * - Apuntador constante a la fila del pais en la tabla, o NULL si no existe.
 */
const struct pais_info *pais_por_id(int id) {
  int i;

  for (i = 0; i < NUM_PAISES; i++) {
    if (PAISES[i].id == id) {
      return &PAISES[i];
    }
  }
  return NULL;
}
