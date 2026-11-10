#include "paises.h"
#include "protocolo.h"
#include "servidor.h"
#include "util.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/*
 * buscar_agente:
 * Busca en el arreglo de agentes registrados del servidor aquel cuyo id_pais
 * coincida con el identificador recibido.
 *
 * Recibe:
 * - struct servidor *s: apuntador a la estructura principal del servidor.
 * - int id_pais: identificador numerico del pais que se quiere localizar.
 *
 * Variables creadas:
 * - int i: contador para recorrer las casillas de agentes ya registrados
 *   desde 0 hasta s->n_registrados - 1.
 *
 * Paso a paso:
 * 1. Primero se recorren con un ciclo for unicamente los agentes que ya se
 *    han registrado en el servidor (s->n_registrados).
 * 2. En cada posicion se compara s->agentes[i].id_pais con id_pais; si son
 *    iguales, se retorna la direccion de esa casilla (&s->agentes[i]).
 * 3. Si termina el ciclo sin encontrar coincidencia, se retorna NULL.
 *
 * Retorna:
 * - Apuntador al struct agente_ctx del pais encontrado, o NULL si no esta
 *   registrado.
 */
static struct agente_ctx *buscar_agente(struct servidor *s, int id_pais) {
  int i;

  for (i = 0; i < s->n_registrados; i++) {
    if (s->agentes[i].id_pais == id_pais) {
      return &s->agentes[i];
    }
  }
  return NULL;
}

/*
 * registrar_agente:
 * Procesa un mensaje MSG_HELLO enviado por un nuevo agente, inicializa su
 * acumulador y su buffer circular con semaforos, y crea su hilo consumidor
 * dedicado.
 *
 * Recibe:
 * - struct servidor *s: apuntador a la estructura principal del servidor.
 * - const struct mensaje *m: apuntador al mensaje MSG_HELLO recibido por el
 *   pipe nominal.
 *
 * Variables creadas:
 * - struct agente_ctx *ag: apuntador a la casilla libre del arreglo de agentes
 *   que se va a configurar.
 * - int rc: codigo de retorno de pthread_create para verificar si el hilo se
 *   creo correctamente.
 *
 * Paso a paso:
 * 1. Primero se verifica que el pais exista en la tabla con pais_por_id y que
 *    el total anunciado sea mayor que 0; si no, se descarta con aviso.
 * 2. Despues se comprueba con buscar_agente que ese pais no se haya registrado
 *    antes y que aun no se haya alcanzado el limite de agentes habilitados
 *    (s->cfg.max_agentes).
 * 3. Se toma la casilla libre &s->agentes[s->n_registrados] y se guardan su
 *    sigla, id_pais, total de encuestas esperadas, recibidas = 0, completo = 0
 *    y estado = AG_ACTIVO.
 * 4. Se limpia el acumulador de sectores del pais con acum_iniciar(&ag->acum)
 *    y se inicializa su buffer acotado y sus 3 semaforos con buffer_iniciar.
 * 5. Se lanza el hilo consumidor de este agente con pthread_create pasandole
 *    la direccion de su casilla ag. Si falla la creacion del hilo, se destruye
 *    el buffer recien creado, se devuelve el estado a AG_LIBRE y se retorna
 *    NULL.
 * 6. Por ultimo se incrementa s->n_registrados, se imprime en pantalla que el
 *    agente se conecto y se retorna el apuntador ag.
 *
 * Retorna:
 * - Apuntador a la casilla del agente registrado, o NULL si fue rechazado o
 *   fallo la reserva de recursos.
 */
