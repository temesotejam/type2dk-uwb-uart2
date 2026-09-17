/* GPIO13 TX; retain proven bit timing. IRQs enabled between bytes.
 * Callback only enqueues; this low-priority task owns the GPIO transmitter. */
#include "event_uart.h"
#include "event_queue.h"
#include "tag_profile.h"
#include "QN9090.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_debug_console.h"
static event_queue_t queue;
static TaskHandle_t worker;
static uint32_t bit_ticks,boot_id,frames,max_irq_cycles,max_frame_ms,max_wait_ms;
static volatile uint32_t timer_fault;
static uint32_t now_ms(void){return xTaskGetTickCount()*portTICK_PERIOD_MS;}
static inline int wait_at(uint32_t start,uint32_t target) {
    /* A stopped debug counter must not hang with interrupts disabled. */
    for(uint32_t n=0;n<bit_ticks*4u;n++) if((uint32_t)(DWT->CYCCNT-start)>=target) return 1;
    return 0;
}
static int tx_byte(uint8_t byte) {
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    const uint32_t start=DWT->CYCCNT;
    GPIO->B[0][13]=0;
    for(unsigned i=0;i<8;i++) {
        if(!wait_at(start,(i+1u)*bit_ticks)) goto fail;
        GPIO->B[0][13]=(uint8_t)((byte>>i)&1u);
    }
    if(!wait_at(start,9u*bit_ticks)) goto fail;
    GPIO->B[0][13]=1;
    const uint32_t elapsed=DWT->CYCCNT-start;
    __set_PRIMASK(mask);
    if(elapsed>max_irq_cycles) max_irq_cycles=elapsed;
    return wait_at(start,10u*bit_ticks);
fail:
    GPIO->B[0][13]=1;__set_PRIMASK(mask);return 0;
}
static int tx_init(void) {
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    if(DWT->CTRL&DWT_CTRL_NOCYCCNT_Msk) return 0;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;__DSB();__ISB();
    uint32_t before=DWT->CYCCNT;
    for(unsigned i=0;i<32;i++) __NOP();
    if(DWT->CYCCNT==before || SystemCoreClock<12000000u) return 0;
    bit_ticks=(SystemCoreClock+EVENT_BAUD/2u)/EVENT_BAUD;
    SYSCON->AHBCLKCTRLSET[0]=SYSCON_AHBCLKCTRL0_GPIO_MASK|SYSCON_AHBCLKCTRL0_IOCON_MASK;
    GPIO->DIRCLR[0]=(1u<<12)|(1u<<13);GPIO->SET[0]=1u<<13;
    const uint32_t config=IOCON_PIO_FUNC(0)|IOCON_PIO_MODE(0)|IOCON_PIO_DIGIMODE(1)|IOCON_PIO_FILTEROFF(1);
    IOCON->PIO[0][12]=config;IOCON->PIO[0][13]=config;GPIO->DIRSET[0]=1u<<13;
    return 1;
}

static void worker_task(void *unused) {
    (void)unused;
    for(;;) {
        event_t e;
        taskENTER_CRITICAL();
        bool available=event_queue_pop(&queue,&e);
        taskEXIT_CRITICAL();
        if(!available){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
        uint32_t wait=now_ms()-e.callback_ms;
        if(wait>max_wait_ms)max_wait_ms=wait;
        if(wait>250u){taskENTER_CRITICAL();queue.dropped++;taskEXIT_CRITICAL();continue;}
        e.tx_start_ms=now_ms();if(timer_fault)e.fault=(uint8_t)timer_fault;
        uint8_t b[EVENT_SIZE];event_encode(b,&e);
        bool complete=true;
        for(unsigned i=0;i<EVENT_SIZE;i++) {
            if(now_ms()-e.tx_start_ms>250u){complete=false;break;}
            if(!tx_byte(b[i])){timer_fault=1;complete=false;break;}
            /* Five scheduling gaps per 48-byte frame, not one after the end. */
            if((i&7u)==7u && i+1<EVENT_SIZE)vTaskDelay(1);
        }
        uint32_t duration=now_ms()-e.tx_start_ms;
        if(duration>max_frame_ms)max_frame_ms=duration;
        if(complete)frames++;
        else {taskENTER_CRITICAL();queue.dropped++;taskEXIT_CRITICAL();}
        if(timer_fault){worker=NULL;vTaskDelete(NULL);}
    }
}
bool event_uart_start(uint32_t boot) {
    boot_id=boot;
    if(!tx_init()){timer_fault=1;return false;}
    if(xTaskCreate(worker_task,"EventTx",768,NULL,1,&worker)!=pdPASS){timer_fault=2;return false;}
    return true;
}
bool event_uart_submit(event_t *e) {
    e->node=TAG_NODE;e->boot=boot_id;
    if(TAG_PROFILE_CONFIRMED)e->flags|=EVENT_PROFILE_CONFIRMED;
    taskENTER_CRITICAL();
    bool ok=event_queue_push(&queue,e);
    taskEXIT_CRITICAL();
    if(ok && worker)xTaskNotifyGive(worker);
    return ok;
}
void event_uart_log(void) {
    taskENTER_CRITICAL();
    uint32_t dropped=queue.dropped;unsigned depth=queue.used,high=queue.high_water;
    taskEXIT_CRITICAL();
    PRINTF("EVENT_TX,node=%u,frames=%lu,dropped=%lu,depth=%u,high=%u,max_wait_ms=%lu,max_frame_ms=%lu,irq_us=%lu,fault=%lu\r\n",
        TAG_NODE,(unsigned long)frames,(unsigned long)dropped,depth,high,(unsigned long)max_wait_ms,
        (unsigned long)max_frame_ms,(unsigned long)(max_irq_cycles/(SystemCoreClock/1000000u)),(unsigned long)timer_fault);
}
