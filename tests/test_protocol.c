#include <assert.h>
#include <stdio.h>
#include "event_protocol.h"
#include "event_queue.h"
static event_t sample(void){
    event_t e={0};e.type=EVENT_RANGE;e.node=1;e.flags=3;
    e.boot=0x12345678;e.sequence=9;e.callback_ms=0xfffffff0;e.tx_start_ms=10;
    e.uci_sequence=0x87654321;e.session_id=0xa0000101;e.anchor=3;e.range_cm=324;
    e.nlos=7;e.session_state=2;e.reason=0x20;e.dropped=5;e.queue_depth=3;return e;
}
static void feed_frame(event_parser_t *p,const uint8_t *b,unsigned *found,uint64_t start){
    for(unsigned i=0;i<EVENT_SIZE;i++){
        event_t e;uint64_t first,last;
        if(event_feed(p,b[i],start+i*261,&e,&first,&last)){
            assert(first==start);assert(last==start+47*261);assert(e.range_cm==324);(*found)++;
        }
    }
}
int main(void){
    event_t e=sample(),out;uint8_t b[EVENT_SIZE];event_encode(b,&e);
    assert(ev_crc((const uint8_t*)"123456789",9)==0x29b1);
    assert(event_decode(b,&out));assert(out.nlos==7&&out.reason==0x20);
    assert(out.tx_start_ms-out.callback_ms==26);assert(out.boot==e.boot&&out.session_id==e.session_id);
    for(unsigned bit=0;bit<EVENT_SIZE*8;bit++){
        uint8_t bad[EVENT_SIZE];memcpy(bad,b,sizeof bad);bad[bit/8]^=1u<<(bit%8);
        assert(!event_decode(bad,&out));
    }
    event_parser_t p={0};unsigned found=0;
    feed_frame(&p,b,&found,1000000);assert(found==1);
    // Deletion at every byte must resynchronize on the next complete frame.
    for(unsigned lost=0;lost<EVENT_SIZE;lost++){
        memset(&p,0,sizeof p);unsigned n=0;
        for(unsigned i=0;i<EVENT_SIZE;i++)if(i!=lost){uint64_t first,last;event_feed(&p,b[i],i*261,&out,&first,&last);}
        feed_frame(&p,b,&n,20000);assert(n==1);
    }
    // Insert one byte at every possible position.
    for(unsigned at=1;at<EVENT_SIZE;at++){
        memset(&p,0,sizeof p);unsigned n=0;uint64_t t=0,first,last;
        for(unsigned i=0;i<EVENT_SIZE;i++){
            if(i==at){event_feed(&p,0x99,t,&out,&first,&last);t+=261;}
            event_feed(&p,b[i],t,&out,&first,&last);t+=261;
        }
        feed_frame(&p,b,&n,20000);assert(n==1);
    }
    memset(&p,0,sizeof p);uint64_t first,last;
    event_feed(&p,0xd2,0,&out,&first,&last);found=0;
    feed_frame(&p,b,&found,200000);assert(found==1&&p.timeouts==1);
    // Interleaved A/B arrival times are retained independently; arbitrary host scheduling.
    event_parser_t a={0},bp={0};event_t eb=e;eb.node=2;uint8_t bb[48];event_encode(bb,&eb);
    unsigned hits=0;
    for(unsigned i=0;i<48;i++){
        if(event_feed(&a,b[i],10000+i*261,&out,&first,&last)){assert(out.node==1&&first==10000);hits++;}
        if(event_feed(&bp,bb[i],10073+i*261,&out,&first,&last)){assert(out.node==2&&first==10073);hits++;}
    }assert(hits==2);
    event_tracker_t tr={0};assert(event_track(&tr,&e,1));
    assert(!event_track(&tr,&e,1)&&tr.duplicates==1);
    e.sequence+=3;assert(event_track(&tr,&e,1)&&tr.missing==2);
    e.sequence--;assert(!event_track(&tr,&e,1)&&tr.backwards==1);
    e.boot++;e.sequence=0xffffffff;assert(event_track(&tr,&e,1)&&tr.restarts==1);
    e.sequence=0;assert(event_track(&tr,&e,1));
    assert(!event_track(&tr,&eb,1)&&tr.wrong_node==1);
    e=sample();e.type=EVENT_TEST;event_encode(b,&e);assert(!event_decode(b,&out));
    e.flags=0;event_encode(b,&e);assert(event_decode(b,&out));
    e=sample();e.range_cm=EVENT_NO_DISTANCE;event_encode(b,&e);assert(!event_decode(b,&out));
    e.flags=2;event_encode(b,&e);assert(event_decode(b,&out));
    event_queue_t q={0};e=sample();
    for(unsigned i=0;i<EVENT_QUEUE_CAPACITY;i++)assert(event_queue_push(&q,&e));
    assert(!event_queue_push(&q,&e)&&q.dropped==1);
    for(unsigned i=0;i<EVENT_QUEUE_CAPACITY;i++){
        assert(event_queue_pop(&q,&out));assert(out.sequence==i&&out.dropped==1);
    }
    assert(!event_queue_pop(&q,&out));assert(event_queue_push(&q,&e));
    assert(event_queue_pop(&q,&out)&&out.sequence==EVENT_QUEUE_CAPACITY+1);
    puts("protocol: CRC, framing, timestamp retention, dual streams, sequence and queue tests passed");
}