static struct agente_ctx *registrar_agente(struct servidor *s,
                                           const struct mensaje *m) {
  struct agente_ctx *ag;
  int rc;

  if (pais_por_id(m->id_pais) == NULL || m->total <= 0) {
    fprintf(stderr, "HELLO invalido descartado (id_pais=%d, total=%ld)\n",
            m->id_pais, m->total);
    return NULL;
  }

  if (buscar_agente(s, m->id_pais) != NULL) {
    fprintf(stderr, "Agente %s (id=%d) ya estaba registrado\n", m->sigla,
            m->id_pais);
    return NULL;
  }

  if (s->n_registrados >= s->cfg.max_agentes) {
    fprintf(stderr,
            "Agente %s rechazado: ya se alcanzo el maximo de %d agentes\n",
            m->sigla, s->cfg.max_agentes);
    return NULL;
  }

  ag = &s->agentes[s->n_registrados];
  strncpy(ag->sigla, m->sigla, TAM_SIGLA - 1);
  ag->sigla[TAM_SIGLA - 1] = '\0';
  ag->id_pais = m->id_pais;
  ag->esperadas = m->total;
  ag->recibidas = 0;
  ag->completo = 0;
  ag->estado = AG_ACTIVO;

  acum_iniciar(&ag->acum);

  if (buffer_iniciar(&ag->buffer, s->cfg.tam_buffer) != 0) {
    fprintf(stderr, "No se pudo iniciar el buffer para el agente %s\n",
            ag->sigla);
    ag->estado = AG_LIBRE;
    return NULL;
  }

  rc = pthread_create(&ag->hilo, NULL, hilo_consumidor, ag);
  if (rc != 0) {
    fprintf(stderr, "pthread_create (%s): %s\n", ag->sigla, strerror(rc));
    buffer_destruir(&ag->buffer);
    ag->estado = AG_LIBRE;
    return NULL;
  }

  s->n_registrados++;
  printf("Agente %s conectado (esperadas: %ld)\n", ag->sigla, ag->esperadas);
  fflush(stdout);
  return ag;
}

/*
 * cerrar_consumidores:
 * Envia un mensaje interno MSG_STOP al buffer de todos los agentes que sigan
 * en estado AG_ACTIVO cuando el recolector termina, para despertar a sus hilos
 * consumidores y evitar que queden bloqueados indefinidamente.
 *
 * Recibe:
 * - struct servidor *s: apuntador a la estructura principal del servidor.
 *
 * Variables creadas:
 * - struct mensaje m_stop: mensaje con tipo MSG_STOP para indicar parada al
 *   consumidor.
 * - int i: contador para recorrer los agentes registrados.
 *
 * Paso a paso:
 * 1. Primero se limpia la estructura m_stop con memset en 0 y se le asigna el
 *    tipo MSG_STOP.
 * 2. Despues se recorren los s->n_registrados agentes con un ciclo for.
 * 3. Para cada agente cuyo estado siga siendo AG_ACTIVO (porque no alcanzo a
 *    enviar MSG_FIN antes de cumplirse el tiempo o la interrupcion), se
 *    inserta m_stop en su buffer con buffer_poner y se cambia su estado a
 *    AG_TERMINADO.
 *
 * Retorna:
 * - Nada (void).
 */
static void cerrar_consumidores(struct servidor *s) {
  struct mensaje m_stop;
  int i;

  memset(&m_stop, 0, sizeof(m_stop));
  m_stop.tipo = MSG_STOP;

  for (i = 0; i < s->n_registrados; i++) {
    if (s->agentes[i].estado == AG_ACTIVO) {
      if (buffer_poner(&s->agentes[i].buffer, &m_stop) != 0) {
        fprintf(stderr, "No se pudo enviar MSG_STOP al consumidor %s\n",
                s->agentes[i].sigla);
      }
      s->agentes[i].estado = AG_TERMINADO;
    }
  }
}

