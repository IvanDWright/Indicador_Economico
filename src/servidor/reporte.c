#include "ise.h"
#include "paises.h"
#include "servidor.h"
#include <stdio.h>

/*
 * imprimir_detalle_pais:
 * Calcula e imprime por pantalla el desglose de promedios por sector, el
 * estado de recepcion, el ISE individual y la recomendacion para un agente
 * registrado.
 *
 * Recibe:
 * - const struct agente_ctx *ag: apuntador a los datos acumulados del agente.
 * - double *ise_out: apuntador donde se guardara el ISE calculado para este
 *   pais si recibio al menos una encuesta.
 *
 * Variables creadas:
 * - double prom[NUM_SECTORES]: arreglo donde promedios_sector deja el promedio
 *   de sentimiento de cada uno de los cinco sectores.
 * - int nsect: cantidad de sectores que tuvieron al menos una encuesta.
 * - int s: contador para recorrer los cinco sectores de 0 a NUM_SECTORES - 1.
 * - double valor_ise: donde se guarda el ISE ponderado calculado para el pais.
 * - enum recomendacion rec: clasificacion obtenida para el ISE del pais.
 *
 * Paso a paso:
 * 1. Primero se calculan los promedios de los cinco sectores con
 *    promedios_sector(&ag->acum, prom), que retorna cuantos sectores tuvieron
 *    datos.
 * 2. Despues se imprime el encabezado del pais con su sigla y la cantidad de
 *    encuestas recibidas frente a las esperadas. Si el envio quedo incompleto
 *    (ag->completo == 0 o recibidas != esperadas), se imprime una advertencia.
 * 3. Si nsect es 0, se informa que el pais no recibio encuestas y se retorna 0
 *    para no incluirlo en el promedio global.
 * 4. Si hay datos, se recorren los cinco sectores imprimiendo el nombre del
 *    sector, su peso porcentual, la cantidad de encuestas recibidas y su
 *    promedio (indicando si algun sector quedo sin encuestas).
 * 5. Por ultimo se calcula el ISE ponderado del pais con ise_pais(prom), su
 *    recomendacion con clasificar(valor_ise), se imprimen ambos con 4 decimales
 *    y se guarda valor_ise en *ise_out retornando 1.
 *
 * Retorna:
 * - 1 si el pais tuvo datos validos y calculo su ISE, o 0 si no recibio datos.
 */
static int imprimir_detalle_pais(const struct agente_ctx *ag, double *ise_out) {
  double prom[NUM_SECTORES];
  int nsect;
  int s;
  double valor_ise;
  enum recomendacion rec;

  nsect = promedios_sector(&ag->acum, prom);

  printf("\n------------------------------------------------------------\n");
  printf("Pais: %s (ID: %d) | Encuestas: %ld/%ld", ag->sigla, ag->id_pais,
         ag->recibidas, ag->esperadas);
  if (!ag->completo || ag->recibidas != ag->esperadas) {
    printf(" [INCOMPLETO]\n");
  } else {
    printf(" [COMPLETO]\n");
  }

  if (nsect == 0) {
    printf("  Sin datos recibidos para este pais.\n");
    return 0;
  }

  if (nsect < NUM_SECTORES) {
    printf("  Aviso: solo se recibieron datos en %d de %d sectores.\n", nsect,
           NUM_SECTORES);
  }

  for (s = 0; s < NUM_SECTORES; s++) {
    printf("  %-12s (peso %2.0f%%) : %6ld encuestas | promedio: %6.2f\n",
           NOMBRE_SECTOR[s], PESO_SECTOR[s] * 100.0, ag->acum.cuenta[s],
           prom[s]);
  }

  valor_ise = ise_pais(prom);
  rec = clasificar(valor_ise);
  *ise_out = valor_ise;

  printf("  ISE %s: %.4f -> Recomendacion: %s\n", ag->sigla, valor_ise,
         nombre_recomendacion(rec));
  return 1;
}

/*
 * imprimir_reporte:
 * Imprime por salida estandar el resumen general del Indicador de Sentimiento
 * Economico (ISE) al finalizar la ejecucion del servidor, mostrando el detalle
 * de cada pais conectado, el ISE global y la recomendacion final de inversion.
 *
 * Recibe:
 * - const struct servidor *s: apuntador a la estructura principal del servidor
 *   con todos los agentes registrados y sus acumuladores.
 *
 * Variables creadas:
 * - double ise_paises[MAX_AGENTES]: arreglo donde se guarda el ISE de cada
 *   pais que tuvo datos validos.
 * - double ise_individual: variable donde imprimir_detalle_pais deposita el
 *   ISE de un pais.
 * - double global: donde se guarda el promedio simple de los ISE de los paises.
 * - enum recomendacion rec_global: clasificacion final (Vender, Comprar o
 *   Neutro) segun el ISE global.
 * - int n_validos: contador de cuantos paises aportaron datos al ISE global.
 * - int i: indice para recorrer los agentes registrados en el servidor.
 *
 * Paso a paso:
 * 1. Primero se revisa que el apuntador s no sea NULL y se imprime el titulo
 *    del resumen general ISE.
 * 2. Si no se registro ningun agente (s->n_registrados == 0), se informa que
 *    ningun agente se conecto durante el tiempo de espera y se termina.
 * 3. Despues se recorren los s->n_registrados agentes llamando a
 *    imprimir_detalle_pais para cada uno. Si el pais tuvo datos, se guarda su
 *    ISE en el arreglo ise_paises y se incrementa n_validos.
 * 4. Si al menos un pais tuvo datos (n_validos > 0), se calcula el ISE global
 *    con ise_global(ise_paises, n_validos) y su clasificacion con clasificar.
 * 5. Por ultimo se imprime el ISE global con 4 decimales y la recomendacion
 *    general de inversion (Vender, Comprar o Neutro).
 *
 * Retorna:
 * - Nada (void).
 */
void imprimir_reporte(const struct servidor *s) {
  double ise_paises[MAX_AGENTES];
  double ise_individual = 0.0;
  double global;
  enum recomendacion rec_global;
  int n_validos = 0;
  int i;

  if (s == NULL) {
    return;
  }

  printf("\n============================================================\n");
  printf("                  RESUMEN GENERAL ISE\n");
  printf("============================================================\n");

  if (s->n_registrados == 0) {
    printf("No se conecto ningun agente durante el tiempo de ejecucion.\n");
    printf("============================================================\n");
    return;
  }

  for (i = 0; i < s->n_registrados; i++) {
    if (imprimir_detalle_pais(&s->agentes[i], &ise_individual)) {
      ise_paises[n_validos] = ise_individual;
      n_validos++;
    }
  }

  printf("\n============================================================\n");
  if (n_validos == 0) {
    printf("Ningun agente envio encuestas validas para calcular el ISE.\n");
  } else {
    global = ise_global(ise_paises, n_validos);
    rec_global = clasificar(global);
    printf("Paises procesados       : %d/%d\n", n_validos, s->cfg.max_agentes);
    printf("ISE Global              : %.4f\n", global);
    printf("Recomendacion Inversion : %s\n", nombre_recomendacion(rec_global));
  }
  printf("============================================================\n");
}
