#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

int dht11_read(uint8_t pin, int *temperature, int *humidity);

#endif


