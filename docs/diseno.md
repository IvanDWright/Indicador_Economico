# Diseño del sistema ISE

Este documento es el mapa del proyecto. Cada archivo `.h`/`.c` repite su parte
en comentarios. Cuando cambien una decisión, actualicen **primero** este archivo.

## 1. Visión general

```
 generadorCSV                agentePais (1 por país, 1 terminal c/u)         servidorISE
 ------------                ---------------------------------------         -----------
 -p FR -c 25  ──> encuestas_FR.csv ──> leer_csv ──> HELLO ─┐
                                                    DATO ×N ├─> [ FIFO ise_fifo ] ──> hilo recolector
                                                    FIN   ──┘      (un solo pipe,          │  reparte por país
                                                                    varios escritores)     ▼
                                                                  buffer[0]  buffer[1] ... buffer[3]
                                                                     │          │             │
                                                                  consumidor consumidor    consumidor   (1 hilo por agente)
                                                                     │          │             │
                                                                  acum[0]    acum[1]  ...  acum[3]
                                                                                   │
                                                              hilo principal (main): join + reporte final
```

Tres ejecutables, un solo pipe nominal y una sola dirección de datos
(agente → servidor).

## 2. Procesos e hilos

| Quién | Cuántos | Qué hace |
|---|---|---|
| `generadorCSV` | proceso corto | Crea `encuestas_XX.csv`. No habla con nadie. |
| `agentePais` | 1 por país, terminal propia | Lee el CSV completo a memoria y lo envía por el FIFO. |
| `servidorISE` hilo **main** | 1 | Lee flags, crea el FIFO, instala las señales y la alarma de 60 s, lanza el recolector, espera (`join`), imprime el reporte, limpia. |
| Hilo **recolector** | 1 | Único que lee el FIFO. Reparte cada mensaje al buffer del agente que corresponde. Termina cuando todos los agentes de `-a` mandaron `FIN` o cuando lee un `MSG_STOP`. |
| Hilo **consumidor** | 1 por agente (máx. 4) | Saca mensajes de *su* buffer y acumula sumas y cuentas por sector. |

Por qué un recolector y no que cada consumidor lea el FIFO: todos los agentes
escriben en el **mismo** pipe, así que los mensajes llegan mezclados. Alguien
tiene que leer y separar por país. Ese alguien es el recolector.

## 3. Módulos (qué archivo hace qué)

| Archivo | Responsabilidad | Lo usan |
|---|---|---|
| `include/comun.h` | Constantes (60 s, 4 agentes, umbrales 45/55, 5 sectores). | todos |
| `src/comun/util.c` | Errores y E/S robusta (`leer_completo`, `escribir_completo`). | todos |
| `src/comun/paises.c` | Tabla de países, nombres y pesos de sector, máximos de la Tabla 1. | generador, agente, servidor |
| `src/comun/protocolo.c` | Enviar/recibir un `struct mensaje` por un descriptor. | agente, servidor |
| `src/generador/main.c` | Flags `-p -c`, validación, orquestación. | generador |
| `src/generador/generador.c` | Repartir encuestas entre sectores y escribir el CSV. | generador |
| `src/agente/main.c` | Flags `-n -p`, conexión, envío, progreso. | agente |
| `src/agente/lector_csv.c` | CSV → arreglo dinámico de encuestas. | agente |
| `src/servidor/main.c` | Flags `-b -p -a`, FIFO, ciclo de vida, limpieza. | servidor |
| `src/servidor/recolector.c` | Hilo que lee el FIFO y reparte. | servidor |
| `src/servidor/consumidor.c` | Hilo que procesa el buffer de un agente. | servidor |
| `src/servidor/buffer.c` | Buffer circular + 3 semáforos (productor/consumidor). | servidor |
| `src/servidor/ise.c` | Cálculo puro: promedios, ponderación, clasificación. | servidor |
| `src/servidor/reporte.c` | Imprimir el resumen final. | servidor |

### 3.1 `paises`: datos fijos del enunciado

- **Qué guarda:** los nombres de sector y sus pesos (pág. 1 del enunciado) y, por
  país, la sigla, el `ID_Pais` y los máximos de la Tabla 1 (pág. 2). En cada fila
  los cinco máximos por sector suman `max_encuestas` (ES 6580, FR 36640, IT 13100).
- **Un solo lugar:** la tabla `PAISES` es `static`; el resto del programa accede
  con `buscar_pais(sigla)` y `pais_por_id(id)`. Agregar un cuarto país toca solo
  `paises.c`.
- **Contrato de las funciones:** devuelven un puntero a la fila de la tabla, o
  `NULL` si el país no existe. `buscar_pais` también acepta `sigla == NULL` (y
  devuelve `NULL`). El puntero apunta a la tabla interna: no se libera ni se
  modifica (`const`).
- **Comparación de texto:** `buscar_pais` compara con `strcmp`; usar `==` compara
  direcciones, no el contenido.
- **Quién la usa:** el generador (valida `-p` y lee los máximos), el agente
  (`lector_csv` comprueba que la sigla y el `ID_Pais` del CSV existan y
  correspondan) y el servidor (convierte el `id_pais` de cada mensaje en un país,
  con sus nombres y pesos).
- **Nombres de sector sin tilde** ("Construccion") a propósito, para evitar
  problemas de codificación en el fuente y en la terminal. El agente identifica el
  sector por `ID_Sector`, no por el nombre.

### 3.2 `util` y `protocolo`: errores y mensajes por el pipe

