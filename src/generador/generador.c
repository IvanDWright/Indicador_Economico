#include "generador.h"
#include "comun.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * sentimiento_aleatorio:
 * Genera un valor de sentimiento aleatorio entre 0.00 y 100.00 con 2 decimales.
 *
 * Recibe:
 * - Nada (void).
 *
 * Variables creadas:
 * - double x: donde se guarda el numero aleatorio escalado entre 0.0 y 100.0.
 *
 * Paso a paso:
 * 1. Primero se divide rand() entre RAND_MAX para tener un decimal entre 0 y 1
 *    y se multiplica por 100.0.
 * 2. Despues se multiplica por 100.0, se le suma 0.5 y se convierte a int
 *    para redondearlo a dos decimales al volver a dividir entre 100.0.
 *
 * Retorna:
 * - El valor double del sentimiento aleatorio con dos decimales.
 */
static double sentimiento_aleatorio(void) {
  double x = (rand() / (double)RAND_MAX) * 100.0;
  return (int)(x * 100.0 + 0.5) / 100.0;
}

/*
 * repartir_sectores:
 * Reparte la cantidad total de encuestas entre los 5 sectores al azar.
 *
 * Recibe:
 * - long total: cantidad total de encuestas pedidas con la bandera -c.
 * - const struct pais_info *pais: apuntador a la estructura pais_info con los
 *   maximos de la Tabla 1.
 * - long cuentas[NUM_SECTORES]: arreglo de 5 posiciones donde se guardara
 *   cuantas encuestas tendra cada sector.
 *
 * Variables creadas:
 * - int i: indice para recorrer los 5 sectores en los ciclos for.
 * - long total_actual: lleva la suma de cuantas encuestas ya se han repartido.
 * - long max_porcentaje: guarda el tope del 30% del total redondeado arriba.
 * - long topes[NUM_SECTORES]: guarda el limite maximo real de cada sector.
 * - long suma_topes: acumula la suma de los topes de los 5 sectores.
 * - long lim: guarda temporalmente el maximo del sector segun la Tabla 1.
 * - int sector: guarda el sector elegido al azar con rand() % NUM_SECTORES.
 *
 * Paso a paso:
 * 1. Primero se revisa que pais y cuentas no sean NULL, y que total sea al
 *    menos 10 (minimo 2 por sector en 5 sectores) y no supere max_encuestas.
 * 2. Se calcula el 30% del total redondeado hacia arriba (+ 99 / 100).
 * 3. En un ciclo for se calcula el tope de cada sector como el menor entre el
 *    30% y el maximo de la Tabla 1, sumandolos en suma_topes.
 * 4. Se revisa que total no sea mayor que suma_topes (si es mayor, no cabe).
 * 5. Despues se asigna el minimo obligatorio (2) a cada sector en cuentas[] y
 *    se inicia total_actual en 10.
 * 6. En un ciclo while se eligen sectores al azar y, si el sector aun no
 *    llega a su tope, se le suma 1 encuesta hasta que total_actual == total.
 *
 * Retorna:
 * - 0 si se logro repartir con exito, o -1 si el reparto es imposible.
 */
int repartir_sectores(long total, const struct pais_info *pais,
                      long cuentas[NUM_SECTORES]) {
  int i;
  long total_actual;
  long max_porcentaje;
  long topes[NUM_SECTORES];
  long suma_topes = 0;

  if (pais == NULL || cuentas == NULL) {
    return -1;
  }

  if (total < NUM_SECTORES * MIN_ENCUESTAS_SECTOR ||
      total > pais->max_encuestas) {
    return -1;
  }

  max_porcentaje = (total * MAX_PORCENTAJE_SECTOR + 99) / 100;

  for (i = 0; i < NUM_SECTORES; i++) {
    long lim = pais->max_sector[i];
    topes[i] = (max_porcentaje < lim) ? max_porcentaje : lim;
    if (topes[i] < MIN_ENCUESTAS_SECTOR) {
      return -1;
    }
    suma_topes += topes[i];
  }

  if (total > suma_topes) {
    return -1;
  }

  for (i = 0; i < NUM_SECTORES; i++) {
    cuentas[i] = MIN_ENCUESTAS_SECTOR;
  }
  total_actual = NUM_SECTORES * MIN_ENCUESTAS_SECTOR;

  while (total_actual < total) {
    int sector = rand() % NUM_SECTORES;
    if (cuentas[sector] < topes[sector]) {
      cuentas[sector]++;
      total_actual++;
    }
  }

  return 0;
}

/*
 * escribir_csv:
 * Crea y escribe el archivo CSV con el encabezado y todas las encuestas.
 *
 * Recibe:
 * - const char *ruta: nombre del archivo CSV a crear (ej: "encuestas_FR.csv").
 * - const struct pais_info *pais: apuntador a la estructura con los datos del
 *   pais (id y sigla).
 * - const long cuentas[NUM_SECTORES]: arreglo con la cantidad de encuestas que
 *   lleva cada sector.
 *
 * Variables creadas:
 * - FILE *f: donde se va a poner el archivo abierto en modo escritura.
 * - int s: indice para recorrer los 5 sectores (de 0 a 4).
 * - long j: contador para escribir las cuentas[s] encuestas de cada sector.
 * - long id: numero correlativo de encuesta (empieza en 1 y sube de a 1).
 *
 * Paso a paso:
 * 1. Primero se revisa que ruta, pais y cuentas no sean NULL, y se abre el
 *    archivo con fopen(ruta, "w"). Si falla, informa con perror y retorna -1.
 * 2. Se escribe la primera linea con el encabezado exacto usando fprintf.
 * 3. Despues se recorren dos ciclos for: el exterior por cada sector s (0..4)
 *    y el interior por cada encuesta j de ese sector, escribiendo cada fila
 *    con formato "%ld,%d,%s,%s,%d,%.2f\n" (%ld: id de encuesta, %d: id del
 *    pais, %s: sigla del pais, %s: nombre del sector, %d: id del sector,
 *    %.2f: sentimiento aleatorio con 2 decimales) e incrementando id++.
 * 4. Al final se cierra el archivo con fclose(f) revisando que no haya error.
 *
 * Retorna:
 * - 0 si el archivo se escribio y cerro correctamente, o -1 si hubo error.
 */
int escribir_csv(const char *ruta, const struct pais_info *pais,
                 const long cuentas[NUM_SECTORES]) {
  FILE *f;
  int s;
  long j;
  long id = 1;

  if (ruta == NULL || pais == NULL || cuentas == NULL) {
    return -1;
  }

  f = fopen(ruta, "w");
  if (f == NULL) {
    perror(ruta);
    return -1;
  }

  if (fprintf(f, "ID_Encuesta,ID_Pais,Pais,Sector,ID_Sector,Sentimiento\n") <
      0) {
    perror(ruta);
    fclose(f);
    return -1;
  }

  for (s = 0; s < NUM_SECTORES; s++) {
    for (j = 0; j < cuentas[s]; j++) {
      if (fprintf(f, "%ld,%d,%s,%s,%d,%.2f\n", id, pais->id, pais->sigla,
                  NOMBRE_SECTOR[s], s, sentimiento_aleatorio()) < 0) {
        perror(ruta);
        fclose(f);
        return -1;
      }
      id++;
    }
  }

  if (fclose(f) != 0) {
    perror(ruta);
    return -1;
  }

  return 0;
}
