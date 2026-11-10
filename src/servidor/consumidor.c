#include "protocolo.h"
#include "servidor.h"
#include <stdio.h>

/*
 * hilo_consumidor:
 * Funcion que ejecuta el hilo consumidor asignado a un agente de pais. Extrae
 * los mensajes depositados en su buffer acotado siguiendo el esquema clasico
 * productor/consumidor con 3 semaforos y acumula las sumas y cuentas por
 * sector para ese pais.
 *
 * Recibe:
 * - void *arg: apuntador generico que recibe la direccion de la estructura
 *   struct agente_ctx correspondiente a este agente.
 *
 * Variables creadas:
 * - struct agente_ctx *ag: apuntador al contexto del agente obtenido al
 *   convertir arg.
 * - struct mensaje m: estructura local donde buffer_sacar copia cada mensaje
 *   retirado de la cola circular antes de procesarlo fuera de la seccion
 *   critica.
 *
 * Paso a paso:
 * 1. Primero se convierte el apuntador generico arg a struct agente_ctx *ag.
 * 2. Despues se entra en un ciclo while (1) para retirar mensajes de su buffer
 *    llamando a buffer_sacar(&ag->buffer, &m). Si el buffer esta vacio, el
 *    hilo se bloquea en el semaforo de llenos sin hacer espera activa.
 * 3. Si buffer_sacar retorna -1 por algun fallo de semaforo, se marca
 *    ag->completo = 0 y se sale del ciclo.
 * 4. Ya fuera de la seccion critica del buffer se consume el elemento segun su
 *    tipo:
 *    - Si m.tipo es MSG_DATO, se suma el sentimiento al sector correspondiente
 *      con acum_agregar y se incrementa el contador ag->recibidas.
 *    - Si m.tipo es MSG_FIN, se marca ag->completo = 1, se imprime en pantalla
 *      "Recibidos todos los datos del agente <sigla>" y se termina el ciclo.
 *    - Si m.tipo es MSG_STOP (orden de parada enviada por el recolector al
 *      vencer el tiempo de 60 segundos o por interrupcion), se marca
 *      ag->completo = 0 y se termina el ciclo.
 * 5. Por ultimo el hilo termina retornando NULL para que el hilo principal
 *    pueda sincronizarse con pthread_join.
 *
 * Retorna:
 * - NULL al finalizar la ejecucion del hilo.
 */
void *hilo_consumidor(void *arg) {
  struct agente_ctx *ag = (struct agente_ctx *)arg;
  struct mensaje m;

  while (1) {
    if (buffer_sacar(&ag->buffer, &m) != 0) {
      fprintf(stderr, "Error al sacar del buffer del agente %s\n", ag->sigla);
      ag->completo = 0;
      break;
    }

    if (m.tipo == MSG_DATO) {
      if (acum_agregar(&ag->acum, m.id_sector, m.sentimiento) == 0) {
        ag->recibidas++;
      } else {
        fprintf(stderr, "Agente %s: sector invalido (%d) descartado\n",
                ag->sigla, m.id_sector);
      }
    } else if (m.tipo == MSG_FIN) {
      ag->completo = 1;
      printf("Recibidos todos los datos del agente %s\n", ag->sigla);
      fflush(stdout);
      break;
    } else if (m.tipo == MSG_STOP) {
      ag->completo = 0;
      break;
    }
  }

  return NULL;
}
