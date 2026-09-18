#ifndef RANGE_VIEW_H
#define RANGE_VIEW_H
#include "event_protocol.h"
#include <string.h>
/* Latest result per fixed anchor. Failure/idle never resurrects an old distance. */
typedef struct {event_t event;uint64_t rx_us;bool seen,active;} range_cell_t;
#define RANGE_ANCHOR_COUNT 7
static const uint16_t range_anchor_ids[RANGE_ANCHOR_COUNT]={0x1111,0x2222,0x3333,0x4444,0x5555,0x6666,0x7777};
static inline void range_view_update(range_cell_t cells[RANGE_ANCHOR_COUNT],const event_t *e,uint64_t rx,bool reboot){
    if(reboot||e->type==EVENT_TEST)memset(cells,0,RANGE_ANCHOR_COUNT*sizeof(*cells));
    if(e->type==EVENT_HEALTH&&e->anchor==0){
        if(e->fault!=0||e->session_state!=2)
            for(unsigned j=0;j<RANGE_ANCHOR_COUNT;j++)
                if(cells[j].seen&&cells[j].event.session_id==e->session_id)cells[j].active=false;
        return;
    }
    unsigned i=0;for(;i<RANGE_ANCHOR_COUNT;i++)if(range_anchor_ids[i]==e->anchor)break;
    if(i==RANGE_ANCHOR_COUNT)return;
    range_cell_t *c=&cells[i];
    if(e->type==EVENT_RANGE){c->event=*e;c->rx_us=rx;c->seen=true;c->active=e->session_state==2&&e->fault==0;}
    else if(e->type==EVENT_HEALTH&&(e->fault!=0||e->session_state!=2))c->active=false;
}
static inline bool range_view_fresh(const range_cell_t *c,uint64_t now){
    return c->seen&&c->active&&now>=c->rx_us&&now-c->rx_us<3000000u&&
           (c->event.flags&(EVENT_DISTANCE_VALID|EVENT_PROFILE_CONFIRMED))==
           (EVENT_DISTANCE_VALID|EVENT_PROFILE_CONFIRMED)&&c->event.status==0&&
           c->event.range_cm!=EVENT_NO_DISTANCE;
}
#endif
