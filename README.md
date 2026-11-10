# Indicador de Sentimiento Económico (ISE)

Proyecto de Sistemas Operativos (ANSI C89 en Linux / POSIX).  
Implementación de un sistema concurrente cliente-servidor para calcular el **Indicador de Sentimiento Económico (ISE)** por país y de forma global, utilizando **pipes nominales (FIFO)**, **hilos POSIX (`pthread`)**, **semáforos POSIX (`sem_t`)** y **manejo de señales (`signal`, `alarm`)**.

---

## 1. Descripción General del Sistema

El sistema está dividido en **tres programas independientes** que trabajan en conjunto:

1. **`generadorCSV`:**  
   Genera un archivo de texto separado por comas (`encuestas_<SIGLA>.csv`) con las encuestas de un país (`ES`, `FR` o `IT`), repartiendo el total solicitado entre los 5 sectores económicos respetando el mínimo de 2 encuestas por sector, el tope del 30% del total por sector y los máximos de la Tabla 1 del enunciado.
2. **`agentePais` (Proceso Cliente):**  
   Se ejecuta un proceso por cada país (cada uno en su propia terminal). Lee el archivo CSV de su país, valida todas sus filas en dos pasadas para reservar memoria exacta con `malloc`, abre el pipe nominal en modo solo escritura (`O_WRONLY`) y envía las encuestas al servidor mostrando el progreso cada 500 registros.
3. **`servidorISE` (Proceso Principal / Servidor):**  
   Crea el pipe nominal con `mkfifo`, instala la alarma de reloj de 60 segundos (`alarm` + `SIGALRM`) y el manejador para `Ctrl-C` (`SIGINT`), levanta un **hilo recolector** (productor) para leer del pipe nominal y crea dinámicamente un **hilo consumidor** y un **buffer circular con 3 semáforos** por cada agente que se conecta. Al terminar todos los agentes o cumplirse el tiempo máximo de 60 segundos, imprime el resumen general del ISE y su recomendación de inversión (`Vender`, `Comprar` o `Neutro`), y elimina todos los recursos temporales.

---

## 2. Organización del Proyecto

```text
.
├── Makefile                  # Reglas de compilación estricta en ANSI C89 (-std=c89 -pedantic -Wall -Wextra)
├── include/                  # Archivos de cabecera (.h) con las estructuras y contratos de cada módulo
│   ├── comun.h               # Constantes globales (límites, umbrales, sectores)
│   ├── paises.h              # Estructuras y funciones de consulta de la Tabla 1 y pesos por sector
│   ├── util.h                # Funciones de lectura/escritura completa en descriptores y avisos de error
│   ├── protocolo.h           # Estructura de mensaje de tamaño fijo para el pipe nominal
│   ├── generador.h           # Prototipos del generador de archivos CSV
│   ├── lector_csv.h          # Estructura de encuesta y prototipos del lector CSV en dos pasadas
│   ├── ise.h                 # Acumulador por país y funciones matemáticas puras del cálculo ISE
│   ├── buffer.h              # Cola circular acotada con 3 semáforos POSIX
│   └── servidor.h            # Estructuras de configuración, contexto de cada agente y estado del servidor
├── src/                      # Implementación en C (.c) comentada paso a paso
│   ├── comun/
│   │   ├── paises.c          # Tabla de países (ES, FR, IT), sectores, pesos y búsqueda por sigla o ID
│   │   ├── util.c            # Implementación de leer_completo, escribir_completo, fatal y aviso
│   │   └── protocolo.c       # Envío y recepción atómica de struct mensaje por el pipe
│   ├── generador/
│   │   ├── generador.c       # Reparto aleatorio por sector y escritura del archivo CSV
│   │   └── main.c            # Lectura de banderas (-p y -c), validaciones y ejecución de generadorCSV
│   ├── agente/
│   │   ├── lector_csv.c      # Lectura y validación del CSV en dos pasadas con arreglo dinámico
│   │   └── main.c            # Lectura de banderas (-n y -p), conexión al pipe y envío por lotes
│   └── servidor/
│       ├── ise.c             # Acumulación por sector, promedio ponderado por país, ISE global y clasificación
│       ├── buffer.c          # Solución productor/consumidor con 3 semáforos (vacios, llenos, mutex)
│       ├── consumidor.c      # Hilo consumidor dedicado a procesar el buffer de un agente
│       ├── recolector.c      # Hilo recolector que lee del pipe nominal y reparte a cada buffer
│       ├── reporte.c         # Impresión del reporte detallado por país y del ISE global
│       └── main.c            # Creación del FIFO, señales (SIGALRM/SIGINT/SIGPIPE), hilos y limpieza
├── data/
│   └── encuestas_FR_ejemplo.csv  # Archivo de prueba con los valores del ejemplo oficial de Francia
├── docs/
│   ├── README.md             # Guía de la documentación técnica y resumen para la sustentación
│   └── diseno.md             #Explicación detallada de diseño, sincronización y protocolo
└── tests/
    ├── verificar_csv.sh      # Script para validar el formato y las reglas de los CSV generados
    └── prueba_ise.sh         # Prueba automática de extremo a extremo (servidor + agente Francia)
```

