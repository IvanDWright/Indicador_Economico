#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <sys/types.h>

/*
 * util.h:
 * Funciones de apoyo que usan los tres programas: avisar errores del sistema
 * y leer o escribir una cantidad exacta de bytes en un descriptor.
 */

/* Imprime el error del sistema con perror y termina el programa. */
void fatal(const char *contexto);

/* Imprime el error del sistema con perror, pero no termina el programa. */
void aviso(const char *contexto);

/* Lee exactamente n bytes, repitiendo read() si hace falta. Retorna n, 0 si
 * el otro extremo se cerro antes de leer nada (EOF), o -1 si hubo error. */
ssize_t leer_completo(int fd, void *buf, size_t n);

/* Escribe exactamente n bytes, repitiendo write() si hace falta. Retorna n,
 * o -1 si hubo error. */
ssize_t escribir_completo(int fd, const void *buf, size_t n);

#endif
