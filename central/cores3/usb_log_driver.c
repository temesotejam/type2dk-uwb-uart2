/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for type2dk-uwb-uart2 (2026-09-18): backport upstream TX idle
 * scheduling, prime initial TX, retain partial writes, fix allocation checks.
 * Sources and license: docs/PROVENANCE.md and licenses/Apache-2.0.txt.
 */

#include <string.h>
#include <stdbool.h>
#include <assert.h>
#ifdef CORES3_USB_HOST_TEST
#include "mock_usb_driver.h"
#else
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/usb_phy_ll.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_intr_alloc.h"
#include "usb_log_driver.h"
#include "soc/periph_defs.h"
#include "esp_check.h"
#endif

// The hardware buffer max size is 64
#define USB_SER_JTAG_ENDP_SIZE          (64)
#define USB_SER_JTAG_RX_MAX_SIZE        (64)

typedef struct{
    intr_handle_t intr_handle;          /*!< USB-SERIAL-JTAG interrupt handler */

    // RX parameters
    RingbufHandle_t rx_ring_buf;        /*!< RX ring buffer handler */
    uint32_t rx_buf_size;               /*!< TX buffer size */
    uint8_t rx_data_buf[USB_SER_JTAG_ENDP_SIZE];            /*!< Data buffer to stash FIFO data */

    // TX parameters
    uint32_t tx_buf_size;               /*!< TX buffer size */
    RingbufHandle_t tx_ring_buf;        /*!< TX ring buffer handler */
    uint8_t tx_data_buf[USB_SER_JTAG_ENDP_SIZE];  /*!< Data buffer to stash TX FIFO data */
    size_t tx_stash_cnt;                          /*!< Number of stashed TX FIFO bytes */
} usb_serial_jtag_obj_t;

static usb_serial_jtag_obj_t *p_usb_serial_jtag_obj = NULL;
static volatile uint32_t usb_tx_bytes = 0;

uint32_t cores3_usb_tx_bytes(void) { return usb_tx_bytes; }

static const char* USB_SERIAL_JTAG_TAG = "usb_serial_jtag";

static size_t usb_serial_jtag_write_and_flush(const uint8_t *buf, uint32_t wr_len)
{
    size_t size = usb_serial_jtag_ll_write_txfifo(buf, wr_len);
    usb_tx_bytes += (uint32_t)size;
    usb_serial_jtag_ll_txfifo_flush();
    return size;
}

/* TX scheduling follows ESP-IDF 5.5: preserve a wakeup across idle,
 * send a terminating zero-length packet, and recheck the queue after masking.
 * This local 4.4.7-compatible copy does not replace SDK symbols. */