- **`fatal(contexto)`** imprime el error (`perror`) y termina con `EXIT_FAILURE`;
  solo la usa `main`. **`aviso(contexto)`** imprime igual pero no termina; es la
  que pueden usar los hilos.
- **`leer_completo(fd, buf, n)`** repite `read` hasta juntar `n` bytes y reintenta
  si una señal lo interrumpe (`EINTR`). Devuelve `n`; `0` si el otro extremo cerró
  antes de enviar nada (EOF limpio); `-1` por error o por EOF a mitad de un
  mensaje. Si hay error, deja `errno` como lo dejó `read`.
- **`escribir_completo(fd, buf, n)`** es la gemela con `write`: devuelve `n` o `-1`.
- **`enviar_mensaje(fd, &m)`** devuelve `0` si escribió el mensaje entero y `-1` si
  no. **`recibir_mensaje(fd, &m)`** devuelve `1` si leyó un mensaje completo, `0`
  en EOF limpio y `-1` por error o mensaje cortado.
- **Pipe nominal y alarma de 60 s:** el servidor crea el FIFO con `mkfifo(ruta, S_IRUSR | S_IWUSR)` y abre `fd_pipe` en modo lectura bloqueante (`O_RDONLY`) junto con un descriptor propio de escritura `fd_ficticio` (`O_WRONLY`). Así `read()` se suspende sin espera activa cuando no hay datos y no devuelve `0` (EOF) si un agente cierra antes de que llegue el siguiente. Para el límite de 60 s se instala `signal(SIGALRM, manejador_senal)` y `alarm(TIMEOUT_SERVIDOR_SEG)` (además de `signal(SIGINT, manejador_senal)`): cuando suena la alarma o llega Ctrl+C, el manejador escribe un `MSG_STOP` en `fd_ficticio` para despertar inmediatamente al recolector bloqueado en `read()`.
- **`SIGPIPE`:** escribir en un pipe cuyo lector ya cerró mata el proceso, salvo que `main` haga `signal(SIGPIPE, SIG_IGN)`. Con eso `escribir_completo` devuelve `-1` con `errno == EPIPE` y el agente puede avisar y terminar con orden.

### 3.3 `buffer`: cola circular entre recolector y consumidor

- **Qué guarda:** `datos` (arreglo dinámico de `capacidad` `struct mensaje`),
  `entra` (próximo hueco donde escribir), `sale` (próximo mensaje que leer) y los
  tres semáforos de §5. Los índices avanzan con `% capacidad`, por eso al llegar al
  final dan la vuelta. Hay **un buffer por agente**, dentro de su `struct agente_ctx`.
- **Guarda copias, no punteros:** `buffer_poner` hace `datos[entra] = *m`. El
  recolector puede reutilizar su variable `m` apenas vuelve de `buffer_poner`.
- **Orden:** sale primero lo que entró primero (FIFO). Es lo que permite que, al
  ver `MSG_FIN`, el consumidor ya haya procesado todos los `MSG_DATO` (§4).
- **Funciones:**
  - `buffer_iniciar(b, capacidad)`: valida `b != NULL` y `capacidad > 0` (`tamB`
    viene de la línea de comandos), reserva `datos` con `malloc`, pone `entra` y
    `sale` en 0 e inicia los semáforos con `sem_init(.., 0, ..)`: el `0` indica
    que solo los usan hilos del mismo proceso. Si algo falla, **deshace lo ya
    hecho** (`sem_destroy` de los creados y `free`), deja `datos = NULL` y
    devuelve `-1`.
  - `buffer_destruir(b)`: `sem_destroy` de los tres semáforos, `free(datos)` y
    `datos = NULL`. Es **idempotente**: con `b == NULL` o `datos == NULL` no hace
    nada.
  - `buffer_poner(b, &m)`: **espera si el buffer está lleno**. `0` si guardó, `-1`
    si falló un semáforo.
  - `buffer_sacar(b, &m)`: **espera si el buffer está vacío**. `0` si copió el
    mensaje a `m`, `-1` si falló un semáforo.
- **Espera y señales:** `sem_wait` puede salir con `EINTR` si llega una señal; el
  helper privado `esperar` lo reintenta. Consecuencia: una señal **no** despierta a
  quien espera. Al consumidor lo despierta un `MSG_STOP`, no una señal; y el
  recolector bloqueado en `buffer_poner` sigue esperando hasta que el consumidor
  libere un hueco (es el tradeoff de §5).
- **Si falla un semáforo:** en `poner`/`sacar`, si falla la segunda espera (`mutex`)
  se devuelve el permiso ya tomado (`vacios` o `llenos`) para no perderlo. Si falla
  un `sem_post` se devuelve `-1`. En la práctica solo ocurre por un error de
  programación (semáforo no iniciado); quien llama avisa y trata a ese agente como
  fallido, sin `exit`.
- **Qué se valida y qué no:** `buffer_iniciar` valida sus argumentos porque `tamB` es
  entrada del usuario. `poner`/`sacar` son llamadas internas y **suponen** un
  buffer iniciado y punteros válidos. Los `sem_destroy` de limpieza no se validan:
  ya se está deshaciendo y no hay nada más que hacer.
- **Quién reserva y quién libera:** `buffer_iniciar` reserva (lo llama el recolector
  en `registrar_agente`, antes de crear el consumidor) y `buffer_destruir` libera
  (lo llama `main`, **después** de los `join`: destruir un semáforo con un hilo
  esperando en él es un error). `main` pone todo `struct servidor` en cero con
  `memset` (no es obvio: así las casillas de agentes nunca registrados tienen
  `datos == NULL` y destruirlas es seguro). Si `registrar_agente` falla a medias,
  llama a `buffer_destruir` sin riesgo aunque `buffer_iniciar` ya haya limpiado.
