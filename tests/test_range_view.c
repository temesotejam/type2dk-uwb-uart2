#include <assert.h>
#include <stdio.h>
#include "range_view.h"
int main(void){
    range_cell_t c[3]={0};event_t e={0};e.anchor=1;e.type=EVENT_RANGE;e.range_cm=324;e.flags=3;e.session_state=2;
    range_view_update(c,&e,100,false);assert(range_view_fresh(c,100));
    e.anchor=2;e.range_cm=456;range_view_update(c,&e,200,false);assert(c[0].event.range_cm==324&&c[1].event.range_cm==456);
    e.status=2;e.flags=2;range_view_update(c,&e,300,false);assert(!range_view_fresh(c+1,300)&&range_view_fresh(c,300));
    e.type=EVENT_HEALTH;e.anchor=1;e.session_state=3;range_view_update(c,&e,400,false);assert(!range_view_fresh(c,400));
    e.session_state=2;range_view_update(c,&e,500,false);assert(!range_view_fresh(c,500));
    e.type=EVENT_RANGE;e.status=0;e.flags=3;range_view_update(c,&e,600,false);assert(range_view_fresh(c,600));
    assert(!range_view_fresh(c,3000600));
    e.type=EVENT_HEALTH;range_view_update(c,&e,700,true);assert(!c[0].seen&&!c[1].seen);
    puts("range display: independent anchors, failed/idle/stale/restarted measurements rejected");
}