static void usb_serial_jtag_isr_handler_default(void *arg) {
    (void)arg;
    BaseType_t xTaskWoken = 0;
    uint32_t status = usb_serial_jtag_ll_get_intsts_mask();
    if (status & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY) {
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        if (usb_serial_jtag_ll_txfifo_writable()) {
            size_t size = p_usb_serial_jtag_obj->tx_stash_cnt;
            bool stashed = size != 0;
            uint8_t *data = stashed ? p_usb_serial_jtag_obj->tx_data_buf :
                (uint8_t *)xRingbufferReceiveUpToFromISR(p_usb_serial_jtag_obj->tx_ring_buf, &size, USB_SER_JTAG_ENDP_SIZE);
            if (data != NULL && size > 0) {
                size_t sent = usb_serial_jtag_write_and_flush(data, size);
                assert(sent <= size);
                p_usb_serial_jtag_obj->tx_stash_cnt = size - sent;
                if (sent < size) {
                    // A partial stash write overlaps the source and destination.
                    memmove(p_usb_serial_jtag_obj->tx_data_buf, data + sent, size - sent);
                }
                if (!stashed) vRingbufferReturnItemFromISR(p_usb_serial_jtag_obj->tx_ring_buf, data, &xTaskWoken);
            } else {
                if (data != NULL && !stashed)
                    vRingbufferReturnItemFromISR(p_usb_serial_jtag_obj->tx_ring_buf, data, &xTaskWoken);
                // Finish a full 64-byte USB transfer and create the next EMPTY
                // event even while its interrupt is masked. Otherwise a later
                // write can enable an interrupt whose status is permanently 0.
                usb_serial_jtag_ll_txfifo_flush();
                usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
                // A writer on the other CPU may have enqueued while this ISR
                // was disabling TX. Recheck using only FromISR APIs (4.4.7's
                // vRingbufferGetInfo uses task-context critical sections).
                size_t next_size = 0;
                uint8_t *next = xRingbufferReceiveUpToFromISR(p_usb_serial_jtag_obj->tx_ring_buf, &next_size, USB_SER_JTAG_ENDP_SIZE);
                if (next != NULL) {
                    if (next_size) memcpy(p_usb_serial_jtag_obj->tx_data_buf, next, next_size);
                    p_usb_serial_jtag_obj->tx_stash_cnt = next_size;
                    vRingbufferReturnItemFromISR(p_usb_serial_jtag_obj->tx_ring_buf, next, &xTaskWoken);
                    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
                }
            }
        }
    }
    if (status & USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT) {
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
        uint32_t n = usb_serial_jtag_ll_read_rxfifo(p_usb_serial_jtag_obj->rx_data_buf, USB_SER_JTAG_RX_MAX_SIZE);
        xRingbufferSendFromISR(p_usb_serial_jtag_obj->rx_ring_buf, p_usb_serial_jtag_obj->rx_data_buf, n, &xTaskWoken);
    }
    if (xTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

esp_err_t cores3_usb_driver_install(usb_serial_jtag_driver_config_t *usb_serial_jtag_config)
{
    esp_err_t err = ESP_OK;
    ESP_RETURN_ON_FALSE((p_usb_serial_jtag_obj == NULL), ESP_ERR_INVALID_STATE, USB_SERIAL_JTAG_TAG, "Driver already installed");
    ESP_RETURN_ON_FALSE((usb_serial_jtag_config->rx_buffer_size > 0), ESP_ERR_INVALID_ARG, USB_SERIAL_JTAG_TAG, "RX buffer is not prepared");
    ESP_RETURN_ON_FALSE((usb_serial_jtag_config->rx_buffer_size > USB_SER_JTAG_RX_MAX_SIZE), ESP_ERR_INVALID_ARG, USB_SERIAL_JTAG_TAG, "RX buffer prepared is so small, should larger than 64");
    ESP_RETURN_ON_FALSE((usb_serial_jtag_config->tx_buffer_size > 0), ESP_ERR_INVALID_ARG, USB_SERIAL_JTAG_TAG, "TX buffer is not prepared");
    p_usb_serial_jtag_obj = (usb_serial_jtag_obj_t*) heap_caps_calloc(1, sizeof(usb_serial_jtag_obj_t), MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if (p_usb_serial_jtag_obj == NULL) {
        ESP_LOGE(USB_SERIAL_JTAG_TAG, "memory allocate error");
        err = ESP_ERR_NO_MEM;
        goto _exit;
    }

    p_usb_serial_jtag_obj->rx_buf_size = usb_serial_jtag_config->rx_buffer_size;
    p_usb_serial_jtag_obj->tx_buf_size = usb_serial_jtag_config->tx_buffer_size;
    p_usb_serial_jtag_obj->rx_ring_buf = xRingbufferCreate(p_usb_serial_jtag_obj->rx_buf_size, RINGBUF_TYPE_BYTEBUF);
    if (p_usb_serial_jtag_obj->rx_ring_buf == NULL) {
        ESP_LOGE(USB_SERIAL_JTAG_TAG, "ringbuffer create error");
        err = ESP_ERR_NO_MEM;
        goto _exit;
    }

    p_usb_serial_jtag_obj->tx_ring_buf = xRingbufferCreate(usb_serial_jtag_config->tx_buffer_size, RINGBUF_TYPE_BYTEBUF);
    if (p_usb_serial_jtag_obj->tx_ring_buf == NULL) {
        ESP_LOGE(USB_SERIAL_JTAG_TAG, "ringbuffer create error");
        err = ESP_ERR_NO_MEM;
        goto _exit;
    }

    // Enable USB-Serial-JTAG peripheral module clock
    usb_serial_jtag_ll_enable_bus_clock(true);

    // Configure PHY
    usb_phy_ll_int_jtag_enable(&USB_SERIAL_JTAG);

    // Preserve any pending EMPTY event, as in the upstream 5.5 driver.
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);

    err = esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, 0, usb_serial_jtag_isr_handler_default, NULL, &p_usb_serial_jtag_obj->intr_handle);
    if (err != ESP_OK) {
        goto _exit;
    }
    // Prime the endpoint even when the bootloader left no EMPTY status.
    // No TX writer is active yet, so this cannot split a payload packet.
    usb_tx_bytes = 0;
    usb_serial_jtag_ll_txfifo_flush();
    return ESP_OK;

_exit:
    cores3_usb_driver_uninstall();
    return err;
}

int cores3_usb_read_bytes(void* buf, uint32_t length, TickType_t ticks_to_wait)
{
    uint8_t *data = NULL;
    size_t data_read_len = 0;

    if (length == 0) {
        return 0;
    }

    // Recieve new data from ISR
    data = (uint8_t*) xRingbufferReceiveUpTo(p_usb_serial_jtag_obj->rx_ring_buf, &data_read_len, (TickType_t) ticks_to_wait, length);
    if (data == NULL) {
        // If there is no data received from ringbuffer, return 0 directly.
        return 0;
    }

    memcpy((uint8_t*)buf, data, data_read_len);
    vRingbufferReturnItem(p_usb_serial_jtag_obj->rx_ring_buf, data);
    data = NULL;

    return data_read_len;
}

int cores3_usb_write_bytes(const void* src, size_t size, TickType_t ticks_to_wait)
{
    ESP_RETURN_ON_FALSE(size != 0, ESP_ERR_INVALID_ARG, USB_SERIAL_JTAG_TAG, "size should be larger than 0");
    ESP_RETURN_ON_FALSE(src != NULL, ESP_ERR_INVALID_ARG, USB_SERIAL_JTAG_TAG, "Invalid buffer pointer.");
    ESP_RETURN_ON_FALSE(p_usb_serial_jtag_obj != NULL, ESP_ERR_INVALID_ARG, USB_SERIAL_JTAG_TAG, "The driver hasn't been initialized");

    const uint8_t *buff = (const uint8_t *)src;
    // Blocking method, Sending data to ringbuffer, and handle the data in ISR.
    BaseType_t result = xRingbufferSend(p_usb_serial_jtag_obj->tx_ring_buf, (void*) (buff), size, ticks_to_wait);
    // Now trigger the ISR to read data from the ring buffer.
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    return (result == pdFALSE) ? 0 : size;
}

esp_err_t cores3_usb_driver_uninstall(void)
{
    if(p_usb_serial_jtag_obj == NULL) {
        ESP_LOGI(USB_SERIAL_JTAG_TAG, "ALREADY NULL");
        return ESP_OK;
    }

    /* Not disable the module clock and usb_pad_enable here since the USJ stdout might still depends on it. */
    //Disable tx/rx interrupt.
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY | USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    esp_intr_free(p_usb_serial_jtag_obj->intr_handle);

    if(p_usb_serial_jtag_obj->rx_ring_buf) {
        vRingbufferDelete(p_usb_serial_jtag_obj->rx_ring_buf);
        p_usb_serial_jtag_obj->rx_ring_buf = NULL;
    }
    if(p_usb_serial_jtag_obj->tx_ring_buf) {
        vRingbufferDelete(p_usb_serial_jtag_obj->tx_ring_buf);
        p_usb_serial_jtag_obj->tx_ring_buf = NULL;
    }
    heap_caps_free(p_usb_serial_jtag_obj);
    p_usb_serial_jtag_obj = NULL;
    return ESP_OK;
}
