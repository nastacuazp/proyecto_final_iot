#include "periph/gpio.h"
#include "xtimer.h"
#include <stdio.h>
#include "dht11.h"

int dht11_read(uint8_t pin, int *temperature, int *humidity) {
    uint8_t data[5] = {0};
    int i, j;

    gpio_set(pin);
    gpio_clear(pin);
    xtimer_usleep(20000); // 20 ms para iniciar comunicación
    gpio_set(pin);
    xtimer_usleep(40); // Pausa mínima después del inicio

    // Esperar respuesta del sensor
    if (!gpio_read(pin)) {
        puts("Error: No se detectó respuesta del sensor.");
        return -1;
    }

    for (j = 0; j < 5; j++) {
        for (i = 7; i >= 0; i--) {
            int timeout = 10000;
            while (!gpio_read(pin) && timeout-- > 0) {
                xtimer_usleep(1); // Verificar señal
            }
            if (timeout <= 0) {
                puts("Error: Tiempo de espera agotado.");
                return -1;
            }

            xtimer_usleep(30); // Leer bit
            if (gpio_read(pin)) {
                data[j] |= (1 << i);
            }
            while (gpio_read(pin)) {} // Esperar a que baje
        }
    }

    // Verificación de datos
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        puts("Error: Fallo en la verificación de datos.");
        return -1;
    }

    *humidity = (data[0] << 8) | data[1];
    *temperature = (data[2] << 8) | data[3];

    return 0; // Éxito
}



