#ifndef CORES3_USB_LOG_DRIVER_H
#define CORES3_USB_LOG_DRIVER_H
#include "driver/usb_serial_jtag.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Private, patched 4.4.7 driver. Do not install Arduino HWCDC or the SDK's
 * usb_serial_jtag driver on the same peripheral. */
esp_err_t cores3_usb_driver_install(usb_serial_jtag_driver_config_t *config);
esp_err_t cores3_usb_driver_uninstall(void);
int cores3_usb_write_bytes(const void *src, size_t size, TickType_t wait);
int cores3_usb_read_bytes(void *dst, uint32_t size, TickType_t wait);
/* Bytes accepted by the peripheral FIFO, not acknowledgement of PC storage. */
uint32_t cores3_usb_tx_bytes(void);
#ifdef __cplusplus
}
#endif
#endif
