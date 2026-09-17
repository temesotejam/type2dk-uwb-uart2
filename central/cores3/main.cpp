#include <M5Unified.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_intr_alloc.h>
#include <esp_timer.h>
#include <hal/uart_ll.h>
#include <soc/uart_periph.h>
#include "event_protocol.h"
#include "log_record.h"

namespace {
constexpr unsigned ringSize=1024;
constexpr uint32_t errorMask=UART_INTR_RXFIFO_OVF|UART_INTR_FRAM_ERR|UART_INTR_PARITY_ERR|UART_INTR_BRK_DET;
struct Byte {uint64_t us;uint32_t epoch;uint8_t value;};
struct Port {
    uart_port_t number;uart_dev_t *hw;int pin;uint8_t node;
    portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
    Byte ring[ringSize];unsigned read=0,write=0;
    uint32_t epoch=0,bytes=0,ringDrops=0,fifoErrors=0,frameErrors=0,parityErrors=0,breaks=0,maxBatch=0;
    intr_handle_t interrupt=nullptr;esp_err_t init=ESP_FAIL;
    event_parser_t parser={};event_tracker_t tracker={};uint32_t readerEpoch=0;
    event_t latest={},measurement={};bool haveMeasurement=false;
    uint64_t lastRx=0,measurementRx=0,maxSpan=0;
    uint32_t rangeCount=0,failedCount=0,testCount=0;
};
Port ports[2];
M5Canvas canvas(&M5.Display);
struct LogLine {char text[960];};
QueueHandle_t logQueue=nullptr;
portMUX_TYPE logMux=portMUX_INITIALIZER_UNLOCKED;
uint32_t logDropped=0;
uint32_t logQueueDropped=0,logWriteDropped=0,logFormatDropped=0,logSequence=0;
esp_err_t usbInit=ESP_FAIL;
uint32_t lastDraw=0,lastStat=0;

void countLogDrop(uint32_t &reason){portENTER_CRITICAL(&logMux);logDropped++;reason++;portEXIT_CRITICAL(&logMux);}
uint32_t getLogDrops(){portENTER_CRITICAL(&logMux);uint32_t n=logDropped;portEXIT_CRITICAL(&logMux);return n;}
void enqueue(LogLine &line){
    if(!log_record_finish(line.text,sizeof(line.text),++logSequence)){
        countLogDrop(logFormatDropped);return;
    }
    if(!logQueue||xQueueSend(logQueue,&line,0)!=pdTRUE)countLogDrop(logQueueDropped);
}
void logger(void *){
    LogLine line;
    for(;;)if(xQueueReceive(logQueue,&line,portMAX_DELAY)==pdTRUE){
        size_t n=strlen(line.text);
        // IDF 4.4.7 queues the whole record or returns 0 on timeout. Its ISR
        // retains any bytes that did not fit in the hardware FIFO. No Arduino
        // HWCDC calls (including if(Serial)/flush) may share this peripheral.
        // A closed/unplugged host can stall only this task, for at most 25 ms.
        if(usb_serial_jtag_write_bytes(line.text,n,pdMS_TO_TICKS(25))!=(int)n)
            countLogDrop(logWriteDropped);
    }
}

/* No display, USB, heap allocation or CRC calculation in this ISR.
 * Timestamp means the instant the ISR observes a received byte, not its start bit. */
void IRAM_ATTR receiveIsr(void *arg){
    auto *p=static_cast<Port*>(arg);
    uint32_t st=uart_ll_get_intsts_mask(p->hw);
    portENTER_CRITICAL_ISR(&p->mux);
    if(st&errorMask){
        if(st&UART_INTR_RXFIFO_OVF)p->fifoErrors++;
        if(st&UART_INTR_FRAM_ERR)p->frameErrors++;
        if(st&UART_INTR_PARITY_ERR)p->parityErrors++;
        if(st&UART_INTR_BRK_DET)p->breaks++;
        p->epoch++;uart_ll_rxfifo_rst(p->hw);
    }else{
        unsigned n=uart_ll_get_rxfifo_len(p->hw);
        if(n>p->maxBatch)p->maxBatch=n;
        for(unsigned i=0;i<n;i++){
            uint8_t b;uart_ll_read_rxfifo(p->hw,&b,1);
            uint64_t t=(uint64_t)esp_timer_get_time();p->bytes++;
            unsigned next=(p->write+1)%ringSize;
            if(next==p->read){p->ringDrops++;p->epoch++;continue;}
            p->ring[p->write]={t,p->epoch,b};p->write=next;
        }
    }
    uart_ll_clr_intsts_mask(p->hw,st);
    portEXIT_CRITICAL_ISR(&p->mux);
}
bool pop(Port &p,Byte &b){
    portENTER_CRITICAL(&p.mux);
    bool ok=p.read!=p.write;
    if(ok){b=p.ring[p.read];p.read=(p.read+1)%ringSize;}
    portEXIT_CRITICAL(&p.mux);return ok;
}

esp_err_t beginPort(Port &p){
    uart_config_t cfg={};cfg.baud_rate=EVENT_BAUD;cfg.data_bits=UART_DATA_8_BITS;
    cfg.parity=UART_PARITY_DISABLE;cfg.stop_bits=UART_STOP_BITS_1;
    cfg.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;cfg.source_clk=UART_SCLK_APB;
    esp_err_t e=uart_param_config(p.number,&cfg);if(e!=ESP_OK)return e;
    uart_ll_disable_intr_mask(p.hw,UART_LL_INTR_MASK);
    e=uart_set_pin(p.number,UART_PIN_NO_CHANGE,p.pin,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(e!=ESP_OK)return e;
    e=gpio_set_pull_mode((gpio_num_t)p.pin,GPIO_FLOATING);if(e!=ESP_OK)return e;
    uart_ll_rxfifo_rst(p.hw);uart_ll_clr_intsts_mask(p.hw,UART_LL_INTR_MASK);
    // Own the interrupt directly; uart_driver_install and uart_isr_register are
    // deliberately not mixed with this timestamping receiver (IDF 4.4.7).
    e=esp_intr_alloc(uart_periph_signal[p.number].irq,ESP_INTR_FLAG_IRAM|ESP_INTR_FLAG_LEVEL1,
                     receiveIsr,&p,&p.interrupt);
    if(e!=ESP_OK)return e;
    uart_intr_config_t intr={};intr.intr_enable_mask=UART_INTR_RXFIFO_FULL|UART_INTR_RXFIFO_TOUT|errorMask;
    intr.rxfifo_full_thresh=1;intr.rx_timeout_thresh=2;
    e=uart_intr_config(p.number,&intr);
    if(e!=ESP_OK){esp_intr_free(p.interrupt);p.interrupt=nullptr;}
    return e;
}

void accept(Port &p,const event_t &e,uint64_t first,uint64_t last){
    bool reboot=p.tracker.seen && p.tracker.boot!=e.boot;
    if(!event_track(&p.tracker,&e,p.node))return;
    if(reboot)p.haveMeasurement=false;
    p.latest=e;p.lastRx=last;if(last-first>p.maxSpan)p.maxSpan=last-first;
    if(e.type==EVENT_RANGE){
        p.rangeCount++;if(!(e.flags&EVENT_DISTANCE_VALID))p.failedCount++;
        p.measurement=e;p.measurementRx=last;p.haveMeasurement=true;
    }else if(e.type==EVENT_TEST){p.testCount++;p.haveMeasurement=false;}
    LogLine line={};
    // cm -> mm is only a unit conversion. Sensor quantization remains 10 mm.
    int mm=(e.flags&EVENT_DISTANCE_VALID)?(int)e.range_cm*10:-1;
    snprintf(line.text,sizeof(line.text),
        "UWB_EVENT,port=%c,node=%u,type=%s,boot=%08lx,seq=%lu,anchor=%u,sid=%08lx,uci_seq=%lu,range_mm=%d,raw_cm=%u,status=0x%02x,nlos_raw=%u,profile=%u,callback_ms=%lu,tx_start_ms=%lu,queue_ms=%lu,rx_first_us=%llu,rx_last_us=%llu,rx_span_us=%llu,tx_drop=%lu,queue_depth=%u,state=%u,reason=0x%02x,fault=%u\n",
        p.node==1?'A':'B',e.node,e.type==EVENT_RANGE?"RANGE":e.type==EVENT_TEST?"TEST":"HEALTH",
        (unsigned long)e.boot,(unsigned long)e.sequence,e.anchor,(unsigned long)e.session_id,
        (unsigned long)e.uci_sequence,mm,e.range_cm,e.status,e.nlos,(e.flags&EVENT_PROFILE_CONFIRMED)?1:0,
        (unsigned long)e.callback_ms,(unsigned long)e.tx_start_ms,(unsigned long)(e.tx_start_ms-e.callback_ms),
        (unsigned long long)first,(unsigned long long)last,(unsigned long long)(last-first),
        (unsigned long)e.dropped,e.queue_depth,e.session_state,e.reason,e.fault);
    enqueue(line);
}
void receive(){
    for(unsigned step=0;step<256;step++){
        bool any=false;
        for(auto &p:ports){
            Byte b;if(p.init!=ESP_OK||!pop(p,b))continue;any=true;
            if(b.epoch!=p.readerEpoch){p.parser.discarded+=p.parser.used;p.parser.used=0;p.readerEpoch=b.epoch;}
            event_t e;uint64_t first,last;
            if(event_feed(&p.parser,b.value,b.us,&e,&first,&last))accept(p,e,first,last);
        }
        if(!any)break;
    }
}
const char *state(const Port &p,uint64_t now){
    if(p.init!=ESP_OK)return "INIT ERROR";
    if(!p.tracker.seen)return "WAITING";
    if(now-p.lastRx>3000000u)return "NO DATA";
    if(p.latest.type==EVENT_TEST)return "UART TEST";
    if(!(p.latest.flags&EVENT_PROFILE_CONFIRMED))return "CONFIG REQUIRED";
    return "RECEIVING";
}
void statistics(){
    for(auto &p:ports){
        uint32_t bytes,ring,fifo,framing,parity,brk,batch;
        portENTER_CRITICAL(&p.mux);
        bytes=p.bytes;ring=p.ringDrops;fifo=p.fifoErrors;framing=p.frameErrors;
        parity=p.parityErrors;brk=p.breaks;batch=p.maxBatch;
        portEXIT_CRITICAL(&p.mux);
        uint32_t logTotal,logQueueLoss,logWriteLoss,logFormatLoss;
        portENTER_CRITICAL(&logMux);
        logTotal=logDropped;logQueueLoss=logQueueDropped;logWriteLoss=logWriteDropped;logFormatLoss=logFormatDropped;
        portEXIT_CRITICAL(&logMux);
        LogLine line={};
        snprintf(line.text,sizeof(line.text),
          "DUAL_STAT,fw=%s,port=%c,rx_us=%llu,state=%s,bytes=%lu,ok=%lu,bad=%lu,missing=%lu,duplicate=%lu,backwards=%lu,restarts=%lu,wrong_node=%lu,range=%lu,range_fail=%lu,test=%lu,ring_drop=%lu,fifo_error=%lu,frame_error=%lu,parity=%lu,breaks=%lu,max_isr_batch=%lu,max_rx_span_us=%llu,log_drop=%lu,log_queue_drop=%lu,log_write_drop=%lu,log_format_drop=%lu,usb_init=%s,init=%s\n",
          FW_VERSION,p.node==1?'A':'B',(unsigned long long)esp_timer_get_time(),state(p,(uint64_t)esp_timer_get_time()),
          (unsigned long)bytes,(unsigned long)p.tracker.accepted,(unsigned long)p.parser.crc_or_format_errors,
          (unsigned long)p.tracker.missing,(unsigned long)p.tracker.duplicates,(unsigned long)p.tracker.backwards,
          (unsigned long)p.tracker.restarts,(unsigned long)p.tracker.wrong_node,(unsigned long)p.rangeCount,
          (unsigned long)p.failedCount,(unsigned long)p.testCount,(unsigned long)ring,(unsigned long)fifo,
          (unsigned long)framing,(unsigned long)parity,(unsigned long)brk,(unsigned long)batch,
          (unsigned long long)p.maxSpan,(unsigned long)logTotal,(unsigned long)logQueueLoss,
          (unsigned long)logWriteLoss,(unsigned long)logFormatLoss,esp_err_to_name(usbInit),esp_err_to_name(p.init));
        enqueue(line);
    }
}
void draw(){
    uint64_t now=(uint64_t)esp_timer_get_time();
    canvas.fillScreen(0x08121f);canvas.setTextColor(0xe7f1ff);canvas.setTextSize(1);
    canvas.drawString("2DK A+B / " FW_VERSION,10,8);
    canvas.drawString("PORT A: yellow=A  white=B  38400",10,25);
    for(unsigned i=0;i<2;i++){
        auto &p=ports[i];int y=47+(int)i*82;
        canvas.setTextColor(0x45d6d0);canvas.setTextSize(2);canvas.setCursor(10,y);
        canvas.printf("%c  %s",i?'B':'A',state(p,now));
        canvas.setTextSize(1);canvas.setTextColor(0xe7f1ff);canvas.setCursor(10,y+23);
        const auto &m=p.measurement;
        bool fresh=p.haveMeasurement && now-p.measurementRx<3000000u &&
                   (m.flags&(EVENT_DISTANCE_VALID|EVENT_PROFILE_CONFIRMED))==3 && m.fault==0;
        if(fresh)canvas.printf("Anchor %u: %u mm   nLos(raw) %u",m.anchor,(unsigned)m.range_cm*10,m.nlos);
        else canvas.print("Distance: -- (test / failed / stale)");
        canvas.setCursor(10,y+39);canvas.printf("RX %lu  MISS %lu  BAD %lu",(unsigned long)p.tracker.accepted,
              (unsigned long)p.tracker.missing,(unsigned long)p.parser.crc_or_format_errors);
        canvas.setCursor(10,y+53);canvas.printf("TX drop %lu  wrong port %lu",(unsigned long)p.latest.dropped,
              (unsigned long)p.tracker.wrong_node);
    }
    canvas.setTextColor(0xffc66d);canvas.setCursor(10,220);
    if(usbInit!=ESP_OK)canvas.printf("USB init: %s",esp_err_to_name(usbInit));
    else canvas.printf("Timestamp: UART ISR / USB drop %lu",(unsigned long)getLogDrops());
    canvas.pushSprite(0,0);
}
}
void setup(){
    // Leave Arduino HWCDC uninitialized: the IDF driver exclusively owns USB.
    auto cfg=M5.config();cfg.serial_baudrate=0;cfg.internal_imu=false;cfg.internal_rtc=false;
    cfg.internal_mic=false;cfg.internal_spk=false;cfg.external_imu=false;cfg.external_rtc=false;
    cfg.external_display_value=0;cfg.output_power=false;cfg.fallback_board=m5::board_t::board_M5StackCoreS3;
    M5.begin(cfg);M5.Display.setRotation(1);M5.Display.setBrightness(160);
    canvas.setColorDepth(16);
    if(!canvas.createSprite(320,240)){M5.Display.println("Display allocation failed");while(true)delay(1000);}
    usb_serial_jtag_driver_config_t usbCfg={};usbCfg.tx_buffer_size=4096;usbCfg.rx_buffer_size=256;
    usbInit=usb_serial_jtag_driver_install(&usbCfg);
    if(usbInit==ESP_OK)logQueue=xQueueCreate(48,sizeof(LogLine));
    if(logQueue && xTaskCreatePinnedToCore(logger,"UsbLogger",4096,nullptr,1,nullptr,0)!=pdPASS){
        vQueueDelete(logQueue);logQueue=nullptr;
    }
    ports[0].number=UART_NUM_1;ports[0].hw=&UART1;ports[0].pin=2;ports[0].node=1;
    ports[1].number=UART_NUM_2;ports[1].hw=&UART2;ports[1].pin=1;ports[1].node=2;
    bool safe=M5.Ex_I2C.getSDA()==2 && M5.Ex_I2C.getSCL()==1 && M5.Ex_I2C.getPort()!=M5.In_I2C.getPort();
    if(safe){
        M5.Ex_I2C.release();
        gpio_config_t pins={};pins.pin_bit_mask=(1ULL<<1)|(1ULL<<2);pins.mode=GPIO_MODE_INPUT;
        pins.pull_up_en=GPIO_PULLUP_DISABLE;pins.pull_down_en=GPIO_PULLDOWN_DISABLE;pins.intr_type=GPIO_INTR_DISABLE;
        esp_err_t pinResult=gpio_config(&pins);
        for(auto &p:ports)p.init=pinResult==ESP_OK?beginPort(p):pinResult;
    }else for(auto &p:ports)p.init=ESP_ERR_INVALID_STATE;
    statistics();draw();
}
void loop(){
    receive();M5.update();uint32_t t=millis();
    if(t-lastStat>=1000u){lastStat=t;statistics();}
    if(t-lastDraw>=200u){lastDraw=t;draw();}
    delay(1);
}
