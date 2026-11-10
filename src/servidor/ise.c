#include "ise.h"
#include "paises.h"
#include <string.h>

/*
 * acum_iniciar:
 * Inicializa el acumulador de un pais en cero antes de recibir encuestas.
 *
 * Recibe:
 * - struct acum_pais *a: apuntador a la estructura acum_pais que se quiere
 *   limpiar.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Usa memset(a, 0, sizeof *a) donde a es la estructura que se inicializa, 0
 *    son los bits que se insertan y sizeof *a es el tamano total en bytes,
 *    dejando suma[0..4] en 0.0 y cuenta[0..4] en 0.
 *
 * Retorna:
 * - Nada (void).
 */
void acum_iniciar(struct acum_pais *a) { memset(a, 0, sizeof *a); }

/*
 * acum_agregar:
 * Agrega el sentimiento de una encuesta al sector correspondiente de un pais.
 *
 * Recibe:
 * - struct acum_pais *a: apuntador al acumulador del pais.
 * - int id_sector: identificador del sector economico (0 a 4).
 * - double sentimiento: valor numerico de la encuesta recibida.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero se revisa si id_sector es valido (entre 0 y NUM_SECTORES - 1);
 *    si esta fuera de rango, retorna -1 sin modificar nada.
 * 2. Despues se suma sentimiento en a->suma[id_sector] y se incrementa en 1
 *    la cantidad de encuestas en a->cuenta[id_sector].
 *
 * Retorna:
 * - 0 si se agrego correctamente, o -1 si el sector es invalido.
 */
int acum_agregar(struct acum_pais *a, int id_sector, double sentimiento) {
  if (id_sector < 0 || id_sector >= NUM_SECTORES) {
    return -1;
  }
  a->suma[id_sector] += sentimiento;
  a->cuenta[id_sector] += 1;
  return 0;
}

/*
 * promedios_sector:
 * Calcula el promedio de sentimiento de cada uno de los 5 sectores de un pais.
 *
 * Recibe:
 * - const struct acum_pais *a: apuntador al acumulador del pais con las sumas y
 *   cuentas.
 * - double prom[NUM_SECTORES]: arreglo de 5 doubles donde se guardara el
 *   promedio de cada sector.
 *
 * Variables creadas:
 * - int i: indice para recorrer los 5 sectores en el ciclo for.
 * - int con_datos: contador de cuantos sectores tienen al menos 1 encuesta.
 *
 * Paso a paso:
 * 1. Primero se recorren los sectores de 0 a NUM_SECTORES - 1.
 * 2. En cada sector se revisa si a->cuenta[i] > 0: si tiene datos, se divide
 *    a->suma[i] / a->cuenta[i], se guarda en prom[i] y se suma 1 a con_datos.
 * 3. Si el sector no tiene encuestas (cuenta == 0), se pone prom[i] = 0.0
 *    para evitar dividir por cero.
 *
 * Retorna:
 * - La cantidad de sectores que tuvieron datos (de 0 a 5).
 */
int promedios_sector(const struct acum_pais *a, double prom[NUM_SECTORES]) {
  int i;
  int con_datos = 0;

  for (i = 0; i < NUM_SECTORES; i++) {
    if (a->cuenta[i] > 0) {
      prom[i] = a->suma[i] / a->cuenta[i];
      con_datos++;
    } else {
      prom[i] = 0.0;
    }
  }
  return con_datos;
}

/*
 * ise_pais:
 * Calcula el ISE de un pais mediante la media ponderada de sus sectores.
 *
 * Recibe:
 * - const double prom[NUM_SECTORES]: arreglo con los 5 promedios por sector del
 *   pais.
 *
 * Variables creadas:
 * - double total: acumulador donde se suma el resultado ponderado.
 * - int i: indice para recorrer los 5 sectores.
 *
 * Paso a paso:
 * 1. Primero se inicializa total en 0.0.
 * 2. Despues se recorre cada sector multiplicando su promedio prom[i] por su
 *    peso en PESO_SECTOR[i] (0.40, 0.30, 0.20, 0.05, 0.05) y sumandolo a total.
 *
 * Retorna:
 * - El valor double del ISE del pais.
 */
double ise_pais(const double prom[NUM_SECTORES]) {
  double total = 0.0;
  int i;

  for (i = 0; i < NUM_SECTORES; i++) {
    total += prom[i] * PESO_SECTOR[i];
  }
  return total;
}

/*
 * ise_global:
 * Calcula el ISE global promediando los ISE de todos los paises participantes.
 *
 * Recibe:
 * - const double ise_paises[]: arreglo con el ISE calculado de cada pais.
 * - int n: cantidad de paises que participaron.
 *
 * Variables creadas:
 * - double suma: acumulador para sumar los ISE de los n paises.
 * - int i: indice para recorrer el arreglo ise_paises.
 *
 * Paso a paso:
 * 1. Primero se revisa si n <= 0; si no hay paises, retorna 0.0 para no
 *    dividir por cero.
 * 2. Despues se suman los n valores del arreglo en la variable suma y se
 *    divide suma entre n.
 *
 * Retorna:
 * - El promedio simple (ISE global) de los paises considerados.
 */
double ise_global(const double ise_paises[], int n) {
  double suma = 0.0;
  int i;

  if (n <= 0) {
    return 0.0;
  }
  for (i = 0; i < n; i++) {
    suma += ise_paises[i];
  }
  return suma / n;
}

/*
 * clasificar:
 * Clasifica un valor de ISE en una recomendacion de inversion.
 *
 * Recibe:
 * - double ise: valor del indicador (de un pais o global) a clasificar.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero se revisa si ise > UMBRAL_VENDER (55.0); si es asi, retorna
 *    REC_VENDER.
 * 2. Despues se revisa si ise < UMBRAL_COMPRAR (45.0); si es asi, retorna
 *    REC_COMPRAR.
 * 3. Si esta entre 45.0 y 55.0 (incluidos), retorna REC_NEUTRO.
 *
 * Retorna:
 * - El valor del enum recomendacion (REC_VENDER, REC_COMPRAR o REC_NEUTRO).
 */
enum recomendacion clasificar(double ise) {
  if (ise > UMBRAL_VENDER) {
    return REC_VENDER;
  }
  if (ise < UMBRAL_COMPRAR) {
    return REC_COMPRAR;
  }
  return REC_NEUTRO;
}

/*
 * nombre_recomendacion:
 * Devuelve el texto correspondiente a una recomendacion de inversion.
 *
 * Recibe:
 * - enum recomendacion r: valor del enum recomendacion a convertir en texto.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Revisa con un switch que recomendacion es y devuelve la cadena literal
 *    correspondiente ("Vender", "Neutro", "Comprar" o "?").
 *
 * Retorna:
 * - Apuntador al texto constante con el nombre de la recomendacion.
 */
const char *nombre_recomendacion(enum recomendacion r) {
  switch (r) {
  case REC_VENDER:
    return "Vender";
  case REC_NEUTRO:
    return "Neutro";
  case REC_COMPRAR:
    return "Comprar";
  default:
    return "?";
  }
}
