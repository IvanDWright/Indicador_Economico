#include "lector_csv.h"
#include "protocolo.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * parsear_args:
 * Lee las banderas de la linea de comandos (-n y -p) en cualquier orden y
 * guarda la ruta del archivo CSV y el nombre del pipe nominal.
 *
 * Recibe:
 * - int argc: cantidad de argumentos recibidos por el programa en main.
 * - char **argv: arreglo de cadenas con los argumentos de la linea de comandos.
 * - const char **ruta_csv: doble apuntador donde se guardara la ruta del
 *   archivo CSV indicada con -n.
 * - const char **pipe_nombre: doble apuntador donde se guardara el nombre del
 *   pipe nominal indicado con -p.
 *
 * Variables creadas:
 * - int opcion: donde se guarda la letra de cada bandera leida por getopt.
 * - const char *csv: donde se guarda temporalmente el argumento de -n.
 * - const char *pipe_nom: donde se guarda temporalmente el argumento de -p.
 *
 * Paso a paso:
 * 1. Primero se recorren las banderas con getopt(argc, argv, "n:p:"). Los dos
 *    puntos indican que tanto -n como -p llevan un valor en optarg.
 * 2. Si la opcion es 'n', se guarda optarg en csv; si es 'p', se guarda en
 *    pipe_nom; si llega otra bandera desconocida, se retorna -1.
 * 3. Despues de leer las banderas se revisa que ninguna de las dos falte ni sea
 *    una cadena vacia; si falta alguna se avisa por stderr y se retorna -1.
 * 4. Luego se comprueba que no hayan quedado argumentos sueltos
 *    (optind == argc).
 * 5. Por ultimo se copian las direcciones en *ruta_csv y *pipe_nombre y se
 *    retorna 0.
 *
 * Retorna:
 * - 0 si ambas banderas son validas, o -1 si hubo algun error.
 */
static int parsear_args(int argc, char **argv, const char **ruta_csv,
                        const char **pipe_nombre) {
  int opcion;
  const char *csv = NULL;
  const char *pipe_nom = NULL;

  while ((opcion = getopt(argc, argv, "n:p:")) != -1) {
    switch (opcion) {
    case 'n':
      csv = optarg;
      break;
    case 'p':
      pipe_nom = optarg;
      break;
    default:
      return -1;
    }
  }

  if (csv == NULL || pipe_nom == NULL || *csv == '\0' || *pipe_nom == '\0') {
    fprintf(stderr, "Faltan banderas: se necesitan -n y -p\n");
    return -1;
  }
  if (optind != argc) {
    fprintf(stderr, "Sobran argumentos: %s\n", argv[optind]);
    return -1;
  }

  *ruta_csv = csv;
  *pipe_nombre = pipe_nom;
  return 0;
}

/*
 * imprimir_uso:
 * Imprime en la salida de error (stderr) como se debe invocar el programa
 * agentePais.
 *
 * Recibe:
 * - const char *programa: nombre del ejecutable (argv[0]).
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero y unico paso: se imprime por stderr el formato de llamada con las
 *    banderas -n y -p.
 *
 * Retorna:
 * - Nada (void).
 */
static void imprimir_uso(const char *programa) {
  fprintf(stderr, "Uso: %s -n <encuestas_XX.csv> -p <pipe_nominal>\n",
          programa);
}

