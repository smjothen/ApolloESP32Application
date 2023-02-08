ApolloESP32Application
====================

This is the Zaptec Go/Apollo ESP application.

The application uses a fork of [Espressif IoT Development Framework](https://github.com/espressif/esp-idf).
Please check [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/v4.2.4/get-started/index.html) for getting started instructions.

## Build instructions ##

The application can be built for development or production. Once build type has been selected run `idf.py build`. Once built `idf.py flash` can be used to flash a charger or `python mark_build.py` can be used to create a version for OTA.

#### Development build #####

This build has no configuration restrictions. There are however a few options that could improve development experience. These options chan be found at the following menuconfig paths:

* (Top)->Zaptec->ZAPTEC_USE_ADVANCED_CONSOLE: Allows sending commands to the charger via console.
* (Top)->Zaptec->ZAPTEC_ENABLE_LOGGING: Enables logging via UART.
* (Top)->Zaptec->ZAPTEC_RUN_FACTORY_TESTS: Makes the charger attempt to connect to ZapProgram for production test. When enabled it will display additional options to affect test procedure.
* (Top)->Component config->Zaptec cloud->ZAPTEC_CLOUD_USE_DEVELOPMENT_URL: Changes the url the charger will connect to.

#### Production build ####

This build has configuration restrictions to ensure at least a minimum set of security features and configuration validity. The required security options to select production build can be found at the following menuconfig paths:

* (Top)->Security features->SECURE_BOOT
* (Top)->Security features->SECURE_FLASH_ENC_ENABLED
* (Top)->Security features->SECURE_FLASH_ENC_ENABLED->SECURE_FLASH_ENCRYPTION_MODE_RELEASE
* (Top)->Security features->SECURE_BOOT_INSECURE **Must be N selected (off)**
* (Top)->Component config->NVS->NVS_ENCRYPTION

In addition `(Top)->Component config->Zaptec cloud->ZAPTEC_CLOUD_USE_DEVELOPMENT_URL` must be N selected.
