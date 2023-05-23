#ifndef EFUSE_H
#define EFUSE_H

#include "esp_err.h"
/**
 * @brief Checks if encryption is enabled. If it is enabled then it prevents it from being disabled.
 *
 * @details Checks the encryption cnt efuse if encryption is enabled. If it is enabled then it burns all bits in the cnt to 1,
 * preventing any changes to the cnt.
 */
esp_err_t lock_encryption_on_if_enabled();

#endif /* EFUSE_H */
