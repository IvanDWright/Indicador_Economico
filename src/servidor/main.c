#include "protocolo.h"
#include "servidor.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Descriptor de escritura del pipe nominal usado por el manejador de senales
 * para enviar el mensaje MSG_STOP cuando suena la alarma de 60 segundos
 * (SIGALRM) o llega una interrupcion desde consola (SIGINT). */
static int fd_aviso = -1;

/*
 * manejador_senal:
 * Manejador asincrono para las senales SIGALRM (alarma de reloj de 60 segundos)
 * y SIGINT (Ctrl-C desde consola). Envia un mensaje interno MSG_STOP por el
 * descriptor de escritura del pipe nominal para despertar al hilo recolector
 * si se encuentra suspendido en read().
 *
 * Recibe:
 * - int sig: numero de la senal recibida (SIGALRM o SIGINT).
 *
 * Variables creadas:
 * - struct mensaje m: estructura de mensaje con tipo MSG_STOP que se escribe
 *   en el pipe para destrabar la lectura del recolector.
 * - ssize_t escrito: cantidad de bytes devuelta por la llamada al sistema
 *   write.
 *
 * Paso a paso:
 * 1. Primero se silencia el aviso de parametro sin usar con (void)sig.
 * 2. Despues se revisa que el descriptor fd_aviso sea valido (>= 0).
 * 3. Se limpia la estructura m con memset en 0, se le asigna m.tipo = MSG_STOP
 *    y se escribe en el pipe nominal con write(fd_aviso, &m, sizeof(m)).
 *
 * Retorna:
 * - Nada (void).
 */
static void manejador_senal(int sig) {
  struct mensaje m;
  ssize_t escrito;

  (void)sig;
  if (fd_aviso >= 0) {
    memset(&m, 0, sizeof(m));
    m.tipo = MSG_STOP;
    escrito = write(fd_aviso, &m, sizeof(m));
    (void)escrito;
  }
}

/*
 * parsear_args:
 * Lee las tres banderas de la linea de comandos (-b, -p y -a) en cualquier
 * orden y verifica que el tamano del buffer, el nombre del pipe nominal y la
 * cantidad de agentes sean validos.
 *
 * Recibe:
 * - int argc: cantidad de argumentos recibidos por el programa en main.
 * - char **argv: arreglo de cadenas con los argumentos de la linea de comandos.
 * - struct config_servidor *cfg: apuntador a la estructura de configuracion
 *   donde se guardaran tam_buffer, pipe_nombre y max_agentes.
 *
 * Variables creadas:
 * - int opcion: donde se guarda la letra de cada bandera leida por getopt.
 * - long valor: donde se guarda temporalmente el numero convertido con strtol.
 * - char *fin: apuntador que usa strtol para verificar que no sobren letras.
 * - int hay_b: bandera (0 o 1) para saber si se recibio la opcion -b.
 * - int hay_p: bandera (0 o 1) para saber si se recibio la opcion -p.
 * - int hay_a: bandera (0 o 1) para saber si se recibio la opcion -a.
 *
 * Paso a paso:
 * 1. Primero se recorren las banderas con getopt(argc, argv, "b:p:a:").
 * 2. Si la opcion es 'b', se convierte optarg con strtol y se comprueba que
 *    sea un entero entre 1 y 1000000 (el tope evita pedir una cantidad de
 *    memoria absurda por error); si es valido, se guarda en cfg->tam_buffer.
 * 3. Si la opcion es 'p', se comprueba que optarg no sea una cadena vacia y
 *    se guarda en cfg->pipe_nombre.
 * 4. Si la opcion es 'a', se convierte optarg con strtol y se comprueba que
 *    este entre 1 y MAX_AGENTES (4); si es valido, se guarda en
 *    cfg->max_agentes.
 * 5. Al terminar el ciclo se verifica que hayan llegado las tres banderas y
 *    que no sobren argumentos sueltos en la linea de comandos (optind == argc).
 *
 * Retorna:
 * - 0 si las tres banderas son validas, o -1 si hubo algun error.
 */