/*
 * hilo_recolector:
 * Funcion que ejecuta el hilo recolector del servidor. Es el unico lector del
 * pipe nominal y actua como productor de los buffers circulares de cada agente:
 * lee cada mensaje del pipe, identifica a que pais pertenece y lo deposita en
 * el buffer del agente correspondiente hasta que todos los agentes habilitados
 * envien MSG_FIN o se cumpla el tiempo maximo de 60 segundos.
 *
 * Recibe:
 * - void *arg: apuntador generico que recibe la direccion de struct servidor.
 *
 * Variables creadas:
 * - struct servidor *s: apuntador a la estructura del servidor obtenido al
 *   convertir arg.
 * - struct mensaje m: estructura local donde recibir_mensaje deposita cada
 *   mensaje leido del pipe nominal.
 * - struct agente_ctx *ag: apuntador a la casilla del agente al que pertenece
 *   el mensaje actual.
 * - int terminados: contador de cuantos agentes ya enviaron su mensaje MSG_FIN.
 * - long descartados: contador de mensajes MSG_DATO o MSG_FIN que llegaron de
 *   un agente que no esta registrado o que ya habia terminado.
 * - int r: valor de retorno de recibir_mensaje (1 mensaje leido, 0 fin de
 *   entrada, -1 error).
 *
 * Paso a paso:
 * 1. Primero se convierte el argumento generico arg a struct servidor *s y se
 *    inicia el contador terminados en 0.
 * 2. Mientras terminados sea menor que la cantidad de agentes habilitados
 *    (s->cfg.max_agentes), se lee el siguiente mensaje del pipe nominal con
 *    recibir_mensaje(s->fd_pipe, &m). Cuando el pipe esta vacio, la llamada
 *    read() suspende al hilo sin espera ocupada hasta que llegue un mensaje.
 * 3. Si recibir_mensaje devuelve 0 (fin de entrada) o -1 (error de lectura),
 *    se sale del ciclo.
 * 4. Segun el campo m.tipo del mensaje recibido:
 *    - Si es MSG_HELLO: se da de alta al agente y su hilo consumidor llamando
 *      a registrar_agente(s, &m).
 *    - Si es MSG_DATO: se busca al agente con buscar_agente(s, m.id_pais) y,
 *      si esta activo, se deposita la encuesta en su buffer con buffer_poner.
 *    - Si es MSG_FIN: se busca al agente y, si esta activo, se deposita el
 *      MSG_FIN en su buffer con buffer_poner, se marca su estado como
 *      AG_TERMINADO y se incrementa terminados.
 *    - Si es MSG_STOP (enviado por el manejador de la alarma de 60 segundos o
 *      Ctrl+C a traves del pipe): se informa que finalizo el tiempo de
 *      recepcion y se sale del ciclo.
 *    Si un MSG_DATO o MSG_FIN llega de un agente que no esta registrado o que
 *    ya termino (por ejemplo, uno rechazado por superar el maximo de -a), no
 *    se imprime nada en ese momento: solo se suma 1 a descartados. Un agente
 *    rechazado igual envia todas sus encuestas, y avisar por cada una
 *    llenaria la pantalla con miles de lineas.
 * 5. Al salir del ciclo, si descartados es mayor que 0 se informa una sola vez
 *    cuantos mensajes se descartaron. Despues se llama a cerrar_consumidores(s)
 *    para despertar a cualquier consumidor que aun espere datos y se retorna
 *    NULL.
 *
 * Retorna:
 * - NULL al finalizar la ejecucion del hilo recolector.
 */
void *hilo_recolector(void *arg) {
  struct servidor *s = (struct servidor *)arg;
  struct mensaje m;
  struct agente_ctx *ag;
  int terminados = 0;
  long descartados = 0;
  int r;

  while (terminados < s->cfg.max_agentes) {
    r = recibir_mensaje(s->fd_pipe, &m);
    if (r == 0) {
      break;
    }
    if (r < 0) {
      aviso("recibir_mensaje (pipe nominal)");
      break;
    }

    if (m.tipo == MSG_HELLO) {
      registrar_agente(s, &m);
    } else if (m.tipo == MSG_DATO) {
      ag = buscar_agente(s, m.id_pais);
      if (ag == NULL || ag->estado != AG_ACTIVO) {
        descartados++;
      } else {
        if (buffer_poner(&ag->buffer, &m) != 0) {
          fprintf(stderr, "Error al insertar dato en buffer de %s\n",
                  ag->sigla);
        }
      }
    } else if (m.tipo == MSG_FIN) {
      ag = buscar_agente(s, m.id_pais);
      if (ag == NULL || ag->estado != AG_ACTIVO) {
        descartados++;
      } else {
        if (buffer_poner(&ag->buffer, &m) != 0) {
          fprintf(stderr, "Error al insertar MSG_FIN en buffer de %s\n",
                  ag->sigla);
        }
        ag->estado = AG_TERMINADO;
        terminados++;
      }
    } else if (m.tipo == MSG_STOP) {
      printf("Tiempo de espera finalizado. Deteniendo recoleccion...\n");
      fflush(stdout);
      break;
    } else {
      fprintf(stderr, "Mensaje de tipo desconocido (%d) ignorado\n", m.tipo);
    }
  }

  if (descartados > 0) {
    fprintf(stderr,
            "Se descartaron %ld mensajes de agentes no registrados o ya "
            "terminados\n",
            descartados);
  }

  cerrar_consumidores(s);
  return NULL;
}
