#define TAG_HOST_TEST 1
#define TAG_SELF_TEST 0
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "../type2dk/source/tag_app.c"
static uint32_t clock_ms;
static event_t received[128];static unsigned received_count,api_calls,starts,stops;
static uint8_t hardware_state=UWBAPI_SESSION_ACTIVATED;
static bool query_fail;
static phRangingParams_t configured;
static UWB_AppParams_List_t configs[32];static unsigned config_count;
uint32_t xTaskGetTickCount(void){return clock_ms;}
int mock_printf(const char *fmt,...){(void)fmt;return 0;}
void AppCallback(eNotificationType t,void *d){(void)t;(void)d;}
bool event_uart_submit(event_t *e){assert(received_count<128);received[received_count++]=*e;return true;}
tUWBAPI_STATUS UwbApi_SessionInit(uint32_t id,unsigned type){(void)id;(void)type;api_calls++;return 0;}
tUWBAPI_STATUS UwbApi_SetAppConfigMultipleParams(uint32_t sid,unsigned n,const UWB_AppParams_List_t *p){
    (void)sid;api_calls++;assert(n<32);memcpy(configs,p,n*sizeof(*p));config_count=n;return 0;
}
tUWBAPI_STATUS UwbApi_SetRangingParams(uint32_t sid,phRangingParams_t *p){(void)sid;api_calls++;configured=*p;return 0;}
tUWBAPI_STATUS UwbApi_GetRangingParams(uint32_t sid,phRangingParams_t *p){(void)sid;api_calls++;*p=configured;return 0;}
tUWBAPI_STATUS UwbApi_GetAppConfig(uint32_t sid,eAppConfig id,uint32_t *value){
    (void)sid;api_calls++;
    for(unsigned i=0;i<config_count;i++)if(configs[i].id==id){*value=configs[i].value;return 0;}
    return 2;
}
tUWBAPI_STATUS UwbApi_StartRangingSession(uint32_t sid){(void)sid;api_calls++;starts++;return 0;}
tUWBAPI_STATUS UwbApi_StopRangingSession(uint32_t sid){(void)sid;api_calls++;stops++;return 0;}
tUWBAPI_STATUS UwbApi_GetSessionState(uint32_t sid,uint8_t *s){
    (void)sid;api_calls++;*s=hardware_state;return query_fail?2:0;
}
int main(void){
    phRangingData_t r={0};r.sessionId=tag_sessions[0].id;r.seq_ctr=77;
    r.ranging_measure_type=MEASUREMENT_TYPE_TWOWAY;r.no_of_measurements=1;
    phRangingMesr_t *m=&r.ranging_meas.range_meas_twr[0];
    ev_p16(m->mac_addr,tag_sessions[0].peer);m->distance=324;m->nLos=7;states[0]=2;
    clock_ms=100;tag_callback(UWBD_RANGING_DATA,&r);
    assert(received_count==1&&api_calls==0);
    assert(received[0].flags==EVENT_DISTANCE_VALID&&received[0].range_cm==324);
    assert(received[0].nlos==7&&received[0].callback_ms==100&&received[0].uci_sequence==77);
    assert(received[0].anchor==tag_sessions[0].anchor&&last_range[0]==100);
    // Failure carries this notification's raw values, never the preceding good distance.
    clock_ms=200;m->status=2;m->distance=65535;m->nLos=255;r.seq_ctr++;
    tag_callback(UWBD_RANGING_DATA,&r);
    assert(received_count==2&&received[1].flags==0&&received[1].range_cm==65535);
    assert(received[1].status==2&&last_range[0]==100&&api_calls==0);
    m->status=0;tag_callback(UWBD_RANGING_DATA,&r);assert(received[2].flags==0);
    unsigned before=received_count;
    ev_p16(m->mac_addr,TAG_ADDRESS);on_range(&r);assert(received_count==before);
    ev_p16(m->mac_addr,tag_sessions[0].peer);r.mac_addr_mode_indicator=1;on_range(&r);
    r.mac_addr_mode_indicator=0;r.ranging_measure_type=99;on_range(&r);
    r.ranging_measure_type=MEASUREMENT_TYPE_TWOWAY;r.sessionId=0xdead;on_range(&r);
    r.sessionId=tag_sessions[0].id;r.no_of_measurements=MAX_NUM_RESPONDERS+1;on_range(&r);
    assert(received_count==before&&api_calls==0);
    phUwbSessionInfo_t info={tag_sessions[0].id,3,0x20};tag_callback(UWBD_SESSION_DATA,&info);
    assert(states[0]==3&&reasons[0]==0x20);
    for(unsigned i=0;i<TAG_SESSION_COUNT;i++){
        assert(configure(&tag_sessions[i]));
        assert(ev_u16(configured.deviceMacAddr)==TAG_ADDRESS);
        assert(ev_u16(configured.dstMacAddr)==tag_sessions[i].peer);
    }
    clock_ms=1000;hardware_state=UWBAPI_SESSION_IDLE;assert(start_session(0,"TEST"));
    assert(starts==1&&states[0]==3);
    hardware_state=UWBAPI_SESSION_ACTIVATED;clock_ms=2000;
    for(unsigned i=0;i<TAG_SESSION_COUNT;i++)last_range[i]=last_attempt[i]=clock_ms;
    assert(recover_tag_sessions());assert(stops==0);
    clock_ms=30000;assert(recover_tag_sessions());assert(stops==TAG_SESSION_COUNT);
    assert(retry_due(20,0xffff0000,0xffff0000,100));
    query_fail=true;assert(recover_tag_sessions());assert(recover_tag_sessions());assert(!recover_tag_sessions());
    printf("tag %u: callback isolation, failures, peer filtering, profile and recovery passed\n",TAG_NODE);
}