- **Memoria visible entre hilos:** `sem_post` y `sem_wait` sincronizan memoria
  (POSIX), así que `entra`, `sale` y `datos` no necesitan `volatile` ni otro candado.
- **Invariante útil para la sustentación:** con el buffer en reposo,
  `vacios + llenos == capacidad`, y `entra == sale` cuando está vacío o lleno
  (los semáforos distinguen cuál de los dos casos es).
- **Tamaño:** cada buffer ocupa `tamB * sizeof(struct mensaje)` (40 bytes en Linux
  x86-64); con 4 agentes y `-b 1000` son unos 160 KB.
- **`MSG_STOP` con el buffer lleno:** el recolector lo encola detrás de lo que haya
  pendiente (puede esperar un hueco). El consumidor procesa todo lo anterior y
  recién entonces para, así no se pierden datos ya recibidos.

### 3.4 `generador`: reparto por sectores y archivo CSV

- **Qué hace el programa:** `./generadorCSV -p <ES|FR|IT> -c <total>` crea
  `encuestas_<SIGLA>.csv` en el directorio actual e imprime el resumen del ejemplo
  de la pág. 8. Las banderas se aceptan en cualquier orden (`getopt`).
- **Reglas del reparto** (pág. 4, pág. 8 y Tabla 1): cada sector lleva **al menos 2**
  encuestas, **a lo sumo** `tope[s] = min(techo(30 % del total), max_sector[s])`
  y las cinco cuentas **suman exactamente** `total`.
- **Redondeo del 30 %: hacia arriba**, en enteros: `(total * 30 + 99) / 100`. Es
  la única lectura en que el ejemplo del enunciado es válido (25 encuestas →
  `5/7/8/3/2`, y 30 % de 25 = 7,5 → tope 8). Sin decimales ni `double`.
- **Cuándo no hay reparto:** `total < 10` (no alcanza el mínimo de 2 en 5
  sectores), `total > max_encuestas` del país, o `total` mayor que la suma de los
  topes. Como defensa, también si algún tope queda por debajo de 2 (con los datos
  actuales no ocurre: el menor tope es 290 o el 30 % de un total ≥ 10, es decir ≥ 3).
  `repartir_sectores` devuelve `-1` sin imprimir nada y **sin modificar** `cuentas`;
  `main` explica el motivo y sale con `EXIT_FAILURE`. Comprobar
  `total > max_encuestas` **primero** evita desbordes en la cuenta del tope cuando
  `-c` es enorme.
- **Totales que sí se pueden repartir** (calculados probando todos los valores,
  con el techo): **ES 10 a 6515, FR 10 a 18029, IT 10 a 10944**. Por encima de
  eso, el 30 % y la Tabla 1 se contradicen (ver 9.7). En el límite no queda
  holgura: todos los sectores van en su tope.
- **Algoritmo:** (1) validar y calcular los topes; (2) poner 2 en cada sector;
  (3) repartir lo que falta de a una encuesta: se sortea un sector con
  `rand() % NUM_SECTORES` y, si ya está en su tope, se sortea otro. Eso equivale a
  elegir al azar entre los sectores que todavía tienen cupo. La validación del
  paso 1 garantiza que siempre queda cupo, así que el paso 3 termina: cada sorteo
  acierta con probabilidad al menos 1/5, y por eso en el peor caso (sin holgura)
  el número de sorteos es solo unas 5 veces el número de encuestas.
- **Aleatoriedad:** `srand((unsigned)time(NULL) ^ (unsigned)getpid())` **una sola
  vez** en `main`. Solo con la hora, dos corridas en el mismo segundo (ES y FR
  seguidos en un script) saldrían con la misma secuencia de sentimientos; el
  número de proceso las separa. El
  sentimiento es `rand() / (double)RAND_MAX * 100` redondeado a 2 decimales
  (queda entre `0.00` y `100.00`, ambos incluidos). Ni `repartir_sectores` ni
  `escribir_csv` llaman a `srand`. En Windows `RAND_MAX` vale 32767 (menos
  resolución); en Linux, 2147483647.
- **Formato del archivo:** encabezado
  `ID_Encuesta,ID_Pais,Pais,Sector,ID_Sector,Sentimiento`; luego las filas
  **agrupadas por sector** (0 a 4), con `ID_Encuesta` global que empieza en 1 y
  sube de a uno. Sentimiento con `%.2f` (no llamar a `setlocale`, o saldría coma),
  fin de línea `\n`. Nombres de sector sin tilde, igual que en `paises.c`.
- **Errores:** se valida `fopen`, cada `fprintf` y `fclose` (puede fallar al
  vaciar el buffer si el disco está lleno). Si algo falla, `escribir_csv` imprime
  el motivo con `perror(ruta)`, cierra el archivo y devuelve `-1`; el archivo puede
  quedar a medias en disco. Ambas funciones devuelven `-1` si reciben un `NULL`.
  Devuelven `0`/`-1` y solo `main` decide terminar.
- **Fin de línea en Windows:** `fopen(.., "w")` escribe `\r\n` en Windows (modo
  texto). En Linux, que es el destino del proyecto, escribe `\n`. Si prueban el
  generador en Windows, `verificar_csv.sh` marcará el `\r\n`: no es un error del
  código.