static int parsear_args(int argc, char **argv, struct config_servidor *cfg) {
  int opcion;
  long valor;
  char *fin;
  int hay_b = 0;
  int hay_p = 0;
  int hay_a = 0;

  while ((opcion = getopt(argc, argv, "b:p:a:")) != -1) {
    switch (opcion) {
    case 'b':
      errno = 0;
      valor = strtol(optarg, &fin, 10);
      if (errno != 0 || fin == optarg || *fin != '\0' || valor <= 0 ||
          valor > 1000000L) {
        fprintf(stderr, "-b debe ser un entero entre 1 y 1000000: %s\n",
                optarg);
        return -1;
      }
      cfg->tam_buffer = (int)valor;
      hay_b = 1;
      break;
    case 'p':
      if (optarg == NULL || *optarg == '\0') {
        fprintf(stderr, "-p requiere un nombre de pipe valido\n");
        return -1;
      }
      cfg->pipe_nombre = optarg;
      hay_p = 1;
      break;
    case 'a':
      errno = 0;
      valor = strtol(optarg, &fin, 10);
      if (errno != 0 || fin == optarg || *fin != '\0' || valor < 1 ||
          valor > MAX_AGENTES) {
        fprintf(stderr, "-a debe estar entre 1 y %d: %s\n", MAX_AGENTES,
                optarg);
        return -1;
      }
      cfg->max_agentes = (int)valor;
      hay_a = 1;
      break;
    default:
      return -1;
    }
  }

  if (!hay_b || !hay_p || !hay_a) {
    fprintf(stderr, "Faltan banderas: se necesitan -b, -p y -a\n");
    return -1;
  }
  if (optind != argc) {
    fprintf(stderr, "Sobran argumentos: %s\n", argv[optind]);
    return -1;
  }

  return 0;
}

/*
 * imprimir_uso:
 * Imprime en la salida de error (stderr) la forma correcta de invocar el
 * programa servidorISE.
 *
 * Recibe:
 * - const char *programa: nombre del ejecutable (argv[0]).
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero y unico paso: se imprime con fprintf en stderr el formato de
 *    llamada indicando las banderas -b, -p y -a.
 *
 * Retorna:
 * - Nada (void).
 */
static void imprimir_uso(const char *programa) {
  fprintf(stderr, "Uso: %s -b <tamB> -p <PipeN> -a <agentes>\n", programa);
}

/*
 * crear_fifo:
 * Crea el pipe nominal en el sistema de archivos usando mkfifo con permisos de
 * lectura y escritura para el usuario (S_IRUSR | S_IWUSR) y abre los
 * descriptores de lectura y de escritura auxiliar del servidor.
 *
 * Recibe:
 * - struct servidor *s: apuntador a la estructura del servidor donde se
 *   guardaran los descriptores fd_pipe y fd_ficticio.
 *
 * Variables creadas:
 * - mode_t fifo_mode: permisos del pipe nominal (S_IRUSR | S_IWUSR).
 * - const char *ruta: nombre del pipe nominal tomado de s->cfg.pipe_nombre.
 * - int fd_temp: descriptor temporal abierto con O_NONBLOCK para poder abrir
 *   ambos extremos sin bloquear el arranque del servidor.
 *
 * Paso a paso:
 * 1. Primero se elimina cualquier pipe nominal residual con el mismo nombre
 *    usando unlink(ruta) y se crea el pipe nominal nuevo con
 *    mkfifo(ruta, fifo_mode).
 * 2. Despues se abre temporalmente el extremo de lectura con
 *    open(ruta, O_RDONLY | O_NONBLOCK) para que no se bloquee esperando un
 *    escritor.
 * 3. Con el extremo de lectura ya abierto, se abre el descriptor de escritura
 *    auxiliar s->fd_ficticio = open(ruta, O_WRONLY). Este descriptor evita que
 *    read() devuelva 0 (fin de entrada) cuando un agente cierra el pipe antes
 *    de que se conecte el siguiente, y permite inyectar MSG_STOP al sonar la
 *    alarma de 60 segundos.
 * 4. Luego se abre el descriptor definitivo de lectura bloqueante
 *    s->fd_pipe = open(ruta, O_RDONLY) y se cierra el descriptor temporal
 *    close(fd_temp).
 * 5. Si cualquiera de las aperturas falla, se cierran los descriptores que se
 *    hayan abierto, se borra el pipe con unlink(ruta) y se retorna -1.
 *
 * Retorna:
 * - 0 si el pipe nominal quedo creado y abierto correctamente, o -1 si fallo.
 */
