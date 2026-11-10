#include "buffer.h"
#include <errno.h>
#include <stdlib.h>

/*
 * esperar:
 * Espera a que un semaforo sea mayor a 0 y le resta 1 (operacion wait).
 *
 * Recibe:
 * - sem_t *s: apuntador al semaforo sobre el cual se va a esperar.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero ejecuta sem_wait(s) dentro de la condicion del while. Si el
 *    semaforo es mayor a 0 le resta 1 y devuelve 0; si es 0 duerme al hilo.
 * 2. Si sem_wait devuelve -1, revisa si errno == EINTR (fue interrumpido por
 *    una senal); si es asi, repite el sem_wait con continue.
 * 3. Si fue otro error distinto de EINTR, sale de inmediato retornando -1.
 *
 * Retorna:
 * - 0 si logro tomar el semaforo, o -1 si ocurrio un error real.
 */
static int esperar(sem_t *s) {
  while (sem_wait(s) == -1) {
    if (errno == EINTR) {
      continue;
    }
    return -1;
  }
  return 0;
}

/*
 * buffer_iniciar:
 * Inicializa un buffer circular y sus semaforos (actua como constructor).
 *
 * Recibe:
 * - struct buffer *b: apuntador a la estructura buffer que se quiere
 *   inicializar.
 * - int capacidad: cantidad maxima de mensajes que caben en el buffer (-b).
 *
 * Variables creadas:
 * - Ninguna (inicializa los campos internos de la estructura *b).
 *
 * Paso a paso:
 * 1. Primero se revisa que el apuntador b no sea NULL y que capacidad > 0.
 * 2. Con malloc (memory allocation) se reserva en RAM el arreglo b->datos:
 *    (struct mensaje *)malloc((size_t)capacidad * sizeof(struct mensaje)).
 *    Se castea al tipo de apuntador deseado multiplicando la cantidad de
 *    mensajes por el tamano en bytes de cada mensaje. Si devuelve NULL,
 *    retorna -1.
 * 3. Se guarda la capacidad y se ponen los indices entra y sale en 0.
 * 4. Despues se inicializan los 3 semaforos con sem_init usando 0 en el
 *    centro porque solo se comparten entre hilos del mismo proceso:
 *    - vacios = capacidad (cuenta los huecos libres para escribir).
 *    - llenos = 0 (cuenta los mensajes listos para leer/sacar).
 *    - mutex  = 1 (candado de exclusion mutua para que solo 1 hilo entre).
 * 5. Si falla la creacion de algun semaforo, se destruyen los ya creados con
 *    sem_destroy, se libera b->datos con free, se pone en NULL y retorna -1.
 *
 * Retorna:
 * - 0 si el buffer se inicializo correctamente, o -1 si hubo error.
 */
int buffer_iniciar(struct buffer *b, int capacidad) {
  if (b == NULL || capacidad <= 0) {
    return -1;
  }
  b->datos =
      (struct mensaje *)malloc((size_t)capacidad * sizeof(struct mensaje));
  if (b->datos == NULL) {
    return -1;
  }
  b->capacidad = capacidad;
  b->entra = 0;
  b->sale = 0;

  if (sem_init(&b->vacios, 0, (unsigned int)capacidad) == -1) {
    free(b->datos);
    b->datos = NULL;
    return -1;
  }
  if (sem_init(&b->llenos, 0, 0) == -1) {
    sem_destroy(&b->vacios);
    free(b->datos);
    b->datos = NULL;
    return -1;
  }
  if (sem_init(&b->mutex, 0, 1) == -1) {
    sem_destroy(&b->llenos);
    sem_destroy(&b->vacios);
    free(b->datos);
    b->datos = NULL;
    return -1;
  }
  return 0;
}

/*
 * buffer_destruir:
 * Destruye los semaforos y libera la memoria del buffer (destructor).
 *
 * Recibe:
 * - struct buffer *b: apuntador al buffer que se va a liberar.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero se revisa si b o b->datos son NULL; si es asi, retorna sin
 *    hacer nada.
 * 2. Destruye los tres semaforos (mutex, llenos y vacios) con sem_destroy.
 * 3. Despues libera el arreglo dinamico con free(b->datos) y deja b->datos en
 *    NULL: asi llamarla otra vez (o sobre un buffer cuyo buffer_iniciar
 *    fallo) no hace un doble free. Solo se llama despues de los join, cuando
 *    ningun hilo usa el buffer. sem_destroy no se valida: es limpieza y no
 *    hay nada mas que hacer si falla.
 *
 * Retorna:
 * - Nada (void).
 */