- **Argumentos (`main.c`):** `-c` se convierte con `strtol` (con `errno` y
  `endptr`) y debe ser un entero mayor que 0 sin letras de más; `-p` se busca en
  la tabla con `buscar_pais`, que distingue mayúsculas (`fr` no vale). Faltar una
  bandera, sobrar un argumento o una bandera desconocida muestran el uso. Todo
  error sale por `stderr` con `EXIT_FAILURE` y **sin crear el archivo**.
- **Nombre del archivo:** `sprintf(ruta, "encuestas_%s.csv", pais->sigla)` en un
  arreglo de 32 bytes. El nombre ocupa 17, y la sigla sale de la tabla, no de
  lo que escribió el usuario: no puede desbordar (por eso no hace falta
  `snprintf`, que es de C99).
- **Consola:** se imprime "Generando fichero…" antes de escribir y el resumen
  después, con el formato de la pág. 8 pero sin tildes ("pais", "Maximos"), igual
  que los nombres de sector. Si `repartir_sectores` falla, `main` dice cuál de
  los tres motivos fue, sin repetir la cuenta de los topes.
- **Cómo se prueba:** `bash tests/verificar_csv.sh encuestas_FR.csv 25` comprueba
  todo lo anterior sobre el archivo generado (no necesita nada compilado aparte).

### 3.5 `lector_csv`: del archivo a un arreglo en memoria

- **Contrato:** `leer_csv(ruta, &encuestas, &n, sigla, &id_pais)` devuelve `0` y
  deja un arreglo de `n` `struct encuesta` reservado con `malloc`, más la sigla y
  el `ID_Pais` del archivo. Quien llama lo libera con `liberar_encuestas`. Si
  devuelve `-1`, `encuestas` queda en `NULL` y `n` en `0`: no hay nada que liberar.
- **Estrategia: dos pasadas.** La primera solo cuenta las filas con datos; con
  ese número se hace **un** `malloc` exacto; `rewind` y la segunda pasada llena
  el arreglo. Se eligió porque reserva justo lo necesario y evita la trampa de
  `realloc` (si falla devuelve `NULL` pero el bloque viejo sigue vivo). El costo
  es leer el archivo dos veces, despreciable (18 029 filas son ~0,5 MB). Exige un
  archivo que se pueda rebobinar: con un pipe no serviría, pero `-n` es un
  archivo. Si la segunda pasada no lee las mismas filas que la primera (el
  archivo cambió en medio), se rechaza.
- **Qué se valida** (cualquier falla rechaza el archivo entero):
  - el encabezado exacto en la línea 1 (si faltara, descartar la primera línea
    perdería una encuesta sin avisar);
  - al menos una encuesta; líneas de hasta 254 caracteres más el `\n` (caben en
    `linea[256]`; una fila real mide unos 35);
  - exactamente 6 campos por fila, con punto decimal. `sscanf` termina el formato
    con `" %c"` y debe devolver 6: si devuelve 7 había algo después del
    sentimiento. Sin esto, `45,23` (coma decimal) se leía como `45` y
    `45.23abc` como `45.23`, sin avisar;
  - `0 <= ID_Sector <= 4` y `0 <= Sentimiento <= 100`, escrito como
    `!(sent >= 0.0 && sent <= 100.0)` para que `nan` también se rechace;
  - que `Pais` exista en la tabla de `paises.c` y que su `id` sea el `ID_Pais` de
    la fila, y que todas las filas sean del mismo país (un agente = un país).
- **Qué no se valida:** el texto de la columna `Sector` (manda `ID_Sector`) y que
  `ID_Encuesta` sea correlativo. Consecuencia: un CSV con la errata del PDF
  (Comercio con `ID_Sector` 2) cuenta esas filas como Consumidores.
  `tests/verificar_csv.sh` sí detecta esa errata.
- **Se toleran:** líneas en blanco, fin de línea `\r\n` y última línea sin `\n`.
- **Mensajes:** cada rechazo imprime por `stderr` el archivo, la línea y el motivo
  (`encuestas_FR.csv: linea 7: Sentimiento fuera de rango (0 a 100)`) mediante el
  helper privado `error_csv`, que además libera el arreglo y cierra el archivo.
  Así ninguna salida por error se olvida de limpiar.

Regla de dependencias (no la rompan): `main.c` → módulos → `comun`.
`comun` no conoce a nadie. `ise.c` no hace E/S ni usa hilos: es matemática pura,
por eso se puede probar sin levantar nada.

## 4. Protocolo del pipe

Un único tipo de mensaje de **tamaño fijo** (`struct mensaje`: 32 bytes medidos
en Windows/MinGW, donde `long` mide 4; en Linux x86-64 `long` mide 8 y son 40).

| `tipo` | Quién lo manda | Campos que importan |
|---|---|---|
| `MSG_HELLO` | agente, 1 vez al conectar | `sigla`, `id_pais`, `total` (encuestas que va a enviar) |
| `MSG_DATO` | agente, N veces | `id_pais`, `id_encuesta`, `id_sector`, `sentimiento` |
| `MSG_FIN` | agente, 1 vez al terminar | `id_pais`, `total` (las que realmente envió) |
| `MSG_STOP` | **solo el servidor**, interno | orden de parar. Viaja dos veces: el manejador de señales lo escribe en el propio FIFO para despertar al recolector, y el recolector lo pone en el buffer de cada consumidor que siga activo |

