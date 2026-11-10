# Documentación Técnica del Proyecto ISE

Esta carpeta reúne la explicación detallada del diseño y funcionamiento de todos los módulos del proyecto para consulta del equipo y preparación de la sustentación.

## Archivos en esta carpeta

- **[diseno.md](diseno.md):** Documento completo de diseño del sistema. Explica:
  1. **Arquitectura general:** Cómo se conectan `generadorCSV`, `agentePais` y `servidorISE`.
  2. **Procesos e hilos:** Qué hace el proceso principal del servidor, el hilo recolector y cada hilo consumidor.
  3. **Detalle de cada módulo:** Qué estructuras y variables crea cada archivo `.c`/`.h` y cómo valida los datos.
  4. **Protocolo sobre el pipe nominal:** Por qué se usa `struct mensaje` de tamaño fijo (`MSG_HELLO`, `MSG_DATO`, `MSG_FIN`, `MSG_STOP`) para lograr escrituras atómicas en el FIFO.
  5. **Sincronización con semáforos POSIX:** Cómo funciona la cola circular con los 3 semáforos (`vacios`, `llenos`, `mutex`), cuál es el protocolo de entrada y de salida de la sección crítica y por qué el orden de espera evita el interbloqueo (*deadlock*).
  6. **Señales y alarma de 60 segundos:** Cómo se usan `signal(SIGALRM, ...)`, `signal(SIGINT, ...)`, `signal(SIGPIPE, SIG_IGN)` y `alarm(60)` junto con el descriptor de escritura auxiliar `fd_ficticio` para detener la recolección sin espera ocupada.
  7. **Fórmulas del ISE:** Cómo se acumulan las sumas y cuentas por sector, cómo se calcula el promedio ponderado de cada país y cómo se clasifica el resultado en `Vender`, `Comprar` o `Neutro`.

## Convención de comentarios en el código fuente

Todas las funciones en `src/` están documentadas arriba de su definición con una misma estructura uniforme para facilitar su lectura y defensa:

1. **Descripción general:** Qué hace la función dentro de su módulo.
2. **`Recibe:`** Lista de parámetros recibidos y qué representa cada uno.
3. **`Variables creadas:`** Lista de todas las variables locales declaradas al inicio de la función y para qué se usa cada una.
4. **`Paso a paso:`** Explicación numerada en orden cronológico (`1. Primero...`, `2. Despues...`) de todas las validaciones y operaciones que ejecuta la función.
5. **`Retorna:`** Qué devuelve la función cuando termina con éxito o cuando ocurre un error.
