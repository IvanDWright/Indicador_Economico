#include "lector_csv.h"
#include "paises.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENCABEZADO "ID_Encuesta,ID_Pais,Pais,Sector,ID_Sector,Sentimiento"

/*
 * error_csv:
 * Avisa un error de lectura y deshace todo lo que leer_csv habia hecho.
 *
 * Recibe:
 * - FILE *f: el archivo abierto (se cierra aqui).
 * - struct encuesta **encuestas: doble apuntador al arreglo; se libera y queda
 *   en NULL.
 * - const char *ruta: nombre del archivo, para el mensaje.
 * - long nlinea: numero de linea donde fallo (1 = encabezado), o 0 si el error
 *   no es de una linea en particular.
 * - const char *motivo: texto que explica el error.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Imprime por stderr "ruta: linea N: motivo" (o "ruta: motivo").
 * 2. Libera el arreglo con free y deja *encuestas en NULL. free(NULL) no
 *    hace nada, asi que sirve tambien si todavia no se habia reservado.
 * 3. Cierra el archivo con fclose.
 *
 * Retorna:
 * - Siempre -1, para poder escribir "return error_csv(...);".
 */
static int error_csv(FILE *f, struct encuesta **encuestas, const char *ruta,
                     long nlinea, const char *motivo) {
  if (nlinea > 0) {
    fprintf(stderr, "%s: linea %ld: %s\n", ruta, nlinea, motivo);
  } else {
    fprintf(stderr, "%s: %s\n", ruta, motivo);
  }
  free(*encuestas);
  *encuestas = NULL;
  fclose(f);
  return -1;
}

/*
 * leer_csv:
 * Lee todas las encuestas de un archivo CSV y las guarda en un arreglo
 * dinamico.
 *
 * Recibe:
 * - const char *ruta: nombre o ubicacion del archivo CSV a leer.
 * - struct encuesta **encuestas: doble apuntador donde se reservan y guardan
 *   las encuestas.
 * - long *n: apuntador donde se guarda la cantidad total de encuestas leidas.
 * - char sigla[TAM_SIGLA]: arreglo donde se guarda la sigla del pais leido (ej:
 *   "FR").
 * - int *id_pais: apuntador donde se guarda el ID numerico del pais.
 *
 * Variables creadas:
 * - FILE *f: donde se guarda el archivo abierto para leer.
 * - char linea[256]: donde se pondra cada linea de texto al leer el archivo.
 * - long count_lineas: contador de cuantas filas de encuestas tiene el CSV.
 * - long i: indice para ir llenando cada casilla del arreglo de encuestas.
 * - long nlinea: numero de la linea que se esta leyendo, para los mensajes de
 *   error (el encabezado es la 1).
 * - size_t largo_enc: cuantas letras tiene el encabezado esperado.
 * - char pais_fila[TAM_SIGLA]: guarda temporalmente la sigla de cada fila.
 * - char nombre_sec[32]: guarda temporalmente el texto del nombre del sector.
 * - char sobra: recibe lo que haya despues del sentimiento (no deberia haber
 *   nada).
 * - int id_enc, id_p, id_sec: guardan los IDs leidos de cada fila.
 * - double sent: guarda el valor del sentimiento leido de cada fila.
 * - const struct pais_info *pais: fila de la tabla de paises.c que corresponde
 *   a la sigla de la primera fila.
 *
 * Paso a paso:
 * 1. Primero se revisa que ninguno de los apuntadores recibidos sea NULL, se
 *    deja *encuestas en NULL y *n en 0 (asi, si algo falla, quien llama no
 *    se queda con basura) y se abre el archivo en modo lectura ("r").
 * 2. Se lee la primera linea con fgets y se revisa que sea el encabezado
 *    exacto. Si no lo es, se rechaza el archivo: si no tuviera encabezado,
 *    descartar la primera linea perderia una encuesta sin avisar.
 * 3. En la primera pasada se recorre el archivo con fgets sumando 1 a
 *    count_lineas por cada fila con datos (ignorando lineas en blanco). Si
 *    hubo un error de lectura (ferror) o count_lineas es 0, retorna -1.
 * 4. Con la cantidad exacta de lineas se reserva el arreglo dinamico con
 *    malloc((size_t)count_lineas * sizeof(struct encuesta)).
 * 5. En la segunda pasada se rebobina el archivo al inicio con rewind(f), se
 *    vuelve a saltar el encabezado y se lee cada fila. Si la linea no trae
 *    '\n' y no es la ultima del archivo, no cupo en linea[256]: se rechaza.
 * 6. Cada fila se separa en sus 6 campos con sscanf. Al final del formato va
 *    " %c": el espacio salta los blancos (tambien '\r' y '\n') y %c lee lo
 *    que sobre. Si sscanf devuelve 7 es que habia algo despues del
 *    sentimiento, como en "45,23" (decimal con coma) o "45.23abc": sin esta
 *    revision, sscanf leeria 45 y 45.23 sin avisar. Debe devolver 6.
 * 7. Se revisa que el sector este entre 0 y 4 y el sentimiento entre 0.0 y
 *    100.0. Se escribe !(sent >= 0.0 && sent <= 100.0) en lugar de
 *    sent < 0.0 || sent > 100.0 porque "nan" no es mayor, menor ni igual a
 *    nada: con la segunda forma pasaria como valido.
 * 8. En la primera fila (i == 0) se busca la sigla en la tabla con
 *    buscar_pais y se exige que exista y que su id sea el ID_Pais de la
 *    fila; luego se guardan sigla e id_pais. En las demas filas se revisa que
 *    coincidan con la primera (un agente = un pais).
 * 9. Se guardan id_encuesta, id_sector y sentimiento en (*encuestas)[i]. La
 *    columna Sector (texto) no se guarda: manda ID_Sector.
 * 10. Al terminar se revisa que no haya habido error de lectura y que la
 *     segunda pasada haya leido las mismas filas que la primera (si no, el
 *     archivo cambio en medio), se cierra el archivo y se asigna *n.
 * 11. En cualquiera de los pasos anteriores, si hay un error se informa con
 *     error_csv, que dice la linea, libera el arreglo y cierra el archivo.
 *
 * Retorna:
 * - 0 si se leyo todo el archivo correctamente, o -1 si hubo algun error (en
 *   ese caso *encuestas queda en NULL y *n en 0).
 */
