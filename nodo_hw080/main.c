#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include "periph/uart.h"
#include "periph/gpio.h"
#include "shell.h"
#include "msg.h"
#include "net/emcute.h"
#include "net/ipv6/addr.h"
#include "thread.h"
#include "xtimer.h"
#include "periph/adc.h"

#define EMCUTE_ID           ("gertrud")
#define EMCUTE_PRIO         (THREAD_PRIORITY_MAIN - 1)
#define PUERTO_BROKER       (1885U)
#define DIRECCION_BROKER    "2001:db8:a::1"
#define ID_NODO             "1"     		// Número del nodo
#define NOMBRE_NODO         "Jordan Manguay"	// Nombre del nodo

// Configuración del HW080
#define HW080_PIN ADC_LINE(4)
#define RETRASO_SENSOR      10		// Retraso entre lecturas en segundos

// Configuración del pin para leer el estado de estrés
#define ESTADO_PIN          GPIO_PIN(0, 35) // Pin donde se recibe el estado (0 o 1)

// Configuración UART para comunicación con Xiao Sense
#define UART_USED           UART_DEV(0)  // Usar UART0 (GPIO 1 y GPIO 3)
#define UART_BAUD           115200
#define REQUEST_PIN         GPIO_PIN(0, 22)

static char pila[THREAD_STACKSIZE_DEFAULT];
static msg_t cola[8];
static bool sensor_inicializado = false;
static bool envio_automatico_activo = false;
static char pila_envio_automatico[THREAD_STACKSIZE_DEFAULT];
static kernel_pid_t pid_envio_automatico = KERNEL_PID_UNDEF;
static char ultimo_estado_estres[3] = "01";  // Buffer para estado

// Prototipo de la función
static int iniciar_envio_automatico(void);

// Función para solicitar y leer datos del Xiao Sense
static int leer_estado_estres(void) {
    // Leer el estado del pin
    int estado = gpio_read(ESTADO_PIN); // Leer el estado del pin (0 o 1)
    printf("Valor leído del pin de estado: %d\n", estado); // Mensaje de depuración

    // Asegúrate de que el dato recibido sea "0" o "1"
    if (estado == 0 || estado == 1) {
        ultimo_estado_estres[0] = '0' + estado; // Convertir a char
        ultimo_estado_estres[1] = '\0'; // Asegúrate de que esté terminado en null
        printf("Estado de estrés recibido: %s\n", ultimo_estado_estres);
        return 0; // Éxito
    } else {
        printf("Error: Estado no válido leído del pin\n");
    }

    printf("Error al leer estado de estrés\n");
    return -1; // Error
}

// Inicializar sensor HW080
static int init_hw080(void) {
    if (adc_init(HW080_PIN) < 0) {
        printf("Error inicializando sensor HW-080\n");
        return -1;
    }
    return 0;
}

// Lee el valor de humedad del sensor HW-080
static float read_soil_moisture(void) {
    int raw_value = adc_sample(HW080_PIN, ADC_RES_12BIT);
    
    // Convertir valor analógico a porcentaje de humedad
    // Nota: Los valores exactos dependerán de tu calibración específica
    float moisture_percentage = 100.0 - ((raw_value / 4095.0) * 100.0);
    
    return moisture_percentage;
}

static void *hilo_emcute(void *arg) {
    (void)arg;
    emcute_run(CONFIG_EMCUTE_DEFAULT_PORT, EMCUTE_ID);
    return NULL;
}

static int conectar_broker(void) {
    sock_udp_ep_t gw = { .family = AF_INET6, .port = PUERTO_BROKER };

    printf("Intentando conectar al broker en [%s]:%i...\n", DIRECCION_BROKER, PUERTO_BROKER);
    if (ipv6_addr_from_str((ipv6_addr_t *)&gw.addr.ipv6, DIRECCION_BROKER) == NULL) {
        puts("error: no se puede analizar la dirección del broker");
        return 1;
    }

    if (emcute_con(&gw, true, NULL, NULL, 0, 0) != EMCUTE_OK) {
        printf("error: no se puede conectar a [%s]:%i\n", DIRECCION_BROKER, (int)PUERTO_BROKER);
        return 1;
    }

    printf("Conectado exitosamente al broker en [%s]:%i\n", DIRECCION_BROKER, (int)PUERTO_BROKER);
    return 0;
}

static int publicar_datos_sensor(const char *nombre_tema, const char *datos) {
    emcute_topic_t t;
    t.name = nombre_tema;

    if (emcute_reg(&t) != EMCUTE_OK) {
        printf("error: no se puede registrar el tema '%s'\n", nombre_tema);
        return 1;
    }

    if (emcute_pub(&t, datos, strlen(datos), EMCUTE_QOS_0) != EMCUTE_OK) {
        printf("error: no se puede publicar datos en el tema '%s [%i]'\n", t.name, (int)t.id);
        return 1;
    }

    printf("Publicado en tema '%s': %s\n", nombre_tema, datos);
    return 0;
}

