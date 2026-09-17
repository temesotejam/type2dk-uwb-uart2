#ifndef LOG_RECORD_H
#define LOG_RECORD_H

#include <stdio.h>
#include <string.h>
#include "event_protocol.h"

/* CRC covers every ASCII byte before ",log_crc=" (including log_seq).
 * The result is one CRLF-terminated record; zero means it did not fit.
 * The caller advances sequence even if this record is subsequently dropped. */
static inline size_t log_record_finish(char *text,size_t capacity,uint32_t sequence) {
    size_t n=0;
    while(n<capacity && text[n])++n;
    if(n==capacity)return 0;
    while(n && (text[n-1]=='\r'||text[n-1]=='\n'))--n;
    for(size_t i=0;i<n;i++)if(text[i]=='\r'||text[i]=='\n')return 0;
    int added=snprintf(text+n,capacity-n,",log_seq=%lu",(unsigned long)sequence);
    if(added<0 || (size_t)added>=capacity-n)return 0;
    n+=(size_t)added;
    uint16_t crc=ev_crc((const uint8_t *)text,n);
    added=snprintf(text+n,capacity-n,",log_crc=%04x\r\n",(unsigned)crc);
    if(added<0 || (size_t)added>=capacity-n)return 0;
    return n+(size_t)added;
}
#endif