---

## 3. Cómo Funciona Paso a Paso

### 3.1 Comunicación por Pipe Nominal (FIFO)
- **Creación:** Primero el servidor elimina cualquier pipe viejo con `unlink` y crea el pipe nominal usando `mkfifo(ruta, S_IRUSR | S_IWUSR)`.
- **Apertura sin espera activa:** El servidor abre `fd_pipe` en modo lectura bloqueante (`O_RDONLY`) y mantiene también un descriptor auxiliar `fd_ficticio` en modo escritura (`O_WRONLY`). De esta manera:
  1. Cuando el pipe está vacío, la llamada `read()` suspende al hilo recolector sin consumir CPU (sin espera ocupada).
  2. Si un agente termina y cierra su descriptor antes de que se conecte el siguiente agente, `read()` no devuelve `0` (fin de archivo prematuro), sino que sigue esperando a los demás agentes.
- **Mensajes atómicos:** Tanto los agentes como el servidor intercambian estructuras `struct mensaje` de tamaño fijo (menor que `PIPE_BUF`). Según las reglas de pipes en UNIX, toda escritura menor a `PIPE_BUF` es **atómica**, evitando que los bytes de distintos agentes se mezclen cuando varios escriben al mismo tiempo.

### 3.2 Sincronización Productor / Consumidor y Prevención de Interbloqueo
Cada agente registrado en el servidor tiene su propio `struct buffer` acotado de capacidad `tamB` (`-b`) protegido por **3 semáforos POSIX**:
- `vacios` (inicia en `capacidad`): cuenta cuántos espacios libres quedan en el buffer.
- `llenos` (inicia en `0`): cuenta cuántos mensajes listos para leer hay en el buffer.
- `mutex` (inicia en `1`): garantiza la **exclusión mutua** al modificar el arreglo `datos` y los índices `entra` y `sale`.

**Orden de solicitud para evitar interbloqueo (Deadlock):**
- **En `buffer_poner` (Hilo Recolector / Productor):**
  1. Primero espera un hueco libre: `sem_wait(&b->vacios)`.
  2. Después entra a la sección crítica: `sem_wait(&b->mutex)`.
  3. Copia el mensaje en `b->datos[b->entra]` y avanza `b->entra = (b->entra + 1) % b->capacidad`.
  4. Sale de la sección crítica: `sem_post(&b->mutex)`.
  5. Avisa al consumidor que hay un elemento nuevo: `sem_post(&b->llenos)`.
- **En `buffer_sacar` (Hilo Consumidor):**
  1. Primero espera un mensaje disponible: `sem_wait(&b->llenos)`.
  2. Después entra a la sección crítica: `sem_wait(&b->mutex)`.
  3. Extrae el mensaje de `b->datos[b->sale]` y avanza `b->sale = (b->sale + 1) % b->capacidad`.
  4. Sale de la sección crítica: `sem_post(&b->mutex)`.
  5. Avisa al productor que quedó un hueco libre: `sem_post(&b->vacios)`.

Nunca se pide `mutex` antes de `vacios` o `llenos`; así ningún hilo se queda dormido reteniendo la exclusión mutua y se previene el interbloqueo por retención y espera.

### 3.3 Manejo de Señales y Tiempo Límite de 60 Segundos
- En `servidorISE` y `agentePais` se ignora `SIGPIPE` con `signal(SIGPIPE, SIG_IGN)` para que, si un extremo del pipe se cierra inesperadamente, `write()` retorne `-1` y el proceso pueda cerrar sus descriptores y liberar su memoria.
- En `servidorISE` se instala `manejador_senal` con `signal(SIGALRM, manejador_senal)` y `signal(SIGINT, manejador_senal)` y se activa el reloj del kernel con `alarm(60)`.
- Si se cumplen los 60 segundos o el usuario presiona `Ctrl-C`, el manejador escribe un mensaje `MSG_STOP` en `fd_aviso` (`fd_ficticio`), despertando de inmediato al hilo recolector bloqueado en `read()`. El recolector envía a su vez un `MSG_STOP` al buffer de cada consumidor activo para que todos los hilos terminen ordenadamente antes de `pthread_join`, se imprima el reporte y se borre el pipe con `unlink`.

### 3.4 Cálculo del ISE y Recomendación
1. **Promedio por sector:** Para cada uno de los 5 sectores se acumulan la suma de sentimientos y la cantidad de encuestas recibidas, y al final se divide una sola vez (`suma[s] / cuenta[s]`).
2. **ISE del país (promedio ponderado):**
   - Industria: **40%** (`0.40`)
   - Servicios: **30%** (`0.30`)
   - Consumidores: **20%** (`0.20`)
   - Comercio: **5%** (`0.05`)
   - Construcción: **5%** (`0.05`)
