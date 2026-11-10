#include "generador.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/*
 * parsear_args:
 * Lee las banderas de la linea de comandos (-p y -c) en cualquier orden y
 * verifica que el pais exista y que la cantidad de encuestas sea un entero
 * positivo.
 *
 * Recibe:
 * - int argc: cantidad de argumentos recibidos por el programa en main.
 * - char **argv: arreglo de cadenas con los argumentos de la linea de comandos.
 * - const struct pais_info **pais: doble apuntador donde se guardara la
 *   direccion de la fila del pais encontrado en la tabla.
 * - long *total: apuntador donde se guardara el total de encuestas a generar.
 *
 * Variables creadas:
 * - int opcion: donde se guarda la letra de cada bandera leida por getopt.
 * - const char *sigla: donde se guarda el texto pasado en la bandera -p.
 * - long valor: donde se guarda el numero convertido de la bandera -c.
 * - int hay_total: bandera (0 o 1) para saber si el usuario paso la opcion -c.
 * - char *fin: apuntador que usa strtol para indicar hasta que caracter pudo
 *   convertir el numero.
 *
 * Paso a paso:
 * 1. Primero se recorren las banderas con getopt(argc, argv, "p:c:"). Los dos
 *    puntos indican que tanto -p como -c requieren un valor (guardado en
 *    optarg).
 * 2. Si la opcion es 'p', se guarda optarg en la variable sigla.
 * 3. Si la opcion es 'c', se pone errno en 0 y se convierte optarg a entero
 *    largo en base 10 con strtol. Se revisa que no haya desbordamiento, que se
 *    haya leido al menos un digito, que no sobren letras al final
 *    (*fin == '\0') y que el valor sea mayor que 0; si falla, se avisa por
 *    stderr y se retorna -1.
 * 4. Despues del ciclo se revisa que hayan llegado ambas banderas (-p y -c) y
 *    que no sobren argumentos sueltos en la linea de comandos (optind == argc).
 * 5. Por ultimo se busca la sigla en la tabla de paises con buscar_pais. Si no
 *    existe se imprime el error y se retorna -1; si existe, se guarda el total
 *    en *total y se retorna 0.
 *
 * Retorna:
 * - 0 si los argumentos son validos, o -1 si hubo algun error.
 */
static int parsear_args(int argc, char **argv, const struct pais_info **pais,
                        long *total) {
  int opcion;
  const char *sigla = NULL;
  long valor = 0;
  int hay_total = 0;
  char *fin;

  while ((opcion = getopt(argc, argv, "p:c:")) != -1) {
    switch (opcion) {
    case 'p':
      sigla = optarg;
      break;
    case 'c':
      errno = 0;
      valor = strtol(optarg, &fin, 10);
      if (errno != 0 || fin == optarg || *fin != '\0' || valor <= 0) {
        fprintf(stderr, "-c debe ser un numero entero mayor que 0: %s\n",
                optarg);
        return -1;
      }
      hay_total = 1;
      break;
    default:
      return -1;
    }
  }

  if (sigla == NULL || !hay_total) {
    fprintf(stderr, "Faltan banderas: se necesitan -p y -c\n");
    return -1;
  }
  if (optind != argc) {
    fprintf(stderr, "Sobran argumentos: %s\n", argv[optind]);
    return -1;
  }

  *pais = buscar_pais(sigla);
  if (*pais == NULL) {
    fprintf(stderr, "Pais desconocido: %s (use ES, FR o IT)\n", sigla);
    return -1;
  }
  *total = valor;
  return 0;
}

/*
 * imprimir_uso:
 * Imprime en la salida de error (stderr) como se debe ejecutar el programa.
 *
 * Recibe:
 * - const char *programa: nombre del ejecutable (argv[0]).
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero y unico paso: se imprime con fprintf en stderr el formato de uso
 *    indicando las banderas -p y -c.
 *
 * Retorna:
 * - Nada (void).
 */
static void imprimir_uso(const char *programa) {
  fprintf(stderr, "Uso: %s -p <ES|FR|IT> -c <num_encuestas>\n", programa);
}

/*
 * explicar_rechazo:
 * Imprime en la salida de error el motivo por el cual repartir_sectores no
 * pudo repartir la cantidad de encuestas solicitada para el pais.
 *
 * Recibe:
 * - const struct pais_info *pais: apuntador a la informacion del pais elegido.
 * - long total: cantidad total de encuestas que se intento repartir.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero se revisa si total es menor que 10 (5 sectores * 2 minimo por
 *    sector) y se informa que no alcanza para el minimo por sector.
 * 2. Despues se revisa si total supera el maximo permitido para ese pais en la
 *    Tabla 1 (pais->max_encuestas) y se informa ese limite.
 * 3. Si no fue ninguno de los dos casos anteriores, se informa que con el tope
 *    del 30% por sector y los maximos de la Tabla 1 los sectores no alcanzan a
 *    sumar el total pedido.
 *
 * Retorna:
 * - Nada (void).
 */
