#ifndef BUFFER_H
#define BUFFER_H

#include <semaphore.h>
#include "protocolo.h"

/*
 * buffer.h:
 * Buffer circular de tamano fijo entre el hilo recolector (productor) y el
 * hilo consumidor de un agente. Es el problema productor/consumidor resuelto
 * con 3 semaforos POSIX. Hay un buffer por cada agente conectado.
 */

struct buffer {
    struct mensaje *datos;     /* arreglo dinamico con los mensajes */
    int             capacidad; /* cuantos mensajes caben (bandera -b) */
    int             entra;     /* posicion donde se guarda el proximo */
    int             sale;      /* posicion de donde se saca el proximo */
    sem_t           vacios;    /* huecos libres; empieza en capacidad */
    sem_t           llenos;    /* mensajes por sacar; empieza en 0 */
    sem_t           mutex;     /* exclusion mutua; empieza en 1 */
};

/* Reserva el arreglo e inicia los 3 semaforos. Retorna 0 o -1 si fallo. */
int  buffer_iniciar(struct buffer *b, int capacidad);

/* Destruye los semaforos y libera el arreglo. Solo se llama cuando ningun
 * hilo esta usando el buffer (despues de los pthread_join). */
void buffer_destruir(struct buffer *b);

/* Guarda una copia de *m. Si el buffer esta lleno, espera a que haya un
 * hueco. Retorna 0 o -1 si fallo un semaforo. */
int  buffer_poner(struct buffer *b, const struct mensaje *m);

/* Saca el mensaje mas antiguo y lo copia en *m. Si el buffer esta vacio,
 * espera a que llegue uno. Retorna 0 o -1 si fallo un semaforo. */
int  buffer_sacar(struct buffer *b, struct mensaje *m);

#endif
