#pragma once

#include "HomeSpan.h"

#include "AppConfig.h"
#include "HKHeaterCooler.h"
#include "MitsubishiProtocol.h"

namespace HomeKitManager {

static void begin(MitsubishiProtocol* proto) {
    homeSpan.setPairingCode(AppConfig::HOMEKIT_PAIRING_CODE);
    homeSpan.setQRID(AppConfig::HOMEKIT_QR_ID);
    homeSpan.setLogLevel(AppConfig::HOMEKIT_LOG_LEVEL);
    homeSpan.begin(static_cast<Category>(AppConfig::HOMEKIT_CATEGORY), AppConfig::HOMEKIT_DEVICE_NAME);

    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name(AppConfig::HOMEKIT_BRIDGE_NAME);
        new Characteristic::Manufacturer(AppConfig::HOMEKIT_MANUFACTURER);
        new Characteristic::Model(AppConfig::HOMEKIT_BRIDGE_MODEL);
        new Characteristic::FirmwareRevision(AppConfig::HOMEKIT_FIRMWARE_REVISION);

    new SpanAccessory();
      new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name(AppConfig::HOMEKIT_DEVICE_NAME);
        new Characteristic::Manufacturer(AppConfig::HOMEKIT_MANUFACTURER);
        new Characteristic::Model(AppConfig::HOMEKIT_DEVICE_MODEL);
        new Characteristic::FirmwareRevision(AppConfig::HOMEKIT_FIRMWARE_REVISION);
      new HKHeaterCooler(proto);
}

static void poll() {
    homeSpan.poll();
}

}  // namespace HomeKitManager