Por qué tamaño fijo: POSIX garantiza que un `write` de hasta `PIPE_BUF` bytes
(4096 en Linux) en un pipe es **atómico**. Con varios agentes escribiendo a la
vez, los mensajes no se mezclan a la mitad. Si el mensaje fuera de largo
variable (texto con `\n`) habría que reensamblar, y es fuente de errores.

Antes de enviar, el agente pone la estructura en cero (`memset(&m, 0, sizeof m)`)
y recién entonces llena los campos: así no viajan bytes de relleno sin
inicializar (que `valgrind` marca como error).

El orden entre mensajes del *mismo* agente se conserva (un solo escritor, un
solo pipe). Por eso, cuando el consumidor ve `MSG_FIN`, ya procesó todos los
`MSG_DATO` de ese agente.

## 5. Sincronización

**Problema:** el recolector (productor) y cada consumidor trabajan a distinta
velocidad y comparten un buffer de `tamB` registros.

**Solución clásica productor/consumidor con 3 semáforos POSIX por buffer:**

| Semáforo | Valor inicial | Significa |
|---|---|---|
| `vacios` | `tamB` (`capacidad` en el código) | huecos libres |
| `llenos` | `0` | registros listos para consumir |
| `mutex` | `1` | exclusión mutua sobre `entra`/`sale`/`datos` |

- `poner`: `sem_wait(vacios)` → `sem_wait(mutex)` → guardar y avanzar `entra` → `sem_post(mutex)` → `sem_post(llenos)`
- `sacar`: `sem_wait(llenos)` → `sem_wait(mutex)` → leer y avanzar `sale` → `sem_post(mutex)` → `sem_post(vacios)`

El orden importa: **nunca** tomar `mutex` antes de `vacios`/`llenos`
(se produce un deadlock: se duerme con el mutex tomado).

**Qué NO necesita candado, a propósito (diseño sin estado compartido):**

- `struct acum_pais` de cada agente lo escribe **solo su consumidor**. El hilo main
  lo lee **después** de `pthread_join`, y `join` ya establece el orden de memoria.
  Única excepción: el recolector llama a `acum_iniciar` al registrar al agente,
  **antes** de crear su consumidor; `pthread_create` ya ordena la memoria, así que
  tampoco hace falta candado.
- `estado` de cada agente lo escribe **solo el recolector**.
- Cada hilo toca únicamente lo suyo → menos semáforos y menos errores.

Tradeoff conocido: si el consumidor de un país se atasca con el buffer lleno,
el recolector se bloquea en `sem_wait(vacios)` y deja de leer el FIFO para los
demás países. Es aceptable aquí; menciónenlo en la sustentación.

## 6. Ciclo de vida del servidor

1. `main`: parsear flags → validar → `unlink` + `mkfifo` → `open` (lectura y
   escritura propia) → inicializar estructuras.
2. `main`: `signal` para `SIGPIPE` (ignorar), `SIGALRM` y `SIGINT` (manejador) →
   `alarm(60)` → `pthread_create(recolector)`.
3. Recolector: lee con `read` bloqueante hasta que ocurra **una** de estas cosas:
   a) todos los agentes de `-a` mandaron `FIN`;
   b) lee un `MSG_STOP`, que el manejador escribe en el FIFO cuando suena la
      alarma de 60 s o llega Ctrl+C.
4. Recolector, al salir: informa cuántos mensajes descartó (si hubo) e inyecta
   `MSG_STOP` en el buffer de cada agente que no cerró, para que su consumidor
   despierte y termine.
5. `main`: `join` del recolector → `alarm(0)` → `join` de cada consumidor.
6. `main`: reporte final → `close` → `unlink` del FIFO → `sem_destroy` y `free`.

### Señales: detalles que hay que saber explicar

- **Por qué el manejador escribe en el pipe.** El recolector está dormido en
  `read`. El manejador no puede "avisarle" con una variable, porque el recolector
  no la miraría hasta despertar. Al escribir un `MSG_STOP` en el mismo FIFO, el
  `read` despierta con un mensaje más, que se procesa en orden: primero todo lo
  que ya estaba en el pipe y después la orden de parar. `write` es una de las
  funciones que se pueden llamar dentro de un manejador.
- **`fd_ficticio` tiene dos usos:** mantiene siempre un escritor abierto, para que
  `read` no devuelva `0` (fin de entrada) cuando un agente cierra y aún no llegó
  el siguiente; y es por donde el manejador escribe el `MSG_STOP`.
- **El manejador se instala una sola vez por señal.** Con `-std=c89` y
  `-D_POSIX_C_SOURCE=200809L`, `signal()` en Linux tiene el comportamiento
  antiguo de System V: después de atender la señal, el manejador **se
  desinstala** y vuelve la acción por defecto. Consecuencia: el primer Ctrl+C
  detiene el servidor con orden (reporte y limpieza); un **segundo** Ctrl+C lo
  mata en el acto y el FIFO queda sin borrar. La diapositiva de clase dice que
  "el manejador queda instalado", pero eso vale para el comportamiento BSD.
- **Las llamadas interrumpidas se reintentan a mano.** Con ese mismo
  comportamiento, una señal hace que `read` o `sem_wait` fallen con `EINTR` en
  el hilo que la reciba. Por eso `leer_completo` y `esperar` repiten la llamada.
