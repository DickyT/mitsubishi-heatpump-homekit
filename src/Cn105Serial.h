#pragma once

#include <Arduino.h>

#include "AppConfig.h"

class Cn105Serial {
public:
    static void begin(HardwareSerial& serial) {
        serial.begin(
            AppConfig::CN105_UART_BAUD,
            SERIAL_8E1,
            AppConfig::CN105_RX_PIN,
            AppConfig::CN105_TX_PIN
        );

        while (serial.available() > 0) {
            serial.read();
        }

        Serial.printf("[UART] CN105 serial initialized: uart=%d rx=%d tx=%d baud=%lu format=8E1\n",
                      AppConfig::CN105_UART_PORT,
                      AppConfig::CN105_RX_PIN,
                      AppConfig::CN105_TX_PIN,
                      static_cast<unsigned long>(AppConfig::CN105_UART_BAUD));
    }
};
