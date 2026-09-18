#include <stdio.h>
#define CORES3_USB_HOST_TEST 1
#include "../central/cores3/usb_log_driver.c"

static void sendAndCheck(const char *s,size_t n){
    size_t before=hostSize;
    assert(cores3_usb_write_bytes(s,n,25)==(int)n);
    hostDrain();assert(hostSize==before+n);
    assert(cores3_usb_tx_bytes()==hostSize);
    assert(memcmp(hostData+before,s,n)==0);
    assert(!(usbMask&USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY));
}

int main(void){
    usb_serial_jtag_driver_config_t config={4096,256};
    assert(usbRaw==0); /* Bootloader left no pending EMPTY interrupt. */
    assert(cores3_usb_driver_install(&config)==ESP_OK);
    sendAndCheck("first\r\n",7);
    /* Initial idle, many independent bursts and ring-buffer wrap. */
    for(unsigned i=0;i<150;i++)sendAndCheck("next event after idle\r\n",23);
    char exact[128];memset(exact,'x',sizeof(exact));
    sendAndCheck(exact,sizeof(exact));
    assert(lastPacket==0); /* ZLP terminates an exact multiple of 64. */

    /* Hardware accepts fewer bytes than requested, including a second partial
     * write from the stash (source/destination overlap). */
    fifoLimit=7;sendAndCheck("abcdefghijklmnopqrstuvwxyz0123456789",36);fifoLimit=UINT_MAX;

    /* Simulate a different CPU enqueueing immediately before ISR masks TX. */
    enqueueBeforeMask="race survived\r\n";
    size_t before=hostSize;
    assert(cores3_usb_write_bytes("trigger\r\n",9,0)==9);hostDrain();
    assert(hostSize==before+24);
    assert(memcmp(hostData+before,"trigger\r\nrace survived\r\n",24)==0);

    /* Closed host: whole-record refusal, then recovery when the host reads. */
    char large[3000];memset(large,'Q',sizeof(large));
    assert(cores3_usb_write_bytes(large,sizeof(large),0)==sizeof(large));
    assert(cores3_usb_write_bytes(large,sizeof(large),0)==0);
    before=hostSize;hostDrain();assert(hostSize==before+sizeof(large));
    sendAndCheck("after closed host\r\n",19);
    assert(cores3_usb_driver_uninstall()==ESP_OK);
    puts("USB driver: cold start, idle restart, 64-byte ZLP, partial FIFO, race and host stall passed");
}
