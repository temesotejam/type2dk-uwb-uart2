#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "log_record.h"

int main(void) {
    char text[960];
    const char *expected="UWB_EVENT,port=A,seq=42,log_seq=1,log_crc=a9a8\r\n";
    strcpy(text,"UWB_EVENT,port=A,seq=42\n");
    assert(log_record_finish(text,sizeof(text),1)==strlen(expected));
    assert(strcmp(text,expected)==0); /* Golden CRC calculated independently. */
    strcpy(text,"DUAL_STAT,port=B,ok=120\r\n");
    assert(log_record_finish(text,sizeof(text),UINT32_MAX)>0);
    assert(strcmp(text,"DUAL_STAT,port=B,ok=120,log_seq=4294967295,log_crc=32f8\r\n")==0);

    /* A short buffer must fail without overrunning or emitting a valid-looking
     * checksum on a truncated record. */
    for(size_t capacity=sizeof("UWB_EVENT,port=A,seq=42\n");capacity<=strlen(expected)+1;capacity++) {
        memset(text,0xa5,sizeof(text));strcpy(text,"UWB_EVENT,port=A,seq=42\n");
        size_t result=log_record_finish(text,capacity,1);
        assert((result!=0)==(capacity==strlen(expected)+1));
        assert((unsigned char)text[capacity]==0xa5);
    }
    memset(text,'X',sizeof(text));
    assert(log_record_finish(text,sizeof(text),1)==0);
    strcpy(text,"UWB_EVENT,port=A\nDUAL_STAT,port=B");
    assert(log_record_finish(text,sizeof(text),1)==0);
    puts("USB log framing tests passed");
}