- **Qué hilo atiende la señal.** Una señal dirigida al proceso la puede atender
  cualquier hilo; en Linux casi siempre es el principal, que está en
  `pthread_join`. Limitación conocida: si la atendiera el recolector **con el
  pipe lleno**, el `write` del manejador se quedaría esperando un hueco que solo
  el propio recolector puede liberar. No se ha visto en las pruebas. La forma de
  eliminarlo sería bloquear `SIGALRM` y `SIGINT` en los demás hilos con
  `pthread_sigmask`, que no se vio en clase.
- **Agentes de más.** Si llegan más agentes que `-a`, o dos del mismo país, el
  `MSG_HELLO` se rechaza con un aviso. Ese agente no se entera (el pipe va en un
  solo sentido) y envía todo igual: el recolector **cuenta** esos mensajes y al
  final informa el total en una línea, en vez de avisar por cada uno. Si dos
  agentes del mismo país envían a la vez, sus datos se mezclan (tienen el mismo
  `id_pais`): se asume un agente por país.

## 7. Cálculo del ISE (`include/ise.h` y `src/servidor/ise.c`)

El módulo `ise` es **puro** (sin E/S, sin hilos y sin semáforos): recibe datos numéricos y devuelve resultados numéricos o literales de texto.

### Estructura de datos
- `struct acum_pais`: mantiene por cada país dos arreglos de tamaño `NUM_SECTORES` (5):
  - `suma[s]`: suma acumulada de los valores de sentimiento del sector `s`.
  - `cuenta[s]`: cantidad de encuestas recibidas para el sector `s`.
  Se acumulan sumas y cuentas por separado para dividir una sola vez al final y evitar errores de redondeo acumulado.

### Funciones del módulo
1. `acum_iniciar(a)`: inicializa `suma[0..4] = 0.0` y `cuenta[0..4] = 0` cuando el recolector registra el `MSG_HELLO` de un agente. Se hace con `memset(a, 0, sizeof *a)`, que sirve porque en IEEE 754 (Linux) el `double` `0.0` tiene todos sus bits en cero.
2. `acum_agregar(a, id_sector, sentimiento)`: invocada por el hilo consumidor por cada `MSG_DATO`. Valida que `0 <= id_sector < NUM_SECTORES` (devuelve `-1` si es inválido sin modificar memoria) y acumula `suma[id_sector] += sentimiento` y `cuenta[id_sector] += 1`.
3. `promedios_sector(a, prom)`: calcula el promedio por sector `prom[s] = suma[s] / cuenta[s]` y devuelve cuántos sectores tuvieron al menos una encuesta (`0..5`).
4. `ise_pais(prom)`: calcula la media ponderada del país usando `PESO_SECTOR[]`:
   `ISE_pais = prom[0]*0.40 + prom[1]*0.30 + prom[2]*0.20 + prom[3]*0.05 + prom[4]*0.05`.
5. `ise_global(ise_paises, n)`: calcula el promedio simple de los `n` países que participaron (`0.0` si `n <= 0`).
6. `clasificar(ise)` y `nombre_recomendacion(r)`: clasifican el índice según los umbrales del enunciado:
   - `> 55.0` → `REC_VENDER` (`"Vender"`)
   - `< 45.0` → `REC_COMPRAR` (`"Comprar"`)
   - `45.0 <= ise <= 55.0` → `REC_NEUTRO` (`"Neutro"`)

### Decisión ante sectores sin encuestas (`cuenta[s] == 0`)
- En `promedios_sector`, si `cuenta[s] == 0` **no se divide** (evitando `NaN`/`Inf` por división entre cero) y se asigna `prom[s] = 0.0`.
- La función devuelve la cantidad de sectores con datos (`con_datos`).
- En el reporte final (`reporte.c`):
  - Si un país tiene `con_datos == 0` (ninguna encuesta recibida), se informa `"sin datos"` y **no se incluye** en el promedio de `ise_global`.
  - Si un país tiene `0 < con_datos < NUM_SECTORES` (o quedó incompleto), se calcula su ISE con los sectores disponibles (`prom[s] = 0.0` en los faltantes) y se imprime una advertencia explícita en el reporte indicando que faltaron sectores/encuestas.

**Caso de prueba oficial:** Francia con promedios `40.25 / 60.70 / 58.44 / 52.13 / 30.12` da **`50.1105`** (`16.10 + 18.21 + 11.688 + 2.6065 + 1.506`) → **`Neutro`**. Ver `data/encuestas_FR_ejemplo.csv`.

## 8. Errores y recursos

- Toda llamada al sistema se valida (`open`, `read`, `write`, `mkfifo`,
  `pthread_create`, `sem_*`, `malloc`, `fopen`). Eso es parte de la rúbrica.
  Única excepción a propósito: los `sem_destroy` y `free` de las rutas de limpieza
  (`buffer_destruir` y el retroceso de `buffer_iniciar`), que no tienen cómo
  recuperarse (ver 3.3).
- Convención: las funciones devuelven `0` si salió bien y `-1` si falló. Tres
  excepciones, porque deben devolver más información: `leer_completo` y
  `escribir_completo` (bytes transferidos; `0` = EOF en la lectura) y
  `recibir_mensaje` (`1` = mensaje, `0` = EOF, `-1` = error; ver 3.2).
  Solo `main` decide terminar el programa.
- Quien reserva libera: todo `malloc` tiene su `free`, todo `open` su `close`,
  todo `sem_init` su `sem_destroy`, y el servidor hace `unlink` del FIFO.
