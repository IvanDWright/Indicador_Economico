#include "util.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * fatal:
 * Informa un error irrecuperable del sistema y termina el programa.
 *
 * Recibe:
 * - const char *contexto: texto que indica que operacion fallo (ej: "mkfifo").
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero imprime el mensaje de error del sistema en stderr con perror.
 * 2. Despues termina todo el proceso inmediatamente con exit(EXIT_FAILURE).
 *
 * Retorna:
 * - void (no retorna porque cierra el programa).
 */
void fatal(const char *contexto) {
  perror(contexto);
  exit(EXIT_FAILURE);
}

/*
 * aviso:
 * Informa un error por consola pero sin terminar el programa (util en hilos).
 *
 * Recibe:
 * - const char *contexto: texto que indica que operacion fallo.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Imprime la descripcion del error actual de errno con perror(contexto) y
 *    permite que el hilo o programa continue para limpiar recursos.
 *
 * Retorna:
 * - Nada (void).
 */
void aviso(const char *contexto) { perror(contexto); }

/*
 * leer_completo:
 * Lee exactamente n bytes de un descriptor de archivo (como el pipe nominal).
 *
 * Recibe:
 * - int fd: descriptor entero del archivo o pipe abierto para lectura.
 * - void *buf: apuntador generico (void *) donde se guardaran los bytes leidos.
 * - size_t n: cantidad exacta de bytes que se deben leer.
 *
 * Variables creadas:
 * - size_t total: acumulador de cuantos bytes se han leido hasta el momento.
 * - char *p: apuntador casteado a char * para avanzar de byte en byte en buf.
 * - ssize_t r: guarda el resultado de cada llamada a read().
 *
 * Paso a paso:
 * 1. Primero se inicializa total en 0 y se convierte buf a char *p.
 * 2. En un ciclo while (total < n) se llama a read(fd, p + total, n - total)
 *    para pedir los bytes que faltan y guardarlos desde la posicion p + total.
 * 3. Si r > 0, se suman los bytes leidos a total.
 * 4. Si r == 0 (EOF: se cerro el pipe), retorna 0 si no se habia leido nada
 *    (total == 0) o -1 si se corto a mitad de un mensaje.
 * 5. Si r < 0, revisa si errno == EINTR para reintentar con continue; si es
 *    otro error, retorna -1.
 *
 * Retorna:
 * - n si leyo todos los bytes, 0 si hubo EOF limpio al inicio, o -1 si fallo.
 */
ssize_t leer_completo(int fd, void *buf, size_t n) {
  size_t total = 0;
  char *p = (char *)buf;
  ssize_t r;

  while (total < n) {
    r = read(fd, p + total, n - total);
    if (r > 0) {
      total += (size_t)r;
    } else if (r == 0) {
      return (total == 0) ? 0 : -1;
    } else {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
  }
  return (ssize_t)total;
}

/*
 * escribir_completo:
 * Escribe exactamente n bytes en un descriptor de archivo (como el pipe).
 *
 * Recibe:
 * - int fd: descriptor entero del archivo o pipe abierto para escritura.
 * - const void *buf: apuntador generico (const void *) con los datos que se van
 *   a enviar.
 * - size_t n: cantidad exacta de bytes que se deben escribir.
 *
 * Variables creadas:
 * - size_t total: acumulador de cuantos bytes ya se han escrito.
 * - const char *p: apuntador casteado a char * para avanzar byte por byte.
 * - ssize_t w: guarda el resultado de cada llamada a write().
 *
 * Paso a paso:
 * 1. Primero se inicializa total en 0 y se convierte buf a const char *p.
 * 2. En un ciclo while (total < n) se llama a write(fd, p + total, n - total)
 *    para enviar los bytes restantes.
 * 3. Si w > 0, se suman los bytes escritos a total.
 * 4. Si w <= 0, se revisa si fue una interrupcion temporal (errno == EINTR)
 *    para reintentar con continue; si no, retorna -1.
 *
 * Retorna:
 * - n si escribio todos los bytes correctamente, o -1 si hubo error.
 */
ssize_t escribir_completo(int fd, const void *buf, size_t n) {
  size_t total = 0;
  const char *p = (const char *)buf;
  ssize_t w;

  while (total < n) {
    w = write(fd, p + total, n - total);
    if (w > 0) {
      total += (size_t)w;
    } else {
      if (w < 0 && errno == EINTR) {
        continue;
      }
      return -1;
    }
  }
  return (ssize_t)total;
}