3. **ISE Global:** Promedio simple de los ISE de los países que enviaron datos.
4. **Clasificación:**
   - Mayor a `55.0` $\rightarrow$ **`Vender`**
   - Entre `45.0` y `55.0` (incluidos) $\rightarrow$ **`Neutro`**
   - Menor a `45.0` $\rightarrow$ **`Comprar`**

---

## 4. Requisitos para Compilar y Ejecutar

Como el proyecto utiliza llamadas al sistema propias de **UNIX / POSIX** (`mkfifo` para pipes nominales, `alarm`, señales `SIGALRM` y `SIGPIPE`, hilos `pthread` y semáforos `sem_t`), **no se ejecuta directamente en CMD o PowerShell de Windows**, sino que requiere un entorno **Linux**.

Herramientas necesarias dentro del entorno Linux:
- **`gcc`:** Compilador de C con soporte para el estándar ANSI C89 (`-std=c89`) y librería `-pthread`.
- **`make`:** Para compilar los tres programas automáticamente usando el `Makefile`.
- **`bash` y `awk`:** Para correr los scripts de prueba de la carpeta `tests/`.

Si estás trabajando en **Windows o macOS**, puedes correr todo el proyecto de dos maneras:

### Opción A: Usando Docker (Recomendado en Windows)

1. Instala y abre **Docker Desktop**.
2. En una primera terminal (PowerShell o CMD) ubicada en la carpeta del proyecto (`d:\Indicador_Economico`), levanta un contenedor con `gcc` y `make` compartiendo los archivos de tu carpeta:
   ```powershell
   docker run -it --rm --name ise_contenedor -v "${PWD}:/proyecto" -w /proyecto gcc:latest bash
   ```
3. Para abrir **terminales adicionales** conectadas exactamente al mismo contenedor (para lanzar cada `agentePais` mientras `servidorISE` espera en la primera terminal), abre otra ventana de PowerShell y ejecuta:
   ```powershell
   docker exec -it ise_contenedor bash
   ```
4. **Nota importante sobre el pipe nominal en Docker:** cuando compartes una carpeta de Windows (`-v`) con un contenedor Linux, el sistema de archivos de Windows no permite crear archivos especiales FIFO (`mkfifo`) dentro de la carpeta montada. Por eso, al correr en Docker, pásale al parámetro `-p` una ruta dentro de `/tmp` (por ejemplo `-p /tmp/ise_fifo`), ya que `/tmp` vive en el sistema de archivos nativo de Linux del contenedor.

### Opción B: Usando WSL2 (Ubuntu en Windows) o Linux Nativo

1. Abre tu terminal de Ubuntu / WSL2 e instala las herramientas de compilación si aún no las tienes:
   ```bash
   sudo apt update && sudo apt install -y build-essential
   ```
2. Entra a la carpeta del proyecto (por ejemplo `cd /mnt/d/Indicador_Economico`).
3. Nota: igual que en Docker, si estás dentro de `/mnt/d/...` (disco de Windows montado en WSL), usa `-p /tmp/ise_fifo` para que el pipe nominal se cree en el sistema de archivos nativo de Linux.

---

## 5. Compilación

Dentro de la terminal de Linux (en Docker, WSL o Linux nativo), en la raíz del proyecto:

```bash
make clean && make
```

Esto compila todos los módulos sin advertencias y genera los tres ejecutables:
- `./generadorCSV`
- `./servidorISE`
- `./agentePais`

---

## 6. Instrucciones de Ejecución

Las banderas de los tres programas se pueden pasar **en cualquier orden** gracias al uso de `getopt`.

### Paso 1: Generar los archivos CSV de los países
```bash
./generadorCSV -p ES -c 1500
./generadorCSV -p FR -c 2500
./generadorCSV -p IT -c 2000
```

### Paso 2: Iniciar el servidor en la Terminal 1
```bash
./servidorISE -b 100 -p /tmp/ise_fifo -a 3
```
- `-b`: tamaño del buffer circular de cada agente (en cantidad de registros).
- `-p`: nombre/ruta del pipe nominal que creará el servidor.
- `-a`: cantidad de agentes habilitados (entre 1 y 4).

### Paso 3: Ejecutar cada agente en una terminal distinta
Si usas Docker, abre cada terminal nueva con `docker exec -it ise_contenedor bash`:

En la **Terminal 2**:
```bash
./agentePais -n encuestas_ES.csv -p /tmp/ise_fifo
```
En la **Terminal 3**:
```bash
./agentePais -n encuestas_FR.csv -p /tmp/ise_fifo
```
En la **Terminal 4**:
```bash
./agentePais -n encuestas_IT.csv -p /tmp/ise_fifo
```

---

## 7. Pruebas Automáticas

Para verificar la generación de un archivo CSV y todas sus restricciones:
```bash
./generadorCSV -p FR -c 25 && bash tests/verificar_csv.sh encuestas_FR.csv 25
```

Para correr la prueba completa de extremo a extremo con el ejemplo oficial de Francia (`ISE = 50.1105 -> Neutro`):
```bash
make && bash tests/prueba_ise.sh
```