static void explicar_rechazo(const struct pais_info *pais, long total) {
  if (total < NUM_SECTORES * MIN_ENCUESTAS_SECTOR) {
    fprintf(stderr,
            "No se puede: con %ld encuestas no alcanza para %d por sector "
            "(minimo %d en total)\n",
            total, MIN_ENCUESTAS_SECTOR, NUM_SECTORES * MIN_ENCUESTAS_SECTOR);
  } else if (total > pais->max_encuestas) {
    fprintf(stderr,
            "No se puede: %s admite como maximo %d encuestas (Tabla 1)\n",
            pais->sigla, pais->max_encuestas);
  } else {
    fprintf(stderr,
            "No se puede repartir %ld encuestas de %s: con un maximo del %d%% "
            "por sector y los maximos de la Tabla 1, los sectores no alcanzan "
            "a sumar el total. Pruebe con menos encuestas.\n",
            total, pais->sigla, MAX_PORCENTAJE_SECTOR);
  }
}

/*
 * imprimir_resumen:
 * Imprime por pantalla cuantas encuestas se generaron para cada uno de los
 * cinco sectores y el total general, siguiendo el formato del enunciado.
 *
 * Recibe:
 * - long total: cantidad total de encuestas generadas.
 * - const long cuentas[NUM_SECTORES]: arreglo con la cantidad de encuestas que
 *   le toco a cada sector.
 *
 * Variables creadas:
 * - int s: contador para recorrer los cinco sectores de 0 a NUM_SECTORES - 1.
 *
 * Paso a paso:
 * 1. Primero se imprime el titulo "Maximos por sector:".
 * 2. Despues se recorre cada sector con un ciclo for imprimiendo su nombre
 *    alineado a 12 caracteres, cuantas encuestas llevo y el total.
 * 3. Por ultimo se imprime el total de encuestas y el mensaje de confirmacion
 *    "Fichero CSV generado".
 *
 * Retorna:
 * - Nada (void).
 */
static void imprimir_resumen(long total, const long cuentas[NUM_SECTORES]) {
  int s;

  printf("Maximos por sector:\n");
  for (s = 0; s < NUM_SECTORES; s++) {
    printf("  %-12s : %ld/%ld encuestas\n", NOMBRE_SECTOR[s], cuentas[s],
           total);
  }
  printf("Total encuestas: %ld\n", total);
  printf("Fichero CSV generado\n");
}

/*
 * main:
 * Funcion principal del programa generadorCSV. Valida los argumentos,
 * inicializa la semilla de numeros aleatorios, reparte las encuestas por sector
 * y genera el archivo encuestas_<sigla>.csv.
 *
 * Recibe:
 * - int argc: cantidad de argumentos en la linea de comandos.
 * - char **argv: arreglo con los argumentos en la linea de comandos.
 *
 * Variables creadas:
 * - const struct pais_info *pais: donde se guardara el apuntador a los datos
 *   del pais elegido.
 * - long total: donde se guardara la cantidad total de encuestas a generar.
 * - long cuentas[NUM_SECTORES]: arreglo donde repartir_sectores dejara cuantas
 *   encuestas lleva cada sector.
 * - char ruta[32]: arreglo de caracteres donde se arma el nombre del archivo
 *   de salida ("encuestas_XX.csv").
 *
 * Paso a paso:
 * 1. Primero se leen y validan los argumentos con parsear_args. Si hay error,
 *    se muestra la forma de uso y se termina con EXIT_FAILURE.
 * 2. Despues se inicializa la semilla de rand() con srand combinando la hora
 *    actual (time(NULL)) y el identificador del proceso (getpid()) para que dos
 *    ejecuciones en el mismo segundo no generen los mismos datos.
 * 3. Luego se reparten las encuestas entre los sectores con repartir_sectores.
 *    Si no es posible cumplir las restricciones, se explica el motivo con
 *    explicar_rechazo y se termina con EXIT_FAILURE.
 * 4. Se arma el nombre del archivo en ruta con sprintf ("encuestas_%s.csv") y
 *    se imprime el mensaje de inicio de generacion.
 * 5. Se escribe el archivo en disco llamando a escribir_csv; si falla, se
 *    informa por stderr y se termina con EXIT_FAILURE.
 * 6. Por ultimo se muestra el resumen por sector con imprimir_resumen y se
 *    termina con EXIT_SUCCESS.
 *
 * Retorna:
 * - EXIT_SUCCESS (0) si el archivo se genero correctamente, o EXIT_FAILURE si
 *   ocurrio algun error.
 */
int main(int argc, char **argv) {
  const struct pais_info *pais = NULL;
  long total = 0;
  long cuentas[NUM_SECTORES];
  char ruta[32];

  if (parsear_args(argc, argv, &pais, &total) != 0) {
    imprimir_uso(argv[0]);
    return EXIT_FAILURE;
  }

  srand((unsigned)time(NULL) ^ (unsigned)getpid());

  if (repartir_sectores(total, pais, cuentas) != 0) {
    explicar_rechazo(pais, total);
    return EXIT_FAILURE;
  }

  sprintf(ruta, "encuestas_%s.csv", pais->sigla);

  printf("Generando fichero %s para pais %s con %ld de encuestas\n", ruta,
         pais->sigla, total);
  if (escribir_csv(ruta, pais, cuentas) != 0) {
    fprintf(stderr, "No se pudo generar %s\n", ruta);
    return EXIT_FAILURE;
  }

  imprimir_resumen(total, cuentas);
  return EXIT_SUCCESS;
}