- **`sleep` y la apertura del pipe.** La rúbrica pide usar "solo los sleeps
  necesarios al momento de la apertura de pipes". El agente abre con
  `open(pipe, O_WRONLY)` bloqueante, sin ningún `sleep`: como el servidor ya
  tiene abierto el extremo de lectura, la llamada vuelve enseguida; si el FIFO no
  existe, falla con `ENOENT` y el agente recuerda que el servidor va primero.
  La clase enseña otra forma: `do { fd = open(pipe, O_WRONLY | O_NONBLOCK);
  if (fd == -1) sleep(1); } while (fd == -1);`, que espera al servidor. Si se
  adopta, **hay que quitar `O_NONBLOCK` después de abrir** (con `fcntl`): si no,
  cuando el pipe se llene `write` falla con `EAGAIN` y el agente se cae a mitad
  del envío.
- **Qué llamadas se validan:** `open`, `read`, `write`, `mkfifo`, `signal`
  (`SIG_ERR`), `pthread_create` y `pthread_join` (devuelven el código de error,
  se imprime con `strerror`), `sem_init`, `sem_wait`, `sem_post`, `malloc`,
  `fopen`, `fprintf` al archivo y `fclose`. No se validan a propósito: `close`,
  `unlink` y `sem_destroy` de las rutas de limpieza, `alarm` (no puede fallar) y
  el `write` del manejador de señales (no hay nada seguro que hacer si falla).

## 9. Decisiones abiertas (preguntar al profesor)

1. `-a <hilos>`: ¿es el número de agentes o de hilos por agente? Aquí se toma
   como **número de agentes** y un hilo consumidor por agente.
2. ¿El servidor termina cuando todos los agentes mandan `FIN`, o siempre espera
   los 60 s? Aquí: termina en lo que ocurra primero.
3. El PDF contiene texto oculto (blanco sobre fondo blanco, págs. 6 y 9) que
   menciona "java", "sql" y "servlet". No es parte visible del enunciado y se
   ignora: el resto del documento exige ANSI C.
4. Ejemplo de consola `hiloPais` vs `agentePais`.
5. En el CSV de ejemplo, Comercio tiene `ID_Sector=2` (igual a Consumidores);
   aquí se usa 3.
6. "máximo el 30 %" por sector: ¿redondeo hacia arriba o abajo? En el ejemplo
   de 25 encuestas, un sector tiene 8 (> 7,5). **Adoptado por ahora: hacia
   arriba** (es lo único que hace válido el ejemplo); ver 3.4. Con redondeo hacia
   abajo los totales máximos serían 6514 / 18028 / 10942 (ES / FR / IT).
7. La Tabla 1 (máximos por sector y país) ya está copiada en `src/comun/paises.c`.
   **Choca con la regla del 30 %:** con el total máximo de cada país, el tope
   `min(30 % del total, máximo de Tabla 1)` suma menos que el total
   (ES: 6534 de 6580; FR: 23612 de 36640; IT: 11590 de 13100), así que no hay
   reparto posible cerca del máximo: solo se pueden generar hasta 6515 (ES),
   18029 (FR) y 10944 (IT) encuestas. Las dos reglas están en el enunciado: la
   pág. 4 dice que los valores se generan "empleando como máximos los valores
   mostrados en la Tabla 1", y la pág. 8 fija el mínimo de 2 y el máximo del
   30 %. **Adoptado por ahora:** se aplican el 30 % y la Tabla 1,
   y si el reparto es imposible el generador lo rechaza con un mensaje. Decidir con
   el profesor: ¿el 30 % se aplica siempre?, ¿el "máximo de encuestas" de la
   tabla es un tope real o solo orientativo?, ¿qué debe hacer el generador cuando
   el reparto es imposible?
8. `ID_Pais`: el ejemplo del enunciado solo muestra FR = 1. Aquí ES = 0 e IT = 2
   (orden alfabético). Si el profesor usa otra numeración, cambia solo la columna
   `id` de `PAISES` en `paises.c`.
9. Entrada del generador: la pág. 4 dice "el país y el porcentaje de creación de
   cantidad de encuestas", pero la pág. 7 dice "la cantidad de encuestas a
   crear" y la invocación de la pág. 8 usa `-c <num encuestas>`. Aquí `-c` es
   una **cantidad**, no un porcentaje. La pág. 9 vuelve a hablar de porcentaje
   ("para Francia con 75% de encuestas") al pie del ejemplo.
10. **Guía de estilo.** La rúbrica (pág. 10) pide "seguir las indicaciones de la
    guía de estilo" del curso. Ese documento no está en el repositorio: hay que
    conseguirlo y contrastar el código (encabezado de archivo con autores,
    nombres, sangría, etc.).
11. Salida del agente: el ejemplo de la pág. 7 abre con un recuadro "THREAD -
    ENVÍO DE ENCUESTAS" y el binario se llama `hiloPais`. Aquí se siguen las
    líneas del ejemplo (País, Pipe, Leídas, Conectando, Conectado, Enviando,
    progreso, Enviadas, Mensaje de finalización, Envío finalizado) sin el
    recuadro y sin tildes.

## 10. Orden de implementación sugerido

(✔ = ya hecho y probado)

1. ✔ `util.c` y `paises.c` (cimientos).
2. ✔ `ise.c` + prueba con los números de Francia (no necesita hilos ni pipes).
3. ✔ `generador.c` y ✔ `generador/main.c` (pasan `verificar_csv.sh`).
4. ✔ `lector_csv.c` y ✔ `agente/main.c`.
5. ✔ `protocolo.c`, ✔ `consumidor.c` y ✔ `recolector.c`.
6. ✔ `buffer.c` con 3 semáforos POSIX (`vacios`, `llenos`, `mutex`).
7. ✔ `servidor/main.c` y ✔ `reporte.c` (alarma de 60 s con `alarm`/`signal`, `MSG_STOP`, limpieza con `close`/`unlink`/`buffer_destruir` y reporte final).

