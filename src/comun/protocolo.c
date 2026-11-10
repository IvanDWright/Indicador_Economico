#include "protocolo.h"
#include "util.h"

/*
 * enviar_mensaje:
 * Envia una estructura mensaje completa a traves del descriptor del pipe.
 *
 * Recibe:
 * - int fd: descriptor del pipe nominal abierto para escritura.
 * - const struct mensaje *m: apuntador al struct mensaje ya lleno que se desea
 *   enviar.
 *
 * Variables creadas:
 * - ssize_t w: guarda la cantidad de bytes escritos por escribir_completo.
 *
 * Paso a paso:
 * 1. Primero llama a escribir_completo(fd, m, sizeof *m) para enviar todos
 *    los bytes que ocupa la estructura mensaje.
 * 2. Despues revisa si la cantidad escrita w es exactamente igual a sizeof *m;
 *    si es asi retorna 0, y si no retorna -1.
 *
 * Retorna:
 * - 0 si el mensaje completo se envio con exito, o -1 si hubo error.
 */
int enviar_mensaje(int fd, const struct mensaje *m) {
  ssize_t w = escribir_completo(fd, m, sizeof *m);
  if (w == (ssize_t)(sizeof *m)) {
    return 0;
  }
  return -1;
}

/*
 * recibir_mensaje:
 * Recibe una estructura mensaje completa desde el descriptor del pipe.
 *
 * Recibe:
 * - int fd: descriptor del pipe nominal abierto para lectura.
 * - struct mensaje *m: apuntador al struct mensaje donde se guardaran los bytes
 *   recibidos.
 *
 * Variables creadas:
 * - ssize_t r: guarda la cantidad de bytes leidos por leer_completo.
 *
 * Paso a paso:
 * 1. Primero llama a leer_completo(fd, m, sizeof *m) para leer del pipe el
 *    tamano exacto de un struct mensaje y depositarlo en m.
 * 2. Despues revisa el valor de r: si es igual a sizeof *m retorna 1 (mensaje
 *    leido); si es 0 retorna 0 (EOF limpio); en otro caso retorna -1 (error).
 *
 * Retorna:
 * - 1 si leyo un mensaje completo, 0 si hubo EOF limpio, o -1 si hubo error.
 */
int recibir_mensaje(int fd, struct mensaje *m) {
  ssize_t r = leer_completo(fd, m, sizeof *m);
  if (r == (ssize_t)(sizeof *m)) {
    return 1;
  }
  if (r == 0) {
    return 0;
  }
  return -1;
}
