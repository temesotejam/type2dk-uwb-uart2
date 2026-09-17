#ifndef EVENT_PROTOCOL_H
#define EVENT_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EVENT_SIZE 48u
#define EVENT_BAUD 38400u
#define EVENT_VERSION 1u
#define EVENT_RANGE 1u
#define EVENT_HEALTH 2u
#define EVENT_TEST 3u
#define EVENT_DISTANCE_VALID 1u
#define EVENT_PROFILE_CONFIRMED 2u
#define EVENT_NO_DISTANCE 65535u

typedef struct {
    uint8_t type,node,flags;
    uint32_t boot,sequence,callback_ms,tx_start_ms,uci_sequence,session_id;
    uint16_t anchor,range_cm;
    uint8_t status,nlos,session_state,reason;
    uint32_t dropped;
    uint8_t queue_depth,fault;
} event_t;
static inline uint16_t ev_u16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static inline uint32_t ev_u32(const uint8_t *p) {return ev_u16(p)|((uint32_t)ev_u16(p+2)<<16);}
static inline void ev_p16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static inline void ev_p32(uint8_t *p,uint32_t v) {ev_p16(p,(uint16_t)v);ev_p16(p+2,(uint16_t)(v>>16));}
static inline uint16_t ev_crc(const uint8_t *p,size_t n) {
    uint16_t c=0xffff;
    while(n--) {c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++)c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));}
    return c;
}
static inline void event_encode(uint8_t *b,const event_t *e) {
    b[0]=0xd2;b[1]=0x55;b[2]=0x45;b[3]=0x33;b[4]=EVENT_VERSION;
    b[5]=e->type;b[6]=e->node;b[7]=e->flags;
    ev_p32(b+8,e->boot);ev_p32(b+12,e->sequence);ev_p32(b+16,e->callback_ms);
    ev_p32(b+20,e->tx_start_ms);ev_p32(b+24,e->uci_sequence);ev_p32(b+28,e->session_id);
    ev_p16(b+32,e->anchor);ev_p16(b+34,e->range_cm);
    b[36]=e->status;b[37]=e->nlos;b[38]=e->session_state;b[39]=e->reason;
    ev_p32(b+40,e->dropped);b[44]=e->queue_depth;b[45]=e->fault;
    ev_p16(b+46,ev_crc(b,46));
}
static inline bool event_decode(const uint8_t *b,event_t *e) {
    if(b[0]!=0xd2||b[1]!=0x55||b[2]!=0x45||b[3]!=0x33||b[4]!=EVENT_VERSION||
       b[5]<EVENT_RANGE||b[5]>EVENT_TEST||(b[6]!=1&&b[6]!=2)||(b[7]&~3u)||
       ev_u16(b+46)!=ev_crc(b,46))return false;
    e->type=b[5];e->node=b[6];e->flags=b[7];
    e->boot=ev_u32(b+8);e->sequence=ev_u32(b+12);e->callback_ms=ev_u32(b+16);
    e->tx_start_ms=ev_u32(b+20);e->uci_sequence=ev_u32(b+24);e->session_id=ev_u32(b+28);
    e->anchor=ev_u16(b+32);e->range_cm=ev_u16(b+34);
    e->status=b[36];e->nlos=b[37];e->session_state=b[38];e->reason=b[39];
    e->dropped=ev_u32(b+40);e->queue_depth=b[44];e->fault=b[45];
    /* A successful command with no distance is never a valid measurement. */
    if((e->flags&EVENT_DISTANCE_VALID)&&
       (e->type!=EVENT_RANGE||e->status!=0||e->range_cm==EVENT_NO_DISTANCE))return false;
    return true;
}

/* Sliding parser: retain byte timestamps through corrupt-frame resynchronization. */
typedef struct {
    uint8_t bytes[EVENT_SIZE];uint64_t times[EVENT_SIZE];unsigned used;
    uint32_t crc_or_format_errors,discarded,timeouts;
} event_parser_t;
static inline bool event_feed(event_parser_t *p,uint8_t b,uint64_t us,
                              event_t *e,uint64_t *first_us,uint64_t *last_us) {
    if(p->used && us-p->times[p->used-1]>100000u) {
        p->discarded+=p->used;p->used=0;p->timeouts++;
    }
    p->bytes[p->used]=b;p->times[p->used++]=us;
    while(p->used && (p->bytes[0]!=0xd2 || (p->used>1&&p->bytes[1]!=0x55) ||
          (p->used>2&&p->bytes[2]!=0x45)||(p->used>3&&p->bytes[3]!=0x33))) {
        --p->used;++p->discarded;
        memmove(p->bytes,p->bytes+1,p->used);
        memmove(p->times,p->times+1,p->used*sizeof(p->times[0]));
    }
    if(p->used!=EVENT_SIZE)return false;
    if(event_decode(p->bytes,e)) {
        *first_us=p->times[0];*last_us=p->times[EVENT_SIZE-1];p->used=0;return true;
    }
    ++p->crc_or_format_errors;++p->discarded;--p->used;
    memmove(p->bytes,p->bytes+1,p->used);
    memmove(p->times,p->times+1,p->used*sizeof(p->times[0]));
    return false;
}

/* Per-physical-port stream identity, separate from per-session UCI sequence. */
typedef struct {
    bool seen;uint32_t boot,last_sequence;
    uint32_t accepted,missing,duplicates,backwards,restarts,wrong_node;
} event_tracker_t;
static inline bool event_track(event_tracker_t *t,const event_t *e,uint8_t expected_node) {
    if(e->node!=expected_node){t->wrong_node++;return false;}
    if(t->seen && t->boot==e->boot) {
        uint32_t delta=e->sequence-t->last_sequence;
        if(!delta){t->duplicates++;return false;}
        if(delta>=0x80000000u){t->backwards++;return false;}
        t->missing+=delta-1;
    }else if(t->seen)t->restarts++;
    t->seen=true;t->boot=e->boot;t->last_sequence=e->sequence;t->accepted++;
    return true;
}
#endif