/*
 * enviar_encuestas:
 * Envia por el pipe nominal el mensaje inicial de registro (MSG_HELLO), todas
 * las encuestas del arreglo dinamico una por una (MSG_DATO) mostrando el
 * progreso cada 500 registros, y finalmente el mensaje de cierre (MSG_FIN).
 *
 * Recibe:
 * - int fd: descriptor de archivo del pipe nominal abierto para escritura.
 * - const struct encuesta *encuestas: arreglo con todas las encuestas leidas
 *   del archivo CSV.
 * - long n: cantidad total de encuestas en el arreglo.
 * - const char *sigla: sigla del pais de 2 letras ("ES", "FR" o "IT").
 * - int id_pais: identificador numerico del pais.
 *
 * Variables creadas:
 * - struct mensaje m: estructura de tamano fijo que se llena y se escribe de
 *   forma atomica en el pipe nominal.
 * - long i: indice para recorrer las encuestas desde 0 hasta n - 1.
 *
 * Paso a paso:
 * 1. Primero se limpia la estructura m con memset en 0, se arma el mensaje de
 *    presentacion (MSG_HELLO) con el id_pais, la sigla y el total esperado n,
 *    y se envia al servidor con enviar_mensaje.
 * 2. Despues se recorre el arreglo encuestas con un ciclo for desde 0 hasta
 *    n - 1. En cada vuelta se limpia m con memset, se copia el id_encuesta,
 *    id_sector y sentimiento en un mensaje MSG_DATO y se envia por el pipe.
 * 3. Cada vez que la cantidad enviada (i + 1) es multiplo de LOTE_PROGRESO
 *    (500), se imprime el avance en consola y se vacia el buffer de salida con
 *    fflush(stdout).
 * 4. Al terminar el ciclo se imprime el total de encuestas enviadas, se limpia
 *    m con memset, se arma el mensaje de finalizacion (MSG_FIN) con el total
 *    enviado y se manda por el pipe.
 * 5. Por ultimo se imprime la confirmacion de que el mensaje de finalizacion
 *    fue enviado y se retorna 0.
 *
 * Retorna:
 * - 0 si todos los mensajes se enviaron correctamente, o -1 si fallo alguna
 *   escritura en el pipe.
 */
static int enviar_encuestas(int fd, const struct encuesta *encuestas, long n,
                            const char *sigla, int id_pais) {
  struct mensaje m;
  long i;

  memset(&m, 0, sizeof(m));
  m.tipo = MSG_HELLO;
  m.id_pais = id_pais;
  strncpy(m.sigla, sigla, TAM_SIGLA - 1);
  m.sigla[TAM_SIGLA - 1] = '\0';
  m.total = n;

  if (enviar_mensaje(fd, &m) != 0) {
    aviso("enviar_mensaje (MSG_HELLO)");
    return -1;
  }

  for (i = 0; i < n; i++) {
    memset(&m, 0, sizeof(m));
    m.tipo = MSG_DATO;
    m.id_pais = id_pais;
    strncpy(m.sigla, sigla, TAM_SIGLA - 1);
    m.sigla[TAM_SIGLA - 1] = '\0';
    m.id_encuesta = encuestas[i].id_encuesta;
    m.id_sector = encuestas[i].id_sector;
    m.sentimiento = encuestas[i].sentimiento;

    if (enviar_mensaje(fd, &m) != 0) {
      aviso("enviar_mensaje (MSG_DATO)");
      return -1;
    }

    if ((i + 1) % LOTE_PROGRESO == 0) {
      printf("  %ld/%ld\n", i + 1, n);
      fflush(stdout);
    }
  }

  printf("Enviadas %ld/%ld encuestas.\n", n, n);

  memset(&m, 0, sizeof(m));
  m.tipo = MSG_FIN;
  m.id_pais = id_pais;
  strncpy(m.sigla, sigla, TAM_SIGLA - 1);
  m.sigla[TAM_SIGLA - 1] = '\0';
  m.total = n;

  if (enviar_mensaje(fd, &m) != 0) {
    aviso("enviar_mensaje (MSG_FIN)");
    return -1;
  }

  printf("Mensaje de finalizacion enviado.\n");
  return 0;
}