### Qué está probado y qué no

- **Probado en Windows (MinGW), ejecutando el código real:** `ise`, `paises`,
  `util`/`protocolo`, `buffer` (130 comprobaciones), `generador` y
  `generadorCSV` completo, `lector_csv` (34 casos), y la lógica del servidor
  (`recolector` + `consumidor` + `reporte`) con un pipe anónimo en lugar del
  FIFO: ejemplo de Francia (50,1105, Neutro), dos agentes intercalados con
  buffer de 2, corte por `MSG_STOP`, agentes de más y mensajes inválidos.
- **Probado en Linux** (Debian 12 en Docker, gcc 12.2, los tres programas
  reales con el FIFO):
  - `make` compila sin ningún aviso; `sizeof(struct mensaje)` es 40.
  - `tests/prueba_ise.sh` pasa (Francia: 50,1105, Neutro, FIFO borrado).
  - Tres agentes a la vez (ES 6515, FR 18029, IT 10944) con `-b 2` y las
    banderas en otro orden: llegan las 35 488 encuestas y los ISE coinciden al
    cuarto decimal con un cálculo aparte hecho con `awk` sobre los CSV.
  - Más agentes que `-a`: el sobrante se rechaza y sus 10 945 mensajes se
    informan en una sola línea.
  - Sin agentes, el servidor termina a los 60 s exactos con el reporte vacío.
  - `SIGINT` (Ctrl+C): para en 2 ms, imprime el reporte y borra el FIFO, con o
    sin agentes conectados.
  - Agente sin servidor: `ENOENT` y mensaje. Lector que muere a mitad del
    envío: el agente recibe `EPIPE` ("Broken pipe") y termina con error.
  - `signal()` con las banderas del Makefile: confirmado que el manejador se
    desinstala tras el primer uso (sección 6).
  - `valgrind --leak-check=full --track-fds=yes` en los tres programas (servidor
    con dos agentes, y agente con un CSV inválido): sin errores ni fugas.
- **Falta:** probarlo en el equipo donde se va a sustentar, con cada agente en
  su propia terminal, y `-fsanitize=thread`.

## 11. Lo visto en clase y lo que hay que saber explicar aparte

La sustentación pide justificar decisiones y **hacer una modificación en vivo**.
Esto sigue las diapositivas del curso:

| En el código | En clase |
|---|---|
| Buffer con `vacios`, `llenos`, `mutex` y el orden `wait`/`post` | Productor/consumidor con `vacias = N`, `llenas = 0`, `excmut = 1` (Clase 11, semáforos), incluido por qué no se toma primero la exclusión mutua |
| `unlink` + `mkfifo(ruta, S_IRUSR \| S_IWUSR)`, un extremo solo lectura y otro solo escritura, `unlink` al terminar | Pipes nominales (Clase 11, pipes) |
| Mensajes de tamaño fijo, `write` atómico, `read` devuelve 0 si se cerró la escritura, `SIGPIPE` | Reglas de pipes de la misma clase y tabla de señales (Clase 14) |
| `signal(SIGALRM, manejador)` + `alarm(60)`, `signal(SIGINT, ...)`, `SIG_IGN` | Señales (Clase 14) |
| `if (... == -1) perror(...)` | Ejemplos de `open` de la clase de pipes |

Esto **no** aparece en las diapositivas que se revisaron y hay que poder
explicarlo con palabras propias:

- `getopt`, `optarg`, `optind` (banderas en cualquier orden) y `strtol` con
  `errno` y `endptr`.
- La API de hilos (`pthread_create`, `pthread_join`) y la de semáforos POSIX
  (`sem_init`, `sem_wait`, `sem_post`, `sem_destroy`): las diapositivas usan
  `doWait`/`doSignal` y `down`/`up`.
- El descriptor de escritura propio (`fd_ficticio`) y el manejador que escribe
  en el pipe (sección 6).
- Los reintentos por `EINTR` en `leer_completo`, `escribir_completo` y `esperar`.
- `memset` antes de enviar un mensaje, `sscanf` con `%2[^,]` y `" %c"`, y
  `rewind` en `lector_csv`.
- El comportamiento de `signal()` con `-std=c89` (sección 6).

## 12. Cómo se diseña un programa en C

C no tiene clases ni paquetes; el diseño se hace con **archivos**:

- **`.h` = contrato** (qué ofrece el módulo: tipos y prototipos).
- **`.c` = implementación** (cómo lo hace). Lo que no está en el `.h` es privado:
  se declara `static`.
- **Un módulo = una responsabilidad.** Si no puede describir el archivo en una
  frase sin usar "y", pártalo.
- **Estructuras (`struct`) en lugar de objetos**, y funciones que reciben el
  puntero a la estructura (`buffer_poner(&b, &m)`) en lugar de métodos.
- **Propiedad de la memoria:** decidir quién reserva y quién libera, y escribirlo
  en el comentario de la función.
- **Dependencias en un solo sentido:** `main` usa módulos; los módulos de bajo
  nivel no conocen a los de alto nivel.
- **Separar lo puro de lo que tiene efectos:** el cálculo (`ise.c`) sin E/S,
  para poder probarlo solo.