int leer_csv(const char *ruta, struct encuesta **encuestas, long *n,
             char sigla[TAM_SIGLA], int *id_pais) {
  FILE *f;
  char linea[256];
  long count_lineas = 0;
  long i = 0;
  long nlinea = 1;
  size_t largo_enc = strlen(ENCABEZADO);
  char pais_fila[TAM_SIGLA];
  char nombre_sec[32];
  char sobra;
  int id_enc;
  int id_p;
  int id_sec;
  double sent;
  const struct pais_info *pais;

  if (ruta == NULL || encuestas == NULL || n == NULL || sigla == NULL ||
      id_pais == NULL) {
    return -1;
  }
  *encuestas = NULL;
  *n = 0;

  f = fopen(ruta, "r");
  if (f == NULL) {
    perror(ruta);
    return -1;
  }

  if (fgets(linea, sizeof(linea), f) == NULL ||
      strncmp(linea, ENCABEZADO, largo_enc) != 0 ||
      (linea[largo_enc] != '\n' && linea[largo_enc] != '\r' &&
       linea[largo_enc] != '\0')) {
    return error_csv(f, encuestas, ruta, 1,
                     "se esperaba el encabezado " ENCABEZADO);
  }

  while (fgets(linea, sizeof(linea), f) != NULL) {
    if (linea[0] == '\n' || linea[0] == '\r' || linea[0] == '\0') {
      continue;
    }
    count_lineas++;
  }
  if (ferror(f)) {
    return error_csv(f, encuestas, ruta, 0, "error al leer el archivo");
  }
  if (count_lineas == 0) {
    return error_csv(f, encuestas, ruta, 0,
                     "no tiene encuestas (solo el encabezado)");
  }

  *encuestas =
      (struct encuesta *)malloc((size_t)count_lineas * sizeof(struct encuesta));
  if (*encuestas == NULL) {
    return error_csv(f, encuestas, ruta, 0, "no hay memoria para leerlo");
  }

  rewind(f);
  if (fgets(linea, sizeof(linea), f) == NULL) {
    return error_csv(f, encuestas, ruta, 1, "no se pudo releer el encabezado");
  }

  while (i < count_lineas && fgets(linea, sizeof(linea), f) != NULL) {
    nlinea++;
    if (strchr(linea, '\n') == NULL && !feof(f)) {
      return error_csv(f, encuestas, ruta, nlinea, "linea demasiado larga");
    }
    if (linea[0] == '\n' || linea[0] == '\r' || linea[0] == '\0') {
      continue;
    }
    if (sscanf(linea, "%d,%d,%2[^,],%31[^,],%d,%lf %c", &id_enc, &id_p,
               pais_fila, nombre_sec, &id_sec, &sent, &sobra) != 6) {
      return error_csv(f, encuestas, ruta, nlinea,
                       "se esperaban 6 campos: ID_Encuesta,ID_Pais,Pais,"
                       "Sector,ID_Sector,Sentimiento (con punto decimal)");
    }

    if (id_sec < 0 || id_sec >= NUM_SECTORES) {
      return error_csv(f, encuestas, ruta, nlinea,
                       "ID_Sector fuera de rango (0 a 4)");
    }
    if (!(sent >= 0.0 && sent <= 100.0)) {
      return error_csv(f, encuestas, ruta, nlinea,
                       "Sentimiento fuera de rango (0 a 100)");
    }

    if (i == 0) {
      pais = buscar_pais(pais_fila);
      if (pais == NULL || pais->id != id_p) {
        return error_csv(f, encuestas, ruta, nlinea,
                         "Pais e ID_Pais no corresponden a ES=0, FR=1 ni "
                         "IT=2");
      }
      strncpy(sigla, pais_fila, TAM_SIGLA - 1);
      sigla[TAM_SIGLA - 1] = '\0';
      *id_pais = id_p;
    } else if (strcmp(sigla, pais_fila) != 0 || *id_pais != id_p) {
      return error_csv(f, encuestas, ruta, nlinea,
                       "Pais o ID_Pais distinto al de la primera fila (un "
                       "archivo = un pais)");
    }

    (*encuestas)[i].id_encuesta = id_enc;
    (*encuestas)[i].id_sector = id_sec;
    (*encuestas)[i].sentimiento = sent;
    i++;
  }

  if (ferror(f)) {
    return error_csv(f, encuestas, ruta, 0, "error al leer el archivo");
  }
  if (i != count_lineas) {
    return error_csv(f, encuestas, ruta, 0,
                     "el archivo cambio mientras se leia");
  }

  fclose(f);
  *n = count_lineas;
  return 0;
}

/*
 * liberar_encuestas:
 * Libera la memoria dinamica reservada para el arreglo de encuestas.
 *
 * Recibe:
 * - struct encuesta *encuestas: apuntador al arreglo de encuestas reservado con
 *   malloc.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Llama a free(encuestas) para devolver la memoria al sistema operativo.
 *    free(NULL) no hace nada, asi que se puede llamar aunque leer_csv haya
 *    fallado.
 *
 * Retorna:
 * - Nada (void).
 */
void liberar_encuestas(struct encuesta *encuestas) { free(encuestas); }
