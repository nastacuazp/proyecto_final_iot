#include <stdio.h>
#include "shell.h"
#include "msg.h"
#include "net/gnrc/netif.h"
#include "net/netopt.h"
#include "net/ipv6/addr.h"
#include "ws281x.h"
#include "net/sock/udp.h"
#include "thread.h" // Incluir el encabezado para manejar hilos

#define MAIN_QUEUE_SIZE     (8)
static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

// Inicializar el LED RGB
static ws281x_t led_strip;
static uint8_t led_strip_buf[WS281X_BYTES_PER_DEVICE * WS281X_PARAM_NUMOF];
static ws281x_params_t led_params = {
    .buf = led_strip_buf,
    .numof = 1,
    .pin = GPIO48
};

// Definir el tamaño del stack para el hilo UDP
#define UDP_THREAD_STACKSIZE  (THREAD_STACKSIZE_DEFAULT)

// Crear un stack para el hilo UDP
char udp_thread_stack[UDP_THREAD_STACKSIZE];

// Función para configurar el canal de las interfaces inalámbricas
void set_wifi_channel(void) {
    gnrc_netif_t *netif = gnrc_netif_iter(NULL);
    int channel = 6;
    while (netif) {
        if (gnrc_netapi_set(netif->pid, NETOPT_CHANNEL, 0, &channel, sizeof(channel)) < 0) {
            printf("Error al configurar el canal en la interfaz %d\n", netif->pid);
        } else {
            printf("Canal configurado correctamente en la interfaz %d: %d\n", netif->pid, channel);
        }
        netif = gnrc_netif_iter(netif);
    }
}

// Función para configurar la dirección IPv6 en la interfaz 10
void configure_ipv6_address(void) {
    gnrc_netif_t *netif = gnrc_netif_iter(NULL);
    ipv6_addr_t addr;
    
    if (ipv6_addr_from_str(&addr, "2001:db8:a::2") == NULL) {
        printf("Error: dirección IPv6 inválida\n");
        return;
    }

    while (netif) {
        if (netif->pid == 10) {
            if (gnrc_netif_ipv6_addr_add(netif, &addr, 64, GNRC_NETIF_IPV6_ADDRS_FLAGS_STATE_VALID) < 0) {
                printf("Error al configurar la dirección IPv6 en la interfaz 10\n");
            } else {
                printf("Dirección IPv6 2001:db8:a::2 configurada correctamente en la interfaz 10\n");
            }
            break;
        }
        netif = gnrc_netif_iter(netif);
    }
    
    if (netif == NULL) {
        printf("Error: No se encontró la interfaz 10\n");
    }
}

// Función para encender el LED RGB
void led_on(void) {
    color_rgb_t white = {0, 168, 243};
    ws281x_set(&led_strip, 0, white);
    ws281x_prepare_transmission(&led_strip);
    ws281x_write(&led_strip);
    puts("LED encendido.");
}

// Función para apagar el LED RGB
void led_off(void) {
    color_rgb_t off = {0, 0, 0};
    ws281x_set(&led_strip, 0, off);
    ws281x_prepare_transmission(&led_strip);
    ws281x_write(&led_strip);
    puts("LED apagado.");
}

// Comando para encender el LED
static int comando_led_on(int argc, char **argv) {
    led_on();
    return 0;
}

// Comando para apagar el LED
static int comando_led_off(int argc, char **argv) {
    led_off();
    return 0;
}

// Comandos del shell
static const shell_command_t comandos_shell[] = {
    { "led_on", "Enciende el LED RGB", comando_led_on },
    { "led_off", "Apaga el LED RGB", comando_led_off },
    { NULL, NULL, NULL }
};

// Función para manejar los mensajes UDP
void udp_listener(void) {
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    sock_udp_t sock;
    local.port = 12345;

    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        puts("Error creando el socket UDP");
        return;
    }

    while (1) {
        sock_udp_ep_t remote;
        uint8_t buf[128];
        ssize_t res;

        if ((res = sock_udp_recv(&sock, buf, sizeof(buf), SOCK_NO_TIMEOUT, &remote)) >= 0) {
            buf[res] = '\0'; // Asegurarse de que el buffer sea una cadena
            printf("Mensaje recibido: %s\n", buf);
            if (strcmp((char *)buf, "ON") == 0) {
                led_on();
            } else if (strcmp((char *)buf, "OFF") == 0) {
                led_off();
            }
        }
    }
}

// Hilo para el listener UDP
static void *udp_thread(void *arg) {
    udp_listener();
    return NULL;
}

int main(void) {
    // Inicializar el LED RGB
    ws281x_init(&led_strip, &led_params);

    // Configurar el canal inalámbrico
    set_wifi_channel();

    // Configurar la dirección IPv6
    configure_ipv6_address();

    // Inicializar la cola de mensajes
    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);
    puts("RIOT border router example application");

    // Crear un hilo para el listener UDP
    thread_create(udp_thread_stack, sizeof(udp_thread_stack), THREAD_PRIORITY_MAIN - 1, 0, udp_thread, NULL, "udp_listener");

    // Iniciar el shell
    puts("All up, running the shell now");
    char line_buf[SHELL_DEFAULT_BUFSIZE];

    // Iniciar el shell con los comandos
    shell_run(comandos_shell, line_buf, SHELL_DEFAULT_BUFSIZE);

    // Esto nunca debería alcanzarse
    return 0;
}