static int crear_fifo(struct servidor *s) {
  mode_t fifo_mode = S_IRUSR | S_IWUSR;
  const char *ruta = s->cfg.pipe_nombre;
  int fd_temp;

  unlink(ruta);
  if (mkfifo(ruta, fifo_mode) == -1) {
    aviso(ruta);
    return -1;
  }

  fd_temp = open(ruta, O_RDONLY | O_NONBLOCK);
  if (fd_temp == -1) {
    aviso(ruta);
    unlink(ruta);
    return -1;
  }

  s->fd_ficticio = open(ruta, O_WRONLY);
  if (s->fd_ficticio == -1) {
    aviso(ruta);
    close(fd_temp);
    unlink(ruta);
    return -1;
  }

  s->fd_pipe = open(ruta, O_RDONLY);
  close(fd_temp);
  if (s->fd_pipe == -1) {
    aviso(ruta);
    close(s->fd_ficticio);
    s->fd_ficticio = -1;
    unlink(ruta);
    return -1;
  }

  return 0;
}

/*
 * liberar_recursos:
 * Cierra los descriptores del pipe nominal, elimina el pipe nominal del
 * sistema de archivos con unlink y destruye los buffers y semaforos de todos
 * los agentes registrados.
 *
 * Recibe:
 * - struct servidor *s: apuntador a la estructura principal del servidor.
 *
 * Variables creadas:
 * - int i: contador para recorrer los agentes registrados en el servidor.
 *
 * Paso a paso:
 * 1. Primero se invalida fd_aviso = -1 y se cierran con close() los
 *    descriptores s->fd_pipe y s->fd_ficticio si estaban abiertos (>= 0).
 * 2. Despues se elimina el pipe nominal del sistema de archivos llamando a
 *    unlink(s->cfg.pipe_nombre).
 * 3. Por ultimo se recorren los s->n_registrados agentes y se libera la
 *    memoria y los 3 semaforos de cada uno llamando a buffer_destruir.
 *
 * Retorna:
 * - Nada (void).
 */
static void liberar_recursos(struct servidor *s) {
  int i;

  fd_aviso = -1;

  if (s->fd_pipe >= 0) {
    close(s->fd_pipe);
    s->fd_pipe = -1;
  }
  if (s->fd_ficticio >= 0) {
    close(s->fd_ficticio);
    s->fd_ficticio = -1;
  }
  if (s->cfg.pipe_nombre != NULL) {
    unlink(s->cfg.pipe_nombre);
  }

  for (i = 0; i < s->n_registrados; i++) {
    buffer_destruir(&s->agentes[i].buffer);
  }
}