static int enviar_lectura_unica(void) {
    char tema[64];
    char datos[64];
    float humedad_suelo = 0.0;

    if (!sensor_inicializado) {
        puts("Error: Sensor no inicializado.");
        return 1;
    }

    printf("\n------------------------------------------------------------------\n");
    
    // Leer humedad del suelo
    humedad_suelo = read_soil_moisture();
    printf("Lectura de humedad del suelo exitosa: Humedad=%.1f%%\n", humedad_suelo);

    // Leer estado de estrés del Xiao Sense
    if (leer_estado_estres() != 0) {
        puts("Error al leer el estado de estrés.");
    }

    // Publicar datos en subtemas
    snprintf(tema, sizeof(tema), "sensores/nodo_%s/nombre", ID_NODO);
    snprintf(datos, sizeof(datos), "\"%s\"", NOMBRE_NODO);
    publicar_datos_sensor(tema, datos);

    snprintf(tema, sizeof(tema), "sensores/nodo_%s/humedad_suelo", ID_NODO);
    snprintf(datos, sizeof(datos), "%.1f", humedad_suelo);
    publicar_datos_sensor(tema, datos);

    // Publicar estado de estrés
    snprintf(tema, sizeof(tema), "sensores/nodo_%s/estado_estres", ID_NODO);
    snprintf(datos, sizeof(datos), "%s", ultimo_estado_estres);
    printf("Publicando estado de estrés: %s\n", ultimo_estado_estres); // Mensaje de depuración
    publicar_datos_sensor(tema, datos);

    // Log en consola
    printf("\nNodo %s (%s) - Humedad del suelo: %.1f%%, Estado: %s\n",
           ID_NODO, NOMBRE_NODO, humedad_suelo, ultimo_estado_estres);
    printf("------------------------------------------------------------------\n");

    return 0;
}

static void *hilo_envio_automatico(void *arg) {
    (void)arg;
    
    while (envio_automatico_activo) {
        printf("Enviando lectura única...\n");
        enviar_lectura_unica();
        xtimer_sleep(RETRASO_SENSOR);
    }
    
    pid_envio_automatico = KERNEL_PID_UNDEF;
    return NULL;
}

static int comando_enviar_datos(int argc, char **argv) {
    (void)argc;
    (void)argv;

    // Conectar automáticamente al broker si no está conectado
    if (emcute_discon() == EMCUTE_NOGW) {
        printf("No conectado al broker. Conectando a %s...\n", DIRECCION_BROKER);
        if (conectar_broker() != 0) {
            return 1; // No se puede conectar, se envía al shell
        }
    }

    iniciar_envio_automatico();
    return 0;
}

static const shell_command_t comandos_shell[] = {
    { "enviar_datos", "envía datos del sensor al broker", comando_enviar_datos },
    { NULL, NULL, NULL }
};

static int iniciar_envio_automatico(void) {
    if (!envio_automatico_activo) {
        envio_automatico_activo = true;
        // Iniciar el hilo para envíos
        pid_envio_automatico = thread_create(pila_envio_automatico, sizeof(pila_envio_automatico),
                                    THREAD_PRIORITY_MAIN - 1, 0,
                                    hilo_envio_automatico, NULL, "envio_automatico");
        puts("Iniciando envío automático de datos cada 10 segundos...");
    }
    return 0;
}

static int iniciar_sistema(void) {
    // Intenta conectar al broker
    printf("Intentando conectar al broker en %s...\n", DIRECCION_BROKER);
    if (conectar_broker() != 0) {
        puts("Error: No se pudo conectar al broker. Iniciando shell para comandos manuales.");
        return 1;
    }
    
    // Si llegamos aquí, la conexión fue exitosa
    puts("Conexión exitosa al broker. Iniciando envío automático...");
    iniciar_envio_automatico();
    return 0;
}

int main(void) {
    puts("Aplicación de ejemplo MQTT-SN con sensor HW080 y Xiao Sense\n");
    puts("Escriba 'help' para comenzar. Consulte el archivo README.md para más información.");

    // Inicializar cola de mensajes
    msg_init_queue(cola, ARRAY_SIZE(cola));

    // Inicializar UART para comunicación con Xiao Sense
    uart_init(UART_USED, UART_BAUD, NULL, NULL);
    puts("UART inicializado para comunicación con Xiao Sense");

    // Configurar pin de estado como entrada
    gpio_init(ESTADO_PIN, GPIO_IN); // Configurar el pin de estado como entrada

    // Iniciar hilo emcute
    thread_create(pila, sizeof(pila), EMCUTE_PRIO, 0,
                  hilo_emcute, NULL, "emcute");

    // Inicializar HW080 y verificar su estado
    if (init_hw080() == 0) {
        sensor_inicializado = true;
        puts("Sensor HW080 inicializado correctamente. Procediendo con la conexión al broker...");
        xtimer_sleep(2);
        
        if (iniciar_sistema() != 0) {
            puts("Iniciando shell debido a fallo en la conexión...");
        }
    } else {
        puts("El sensor HW080 no se inicializó correctamente. Iniciando shell para comandos manuales.");
    }

    // Iniciar shell
    char buffer_linea[SHELL_DEFAULT_BUFSIZE];
    shell_run(comandos_shell, buffer_linea, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