void buffer_destruir(struct buffer *b) {
  if (b == NULL || b->datos == NULL) {
    return;
  }
  sem_destroy(&b->mutex);
  sem_destroy(&b->llenos);
  sem_destroy(&b->vacios);
  free(b->datos);
  b->datos = NULL;
}

/*
 * buffer_poner:
 * Agrega un mensaje al buffer circular (lado productor).
 *
 * Recibe:
 * - struct buffer *b: apuntador al buffer donde se va a guardar el mensaje.
 * - const struct mensaje *m: apuntador al mensaje que se quiere copiar dentro
 *   del buffer.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero se revisa vacios con esperar(&b->vacios) para esperar a que haya
 *    al menos un hueco libre en el buffer. Si falla, retorna -1.
 * 2. Despues se revisa mutex con esperar(&b->mutex) para esperar a que ningun
 *    otro hilo este usando el buffer. Si falla, devuelve el hueco con
 *    sem_post(&b->vacios) y retorna -1.
 * 3. Se copia el mensaje *m en b->datos[b->entra] y se avanza el indice entra
 *    de forma circular con (b->entra + 1) % b->capacidad.
 * 4. Al final se suelta el candado con sem_post(&b->mutex) y se suma 1 a
 *    llenos con sem_post(&b->llenos) para avisar al consumidor. Si alguno de
 *    los dos sem_post falla retorna -1 (el buffer ya no es confiable).
 *
 * Retorna:
 * - 0 si agrego el mensaje con exito, o -1 si hubo error.
 */
int buffer_poner(struct buffer *b, const struct mensaje *m) {
  if (esperar(&b->vacios) == -1) {
    return -1;
  }
  if (esperar(&b->mutex) == -1) {
    sem_post(&b->vacios);
    return -1;
  }

  b->datos[b->entra] = *m;
  b->entra = (b->entra + 1) % b->capacidad;

  if (sem_post(&b->mutex) == -1) {
    return -1;
  }
  if (sem_post(&b->llenos) == -1) {
    return -1;
  }
  return 0;
}

/*
 * buffer_sacar:
 * Saca el mensaje mas antiguo del buffer circular (lado consumidor).
 *
 * Recibe:
 * - struct buffer *b: apuntador al buffer de donde se va a sacar el mensaje.
 * - struct mensaje *m: apuntador a la estructura mensaje donde se copiara el
 *   dato leido.
 *
 * Variables creadas:
 * - Ninguna.
 *
 * Paso a paso:
 * 1. Primero se revisa llenos con esperar(&b->llenos) para esperar a que haya
 *    al menos un mensaje disponible para leer/sacar. Si falla, retorna -1.
 * 2. Despues se revisa mutex con esperar(&b->mutex) para esperar a que ningun
 *    otro hilo este usando el buffer. Si falla, devuelve el permiso con
 *    sem_post(&b->llenos) y retorna -1.
 * 3. Se copia el mensaje de b->datos[b->sale] en *m y se avanza el indice
 *    sale de forma circular con (b->sale + 1) % b->capacidad.
 * 4. Al final se suelta el candado con sem_post(&b->mutex) y se suma 1 a
 *    vacios con sem_post(&b->vacios) para avisar al productor que hay
 *    espacio. Si alguno de los dos sem_post falla retorna -1 (el buffer ya no
 *    es confiable).
 *
 * Retorna:
 * - 0 si saco el mensaje con exito, o -1 si hubo error.
 */
int buffer_sacar(struct buffer *b, struct mensaje *m) {
  if (esperar(&b->llenos) == -1) {
    return -1;
  }
  if (esperar(&b->mutex) == -1) {
    sem_post(&b->llenos);
    return -1;
  }

  *m = b->datos[b->sale];
  b->sale = (b->sale + 1) % b->capacidad;

  if (sem_post(&b->mutex) == -1) {
    return -1;
  }
  if (sem_post(&b->vacios) == -1) {
    return -1;
  }
  return 0;
}