/*
 * main:
 * Funcion principal del proceso cliente agentePais. Lee las encuestas de su
 * archivo CSV, abre el pipe nominal en modo solo escritura (O_WRONLY), envia
 * todas las encuestas al servidor y libera sus recursos antes de terminar.
 *
 * Recibe:
 * - int argc: cantidad de argumentos de la linea de comandos.
 * - char **argv: arreglo con los argumentos de la linea de comandos.
 *
 * Variables creadas:
 * - const char *ruta_csv: donde se guarda la ruta del archivo CSV a leer.
 * - const char *pipe_nombre: donde se guarda el nombre del pipe nominal.
 * - struct encuesta *encuestas: apuntador al arreglo dinamico de encuestas.
 * - long n: donde se guarda el total de encuestas leidas del archivo CSV.
 * - char sigla[TAM_SIGLA]: arreglo donde leer_csv deja la sigla del pais.
 * - int id_pais: donde leer_csv deja el identificador numerico del pais.
 * - int fd: descriptor de archivo del pipe nominal devuelto por open().
 * - int rc: codigo de retorno de enviar_encuestas para saber si hubo error.
 * - int codigo_error: copia de errno tomada justo despues de que open() falla,
 *   porque perror puede cambiar errno antes de que se alcance a revisar.
 *
 * Paso a paso:
 * 1. Primero se validan las banderas -n y -p con parsear_args; si fallan, se
 *    muestra el uso y se termina con EXIT_FAILURE.
 * 2. Despues se ignora la senal SIGPIPE con signal(SIGPIPE, SIG_IGN). Segun las
 *    reglas de pipes, si el proceso lector cierra el pipe y el escritor intenta
 *    hacer write(), el sistema operativo envia SIGPIPE; al ignorarla, write()
 *    retorna -1 y el agente puede cerrar su descriptor y liberar memoria. Si
 *    signal falla (retorna SIG_ERR) se avisa y se termina con EXIT_FAILURE.
 * 3. Luego se carga el archivo CSV en memoria con leer_csv; si el archivo es
 *    invalido se termina con EXIT_FAILURE.
 * 4. Se imprimen el pais, el pipe y la cantidad de encuestas leidas, una linea
 *    por dato como en el ejemplo del enunciado, y se abre el pipe nominal
 *    unicamente para escritura con open(pipe_nombre, O_WRONLY). Esta llamada
 *    bloquea al agente hasta que el servidor tenga abierto el extremo de
 *    lectura. Si falla, se guarda errno en codigo_error, se informa el error y,
 *    si el pipe no existe (ENOENT), se recuerda que el servidor va primero.
 * 5. Una vez conectado, se envian todos los mensajes con enviar_encuestas.
 * 6. Por ultimo se cierra el descriptor del pipe con close(fd) y se libera el
 *    arreglo dinamico con liberar_encuestas. Si todo salio bien se imprime el
 *    mensaje de exito y se retorna EXIT_SUCCESS.
 *
 * Retorna:
 * - EXIT_SUCCESS (0) si el envio finalizo correctamente, o EXIT_FAILURE si
 *   ocurrio algun error.
 */
int main(int argc, char **argv) {
  const char *ruta_csv = NULL;
  const char *pipe_nombre = NULL;
  struct encuesta *encuestas = NULL;
  long n = 0;
  char sigla[TAM_SIGLA];
  int id_pais = 0;
  int fd = -1;
  int rc;
  int codigo_error;

  if (parsear_args(argc, argv, &ruta_csv, &pipe_nombre) != 0) {
    imprimir_uso(argv[0]);
    return EXIT_FAILURE;
  }

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
    aviso("signal (SIGPIPE)");
    return EXIT_FAILURE;
  }

  if (leer_csv(ruta_csv, &encuestas, &n, sigla, &id_pais) != 0) {
    return EXIT_FAILURE;
  }

  printf("Pais: %s\n", sigla);
  printf("Pipe: %s\n", pipe_nombre);
  printf("Leidas %ld encuestas.\n", n);
  printf("Conectando al proceso principal...\n");
  fflush(stdout);

  fd = open(pipe_nombre, O_WRONLY);
  if (fd == -1) {
    codigo_error = errno;
    aviso(pipe_nombre);
    if (codigo_error == ENOENT) {
      fprintf(stderr, "Verifique que servidorISE este ejecutandose primero.\n");
    }
    liberar_encuestas(encuestas);
    return EXIT_FAILURE;
  }

  printf("Conectado.\n");
  printf("Enviando...\n");

  rc = enviar_encuestas(fd, encuestas, n, sigla, id_pais);

  close(fd);
  liberar_encuestas(encuestas);

  if (rc != 0) {
    fprintf(stderr, "Error durante el envio del agente %s.\n", sigla);
    return EXIT_FAILURE;
  }

  printf("Envio %s finalizado satisfactoriamente!!.\n", sigla);
  return EXIT_SUCCESS;
}
