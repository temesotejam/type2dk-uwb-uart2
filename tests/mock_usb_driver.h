#ifndef MOCK_USB_DRIVER_H
#define MOCK_USB_DRIVER_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

typedef int esp_err_t;
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef unsigned TickType_t;
typedef void *intr_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_NO_MEM -4
#define pdTRUE 1
#define pdFALSE 0
#define RINGBUF_TYPE_BYTEBUF 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define ETS_USB_SERIAL_JTAG_INTR_SOURCE 3
#define USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY 1u
#define USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT 2u
#define ESP_RETURN_ON_FALSE(c,r,tag,...) do { (void)(tag);if(!(c))return (r); } while(0)
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define portYIELD_FROM_ISR() ((void)0)
#define heap_caps_calloc(n,s,c) calloc(n,s)
#define heap_caps_free(p) free(p)

typedef struct {uint32_t tx_buffer_size,rx_buffer_size;} usb_serial_jtag_driver_config_t;
esp_err_t cores3_usb_driver_uninstall(void);
int cores3_usb_write_bytes(const void *,size_t,TickType_t);

typedef struct {uint8_t *data;size_t capacity,head,tail,count,taken;} MockRing;
typedef MockRing *RingbufHandle_t;
static RingbufHandle_t xRingbufferCreate(size_t capacity,int kind) {
    (void)kind;MockRing *r=calloc(1,sizeof(*r));assert(r);
    r->capacity=capacity;r->data=malloc(capacity);assert(r->data);return r;
}
static void vRingbufferDelete(RingbufHandle_t r){free(r->data);free(r);}
static BaseType_t xRingbufferSend(RingbufHandle_t r,void *src,size_t n,TickType_t wait){
    (void)wait;if(n>r->capacity-r->count)return pdFALSE;
    for(size_t i=0;i<n;i++){r->data[r->tail]=((uint8_t*)src)[i];r->tail=(r->tail+1)%r->capacity;}
    r->count+=n;return pdTRUE;
}
static BaseType_t xRingbufferSendFromISR(RingbufHandle_t r,void *src,size_t n,BaseType_t *wake){
    (void)wake;return xRingbufferSend(r,src,n,0);
}
static void *xRingbufferReceiveUpTo(RingbufHandle_t r,size_t *n,TickType_t wait,size_t max){
    (void)wait;assert(!r->taken);*n=r->count;
    if(*n>max)*n=max;
    if(*n>r->capacity-r->head)*n=r->capacity-r->head;
    r->taken=*n;return *n?r->data+r->head:NULL;
}
static void *xRingbufferReceiveUpToFromISR(RingbufHandle_t r,size_t *n,size_t max){return xRingbufferReceiveUpTo(r,n,0,max);}
static void vRingbufferReturnItem(RingbufHandle_t r,void *item){
    (void)item;r->head=(r->head+r->taken)%r->capacity;r->count-=r->taken;r->taken=0;
}
static void vRingbufferReturnItemFromISR(RingbufHandle_t r,void *item,BaseType_t *wake){(void)wake;vRingbufferReturnItem(r,item);}

/* Model event-triggered TX EMPTY, a single 64-byte USB packet, and explicit
 * WR_DONE. Merely enabling its mask cannot invent a missing status event. */
static uint32_t usbMask,usbRaw;
static uint8_t endpoint[64],hostData[65536];
static size_t endpointSize,hostSize,lastPacket,packetCount;
static unsigned fifoLimit=UINT_MAX;
static bool packetPending;
static void (*irqHandler)(void *);
static void *irqArg;
static const char *enqueueBeforeMask;
static int USB_SERIAL_JTAG;
static uint32_t usb_serial_jtag_ll_get_intsts_mask(void){return usbMask&usbRaw;}
static void usb_serial_jtag_ll_clr_intsts_mask(uint32_t m){usbRaw&=~m;}
static void usb_serial_jtag_ll_ena_intr_mask(uint32_t m){usbMask|=m;}
static void usb_serial_jtag_ll_disable_intr_mask(uint32_t m){
    if((m&1u)&&enqueueBeforeMask){
        const char *s=enqueueBeforeMask;enqueueBeforeMask=NULL;
        assert(cores3_usb_write_bytes(s,strlen(s),0)==(int)strlen(s));
    }
    usbMask&=~m;
}
static int usb_serial_jtag_ll_txfifo_writable(void){return !packetPending&&endpointSize<64;}
static uint32_t usb_serial_jtag_ll_write_txfifo(const uint8_t *src,uint32_t n){
    if(packetPending)return 0;
    if(n>fifoLimit)n=fifoLimit;
    if(n>64-endpointSize)n=(uint32_t)(64-endpointSize);
    memcpy(endpoint+endpointSize,src,n);endpointSize+=n;return n;
}
static void usb_serial_jtag_ll_txfifo_flush(void){packetPending=true;}
static uint32_t usb_serial_jtag_ll_read_rxfifo(uint8_t *dst,uint32_t n){(void)dst;(void)n;return 0;}
static void usb_serial_jtag_ll_enable_bus_clock(bool on){(void)on;}
static void usb_phy_ll_int_jtag_enable(void *hw){(void)hw;}
static esp_err_t esp_intr_alloc(int source,int flags,void (*fn)(void*),void *arg,intr_handle_t *handle){
    (void)source;(void)flags;irqHandler=fn;irqArg=arg;*handle=(void*)1;return ESP_OK;
}
static void esp_intr_free(intr_handle_t handle){(void)handle;irqHandler=NULL;}
static void hostDrain(void){
    for(unsigned i=0;i<10000;i++){
        bool progress=false;
        if(packetPending){
            assert(hostSize+endpointSize<=sizeof(hostData));
            memcpy(hostData+hostSize,endpoint,endpointSize);hostSize+=endpointSize;
            lastPacket=endpointSize;packetCount++;endpointSize=0;packetPending=false;
            usbRaw|=USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY;progress=true;
        }
        if(irqHandler&&(usbMask&usbRaw)){irqHandler(irqArg);progress=true;}
        if(!progress)return;
    }
    assert(!"USB interrupt loop did not become idle");
}
#endif
