#define ANCHOR_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include "../type2bp/source/anchor_app.c"
static uint32_t clock_ms;static unsigned calls,starts,stops;
static bool query_fail;
static phRangingParams_t role;
static uint8_t hwstate=2;
static const UWB_AppParams_List_t *current;
uint32_t xTaskGetTickCount(void){return clock_ms;}
int mock_printf(const char *f,...){(void)f;return 0;}
void AppCallback(eNotificationType t,void *d){(void)t;(void)d;}
tUWBAPI_STATUS UwbApi_SessionInit(uint32_t id,unsigned type,uint32_t *handle){
    assert(type==0);assert(id==anchor_sessions[0].id||id==anchor_sessions[1].id);
    *handle=id^0xff000000u;calls++;return 0;
}
tUWBAPI_STATUS UwbApi_SetRangingParams(uint32_t h,phRangingParams_t *r){assert(index_of(h)>=0);role=*r;calls++;return 0;}
tUWBAPI_STATUS UwbApi_GetRangingParams(uint32_t h,phRangingParams_t *r){assert(index_of(h)>=0);*r=role;calls++;return 0;}
static uint32_t vals[128];
tUWBAPI_STATUS UwbApi_SetAppConfigMultipleParams(uint32_t h,unsigned n,const UWB_AppParams_List_t *p){
    int i=index_of(h);assert(i>=0);calls++;current=p;
    for(unsigned j=0;j<n;j++){
        assert(p[j].id<128);vals[p[j].id]=p[j].value;
        if(p[j].id==DST_MAC_ADDRESS)assert(p[j].length==2&&short_addr(p[j].bytes)==anchor_sessions[i].peer);
        if(p[j].id==STATIC_STS_IV)assert(p[j].length==6&&memcmp(p[j].bytes,"\1\2\3\4\5\6",6)==0);
    }
    assert(vals[CHANNEL_NUMBER]==5&&vals[RFRAME_CONFIG]==1&&vals[STS_CONFIG]==0);
    assert(vals[RANGING_DURATION]==1000&&vals[RANGING_ROUND_CONTROL]==3&&vals[RESULT_REPORT_CONFIG]==1);
    return 0;
}
tUWBAPI_STATUS UwbApi_GetAppConfig(uint32_t h,eAppConfig id,uint32_t *v){assert(index_of(h)>=0);*v=vals[id];calls++;return 0;}
tUWBAPI_STATUS UwbApi_StartRangingSession(uint32_t h){assert(index_of(h)>=0);starts++;return 0;}
tUWBAPI_STATUS UwbApi_StopRangingSession(uint32_t h){assert(index_of(h)>=0);stops++;return 0;}
tUWBAPI_STATUS UwbApi_GetSessionState(uint32_t h,uint8_t *s){assert(index_of(h)>=0);*s=hwstate;return query_fail?2:0;}
int main(void){
    for(unsigned i=0;i<2;i++){assert(configure(i));assert(handles[i]!=anchor_sessions[i].id);assert(start(i));}
    assert(role.deviceRole==1&&role.deviceType==1&&role.scheduledMode==1);
    phRangingData_t r={0};r.sessionHandle=handles[0];r.ranging_measure_type=1;r.no_of_measurements=1;
    phRangingMesr_t *m=&r.ranging_meas.range_meas_twr[0];m->mac_addr[0]=(uint8_t)anchor_sessions[0].peer;m->mac_addr[1]=(uint8_t)(anchor_sessions[0].peer>>8);m->distance=123;
    unsigned before=calls;clock_ms=100;callback(UWBD_RANGING_DATA,&r);assert(range_ok[0]==1&&last_good[0]==100&&range_ok[1]==0&&calls==before);
    m->status=2;clock_ms=200;callback(UWBD_RANGING_DATA,&r);assert(range_bad[0]==1&&last_good[0]==100);
    r.sessionHandle=anchor_sessions[0].id;callback(UWBD_RANGING_DATA,&r);assert(range_bad[0]==1);
    r.sessionHandle=handles[0];m->mac_addr[0]^=1;callback(UWBD_RANGING_DATA,&r);assert(range_bad[0]==1);
    phUwbSessionInfo_t info={handles[1],3,0x20};callback(UWBD_SESSION_DATA,&info);assert(states[1]==3&&reasons[1]==0x20);
    clock_ms=1000;assert(recover()&&stops==0);clock_ms=30000;assert(recover()&&stops==2&&starts==4);
    query_fail=true;assert(recover());assert(recover());assert(!recover());
    puts("anchor: distinct handles, opposite roles, static STS, callback isolation, peer filter and recovery passed");
}
