#ifndef SERVIDOR_H
#define SERVIDOR_H

#include <pthread.h>
#include "comun.h"
#include "buffer.h"
#include "ise.h"

/*
 * servidor.h:
 * Estructuras del servidorISE y las funciones de sus hilos. En cada campo se
 * anota que hilo lo escribe: lo unico que comparten dos hilos a la vez es el
 * buffer, y por eso es lo unico protegido con semaforos.
 */

/* Valores leidos de la linea de comandos. Los escribe main antes de crear
 * los hilos y despues nadie los cambia. */
struct config_servidor {
    int         tam_buffer;  /* bandera -b: mensajes que caben por buffer */
    const char *pipe_nombre; /* bandera -p: nombre del pipe nominal */
    int         max_agentes; /* bandera -a: agentes habilitados (1 a 4) */
};

/* Estado de una casilla de agente. Lo escribe solo el hilo recolector. */
enum estado_agente {
    AG_LIBRE,    /* casilla sin usar */
    AG_ACTIVO,   /* agente registrado que todavia no envio MSG_FIN */
    AG_TERMINADO /* ya envio MSG_FIN, o el servidor le ordeno parar */
};

/* Todo lo que el servidor guarda de un agente. */
struct agente_ctx {
    /* Los escribe el recolector al registrar al agente, antes de crear su
     * hilo consumidor. */
    enum estado_agente estado;
    char               sigla[TAM_SIGLA];
    int                id_pais;
    long               esperadas; /* total anunciado en el MSG_HELLO */

    /* Compartido: el recolector pone y el consumidor saca. Lo protegen sus 3
     * semaforos. */
    struct buffer      buffer;

    /* Identificador del hilo consumidor, para el pthread_join de main. */
    pthread_t          hilo;

    /* El recolector los deja en cero al registrar al agente. Despues los
     * escribe solo el hilo consumidor de este agente, y main los lee tras el
     * pthread_join, cuando ese hilo ya termino. */
    struct acum_pais   acum;      /* sumas y cuentas por sector */
    long               recibidas; /* encuestas validas procesadas */
    int                completo;  /* 1 si llego el MSG_FIN, 0 si no */
};

/* Estado general del servidor. */
struct servidor {
    struct config_servidor cfg;
    int                    fd_pipe;     /* lectura del pipe (recolector) */
    int                    fd_ficticio; /* escritura propia del pipe */
    struct agente_ctx      agentes[MAX_AGENTES];
    int                    n_registrados; /* lo escribe solo el recolector */
};

/* Hilo unico que lee el pipe nominal y reparte cada mensaje al buffer de su
 * agente. Recibe un struct servidor *. */
void *hilo_recolector(void *arg);

/* Hilo que atiende a un agente: saca los mensajes de su buffer y acumula.
 * Hay uno por agente. Recibe un struct agente_ctx *. */
void *hilo_consumidor(void *arg);

/* Imprime el resumen final. Se llama cuando todos los hilos ya terminaron. */
void imprimir_reporte(const struct servidor *s);

#endif
