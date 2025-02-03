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
#include "dht.h"
#include "dht_params.h"
#include "net/gnrc.h"
#include "net/gnrc/icmpv6.h"
#include "net/gnrc/ipv6.h"

#define EMCUTE_ID           ("gertrud")
#define EMCUTE_PRIO         (THREAD_PRIORITY_MAIN - 1)
#define PUERTO_BROKER       (1885U)
#define DIRECCION_BROKER    "2001:db8:a::1"
#define ID_NODO             "4"     		// Número del nodo
#define NOMBRE_NODO         "Anthony Ibujes"	// Nombre del nodo

// Configuración del DHT11
#define PIN_DHT             GPIO_PIN(0, 25)
#define TIPO_DHT            DHT11
#define RETRASO_SENSOR      10		// Retraso entre lecturas en segundos

// Configuración del pin para leer el estado de estrés
#define ESTADO_PIN          GPIO_PIN(0, 35) // Pin donde se recibe el estado (0 o 1)

// Configuración UART para comunicación con Xiao Sense
#define UART_USED           UART_DEV(0)  // Usar UART0 (GPIO 1 y GPIO 3)
#define UART_BAUD           115200
#define REQUEST_PIN         GPIO_PIN(0, 22)

static char pila[THREAD_STACKSIZE_DEFAULT];
static char pila_icmp[THREAD_STACKSIZE_DEFAULT];
static msg_t cola[8];
static dht_t dht;
static bool sensor_inicializado = false;
static bool envio_automatico_activo = false;
static char pila_envio_automatico[THREAD_STACKSIZE_DEFAULT];
static kernel_pid_t pid_envio_automatico = KERNEL_PID_UNDEF;
static kernel_pid_t pid_icmp;
static char ultimo_estado_estres[3] = "01";  // Buffer para estado

// Prototipo de la función
static int iniciar_envio_automatico(void);

// Función para manejar paquetes ICMPv6 (ping)
static void *hilo_icmp(void *arg)
{
    (void)arg;
    msg_t msg;
    msg_t msg_queue[8];
    msg_init_queue(msg_queue, 8);

    while (1) {
        msg_receive(&msg);
        gnrc_pktsnip_t *pkt = (gnrc_pktsnip_t *)msg.content.ptr;
        if (pkt->type == GNRC_NETTYPE_ICMPV6) {
            gnrc_icmpv6_echo_reply(pkt);
        }
        gnrc_pktbuf_release(pkt);
    }
    return NULL;
}

// Función para solicitar y leer datos del Xiao Sense
static int leer_estado_estres(void) {
    // Leer el estado del pin
    int estado = gpio_read(ESTADO_PIN); // Leer el estado del pin (0 o 1)

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

// Inicializar sensor DHT11
static void inicializar_dht_silencioso(void) {
    dht_params_t parametros = {
        .pin = PIN_DHT,
        .type = TIPO_DHT
    };
    for (int i = 0; i < 5; i++) {
        if (dht_init(&dht, &parametros) == DHT_OK) {
            sensor_inicializado = true;
            puts("Sensor DHT11 inicializado correctamente");
            return;
        }
        puts("Advertencia: No se pudo inicializar el sensor DHT11, reintentando...");
        xtimer_sleep(1);
    }
    puts("Advertencia: No se pudo inicializar el sensor DHT11 después de varios intentos.");
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
    float temp_c = 0.0;
    float hum_perc = 0.0;

    if (!sensor_inicializado) {
        puts("Error: Sensor no inicializado.");
        return 1;
    }

    printf("\n------------------------------------------------------------------\n");
    
    // Leer DHT11
    int intentos = 3;
    int16_t temp, hum;
    while (intentos--) {
        if (dht_read(&dht, &temp, &hum) == DHT_OK) {
            temp_c = temp / 10.0;
            hum_perc = hum / 10.0;
            printf("Lectura DHT11 exitosa: Temp=%.1f°C, Hum=%.1f%%\n", temp_c, hum_perc);
            break;
        }
        puts("Error al leer el sensor DHT11, reintentando...");
        xtimer_sleep(1);
    }

    if (intentos < 0) {
        puts("Error: No se pudo leer el sensor DHT11 después de varios intentos.");
        return 1;
    }

    // Leer estado de estrés del Xiao Sense
    if (leer_estado_estres() != 0) {
        puts("Error al leer el estado de estrés.");
    }

    // Publicar datos en subtemas
    snprintf(tema, sizeof(tema), "sensores/nodo_%s/nombre", ID_NODO);
    snprintf(datos, sizeof(datos), "\"%s\"", NOMBRE_NODO);
    publicar_datos_sensor(tema, datos);

    snprintf(tema, sizeof(tema), "sensores/nodo_%s/temperatura", ID_NODO);
    snprintf(datos, sizeof(datos), "%.1f", temp_c);
    publicar_datos_sensor(tema, datos);

    snprintf(tema, sizeof(tema), "sensores/nodo_%s/humedad", ID_NODO);
    snprintf(datos, sizeof(datos), "%.1f", hum_perc);
    publicar_datos_sensor(tema, datos);

    // Publicar estado de estrés
    snprintf(tema, sizeof(tema), "sensores/nodo_%s/estado_estres", ID_NODO);
    snprintf(datos, sizeof(datos), "%s", ultimo_estado_estres);
    publicar_datos_sensor(tema, datos);

    // Log en consola
    printf("\nNodo %s (%s) - Temp: %.1f°C, Humedad: %.1f%%, Estado: %s\n",
           ID_NODO, NOMBRE_NODO, temp_c, hum_perc, ultimo_estado_estres);
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
    puts("Aplicación de ejemplo MQTT-SN con sensor DHT11 y Xiao Sense\n");
    puts("Escriba 'help' para comenzar. Consulte el archivo README.md para más información.");

    // Inicializar cola de mensajes
    msg_init_queue(cola, ARRAY_SIZE(cola));

    // Iniciar hilo para manejar pings (ICMPv6)
    pid_icmp = thread_create(pila_icmp, sizeof(pila_icmp),
                           THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST,
                           hilo_icmp, NULL, "icmpv6");

    // Registrar el hilo ICMP para recibir paquetes ICMPv6
    gnrc_netreg_entry_t icmp_entry = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL,
                                                                pid_icmp);
    gnrc_netreg_register(GNRC_NETTYPE_ICMPV6, &icmp_entry);

    // Inicializar UART para comunicación con Xiao Sense
    uart_init(UART_USED, UART_BAUD, NULL, NULL);
    puts("UART inicializado para comunicación con Xiao Sense");

    // Configurar pin de estado como entrada
    gpio_init(ESTADO_PIN, GPIO_IN);

    // Iniciar hilo emcute
    thread_create(pila, sizeof(pila), EMCUTE_PRIO, 0,
                  hilo_emcute, NULL, "emcute");

    // Inicializar DHT11 y verificar su estado
    inicializar_dht_silencioso();

    if (sensor_inicializado) {
        puts("Sensor DHT11 inicializado correctamente. Procediendo con la conexión al broker...");
        xtimer_sleep(2);
        
        if (iniciar_sistema() != 0) {
            puts("Iniciando shell debido a fallo en la conexión...");
        }
    } else {
        puts("El sensor DHT11 no se inicializó correctamente. Iniciando shell para comandos manuales.");
    }

    // Iniciar shell
    char buffer_linea[SHELL_DEFAULT_BUFSIZE];
    shell_run(comandos_shell, buffer_linea, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
