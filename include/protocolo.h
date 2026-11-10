#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include "comun.h"

/*
 * protocolo.h:
 * Formato de los mensajes que los agentes envian al servidor por el pipe
 * nominal, y las dos funciones para enviarlos y recibirlos.
 */

/* Tipo de cada mensaje. Empieza en 1 para que un mensaje lleno de ceros (sin
 * inicializar) no se confunda con un tipo valido. */
enum tipo_mensaje {
    MSG_HELLO = 1, /* el agente se presenta: sigla, id_pais y total a enviar */
    MSG_DATO,      /* una encuesta: id_encuesta, id_sector y sentimiento */
    MSG_FIN,       /* el agente ya envio todas sus encuestas */
    MSG_STOP       /* interno del servidor: orden de parar (60 s o Ctrl+C) */
};

/* Mensaje de tamano fijo. Todos los mensajes ocupan sizeof(struct mensaje)
 * bytes, que es mucho menos que PIPE_BUF, y por eso cada write() al pipe es
 * atomico: aunque varios agentes escriban a la vez, los mensajes no se
 * mezclan por la mitad. No todos los tipos usan todos los campos. */
struct mensaje {
    int    tipo;             /* uno de los valores de enum tipo_mensaje */
    int    id_pais;          /* ID_Pais del agente que lo envia */
    char   sigla[TAM_SIGLA]; /* sigla del pais, por ejemplo "FR" */
    long   total;            /* HELLO y FIN: cantidad total de encuestas */
    int    id_encuesta;      /* DATO: columna ID_Encuesta del CSV */
    int    id_sector;        /* DATO: sector de la encuesta, de 0 a 4 */
    double sentimiento;      /* DATO: valor de la encuesta, de 0 a 100 */
};

/* Escribe un mensaje completo en fd. Retorna 0 si lo envio o -1 si fallo. */
int enviar_mensaje(int fd, const struct mensaje *m);

/* Lee un mensaje completo de fd. Retorna 1 si lo leyo, 0 si el pipe se cerro
 * sin datos (EOF) o -1 si hubo error. */
int recibir_mensaje(int fd, struct mensaje *m);

#endif