/*
 * main:
 * Funcion principal del proceso servidorISE ("Control Proceso Principal ISE").
 * Valida las banderas, crea el pipe nominal, instala el manejador de senales
 * y la alarma de 60 segundos, lanza el hilo recolector, espera con pthread_join
 * a que terminen todos los hilos, emite el reporte general del ISE y limpia
 * todos los recursos temporales.
 *
 * Recibe:
 * - int argc: cantidad de argumentos de la linea de comandos.
 * - char **argv: arreglo con los argumentos de la linea de comandos.
 *
 * Variables creadas:
 * - struct servidor srv: estructura principal que agrupa la configuracion,
 *   los descriptores del pipe y el arreglo de agentes.
 * - pthread_t recolector: identificador del hilo recolector creado con
 *   pthread_create.
 * - int rc: codigo de retorno de pthread_create y de cada pthread_join.
 * - int i: contador para hacer pthread_join de cada hilo consumidor.
 *
 * Paso a paso:
 * 1. Primero se inicializa la estructura srv en cero con memset y se ponen los
 *    descriptores en -1.
 * 2. Despues se leen y validan las banderas -b, -p y -a con parsear_args; si
 *    hay error se muestra el uso y se termina con EXIT_FAILURE.
 * 3. Se crea y abre el pipe nominal llamando a crear_fifo(&srv) y se guarda
 *    srv.fd_ficticio en fd_aviso para el manejador de senales.
 * 4. Se configuran las senales con signal(): se ignora SIGPIPE y se instala
 *    manejador_senal para SIGALRM y SIGINT. Si alguna llamada a signal falla
 *    (retorna SIG_ERR) se avisa, se liberan los recursos y se termina con
 *    EXIT_FAILURE. Luego se solicita la alarma de reloj al kernel con
 *    alarm(TIMEOUT_SERVIDOR_SEG) para fijar el plazo maximo de 60 segundos.
 * 5. Se crea el hilo recolector con pthread_create(&recolector, NULL,
 *    hilo_recolector, &srv). Si falla, se cancela la alarma con alarm(0), se
 *    liberan los recursos y se termina con EXIT_FAILURE.
 * 6. El hilo principal espera a que termine el hilo recolector con
 *    pthread_join(recolector, NULL), cancela cualquier alarma pendiente con
 *    alarm(0) y espera a todos los hilos consumidores registrados con un
 *    ciclo de pthread_join. Si algun pthread_join falla (retorna distinto de
 *    0) se informa el error con strerror.
 * 7. Por ultimo, con todos los hilos ya finalizados, se imprime el resumen
 *    general con imprimir_reporte(&srv), se eliminan los recursos temporales
 *    con liberar_recursos(&srv) y se retorna EXIT_SUCCESS.
 *
 * Retorna:
 * - EXIT_SUCCESS (0) al finalizar correctamente, o EXIT_FAILURE si ocurrio un
 *   error al iniciar.
 */
int main(int argc, char **argv) {
  struct servidor srv;
  pthread_t recolector;
  int rc;
  int i;

  memset(&srv, 0, sizeof(srv));
  srv.fd_pipe = -1;
  srv.fd_ficticio = -1;

  if (parsear_args(argc, argv, &srv.cfg) != 0) {
    imprimir_uso(argv[0]);
    return EXIT_FAILURE;
  }

  if (crear_fifo(&srv) != 0) {
    return EXIT_FAILURE;
  }
  fd_aviso = srv.fd_ficticio;

  if (signal(SIGPIPE, SIG_IGN) == SIG_ERR ||
      signal(SIGALRM, manejador_senal) == SIG_ERR ||
      signal(SIGINT, manejador_senal) == SIG_ERR) {
    aviso("signal");
    liberar_recursos(&srv);
    return EXIT_FAILURE;
  }
  alarm(TIMEOUT_SERVIDOR_SEG);

  printf("Servidor ISE activo. Pipe: %s | Buffer: %d | Agentes: %d | Tiempo "
         "max: %d s\n",
         srv.cfg.pipe_nombre, srv.cfg.tam_buffer, srv.cfg.max_agentes,
         TIMEOUT_SERVIDOR_SEG);
  printf("Esperando conexion de agentes...\n");
  fflush(stdout);

  rc = pthread_create(&recolector, NULL, hilo_recolector, &srv);
  if (rc != 0) {
    fprintf(stderr, "pthread_create (recolector): %s\n", strerror(rc));
    alarm(0);
    liberar_recursos(&srv);
    return EXIT_FAILURE;
  }

  rc = pthread_join(recolector, NULL);
  if (rc != 0) {
    fprintf(stderr, "pthread_join (recolector): %s\n", strerror(rc));
  }
  alarm(0);

  for (i = 0; i < srv.n_registrados; i++) {
    rc = pthread_join(srv.agentes[i].hilo, NULL);
    if (rc != 0) {
      fprintf(stderr, "pthread_join (%s): %s\n", srv.agentes[i].sigla,
              strerror(rc));
    }
  }

  imprimir_reporte(&srv);
  liberar_recursos(&srv);
  return EXIT_SUCCESS;
}
