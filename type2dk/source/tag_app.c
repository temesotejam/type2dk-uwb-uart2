/* Independent ranging tag. No BLE, A-B links, accelerometer or UWB user data. */
#ifdef TAG_HOST_TEST
#include "mock_sdk.h"
#else
#include "phUwb_BuildConfig.h"
#include "UwbApi.h"
#include "AppInternal.h"
#include "AppRecovery.h"
#include "Demo_SR040_RadioConfigAndGroupDelay.h"
#include "phOsalUwb_Thread.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_debug_console.h"
#include "fsl_reset.h"
#include "RNG_Interface.h"
#endif
#include "event_uart.h"
#include "tag_profile.h"
#include <string.h>
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static volatile uint8_t states[TAG_SESSION_COUNT],reasons[TAG_SESSION_COUNT];
static volatile uint8_t reset_seen,runtime_ready;
static volatile uint32_t heartbeat_ms,last_range[TAG_SESSION_COUNT];
static uint32_t last_attempt[TAG_SESSION_COUNT],boot_id;
static uint8_t api_errors[TAG_SESSION_COUNT];
static uint16_t start_count[TAG_SESSION_COUNT];
static uint32_t now_ms(void){return xTaskGetTickCount()*portTICK_PERIOD_MS;}
static bool retry_due(uint32_t t,uint32_t progress,uint32_t attempt,uint32_t interval){
    return (uint32_t)(t-progress)>=interval && (uint32_t)(t-attempt)>=interval;
}
static int session_index(uint32_t id){
    for(unsigned i=0;i<TAG_SESSION_COUNT;i++)if(tag_sessions[i].id==id)return (int)i;
    return -1;
}
static void on_range(const phRangingData_t *r){
    uint32_t at=now_ms();
    if(!r || r->ranging_measure_type!=MEASUREMENT_TYPE_TWOWAY || r->mac_addr_mode_indicator!=0 ||
       r->no_of_measurements>MAX_NUM_RESPONDERS)return;
    int si=session_index(r->sessionId);if(si<0)return;
    for(unsigned i=0;i<r->no_of_measurements;i++){
        const phRangingMesr_t *m=&r->ranging_meas.range_meas_twr[i];
        if(ev_u16(m->mac_addr)!=tag_sessions[si].peer)continue;
        event_t e={0};e.type=EVENT_RANGE;e.callback_ms=at;e.uci_sequence=r->seq_ctr;
        e.session_id=r->sessionId;e.anchor=tag_sessions[si].anchor;e.range_cm=m->distance;
        e.status=m->status;e.nlos=m->nLos;e.session_state=states[si];e.reason=reasons[si];
        if(m->status==UWBAPI_STATUS_OK && m->distance!=EVENT_NO_DISTANCE){
            e.flags=EVENT_DISTANCE_VALID;last_range[si]=at;
        }
        event_uart_submit(&e); /* Copies only; never blocks on UART or issues UWB commands. */
    }
}
static void tag_callback(eNotificationType type,void *data){
    if(type==UWBD_RANGING_DATA){on_range((const phRangingData_t*)data);return;}
    if(type==UWBD_DATA_RCV_NTF || type==UWBD_DATA_TRANSMIT_NTF)return;
    if(type==UWBD_SESSION_DATA && data){
        const phUwbSessionInfo_t *s=(const phUwbSessionInfo_t*)data;
        int i=session_index(s->session_id);if(i>=0){states[i]=s->state;reasons[i]=s->reason_code;}
        return;
    }
    if(type==UWBD_DEVICE_RESET || type==UWBD_RECOVERY_NTF){reset_seen=1;return;}
    AppCallback(type,data);
}
static bool step_ok(const char *step,tUWBAPI_STATUS status,uint32_t sid){
    PRINTF("INIT,NODE=%d,STEP=%s,SID=%08lx,STATUS=%u\r\n",TAG_NODE,step,(unsigned long)sid,(unsigned)status);
    return status==UWBAPI_STATUS_OK;
}
static bool configure(const tag_session_t *s)
{
    phRangingParams_t r={0},check={0};
    const UWB_AppParams_List_t cfg[]={
        UWB_SET_APP_PARAM_VALUE(RANGING_ROUND_USAGE,kUWB_RangingRoundUsage_DS_TWR),
        UWB_SET_APP_PARAM_VALUE(RFRAME_CONFIG,TAG_RADIO_RFRAME),
        UWB_SET_APP_PARAM_VALUE(SLOTS_PER_RR,TAG_RADIO_SLOTS),
        UWB_SET_APP_PARAM_VALUE(SLOT_DURATION,TAG_RADIO_SLOT_DURATION),
        UWB_SET_APP_PARAM_VALUE(RANGING_INTERVAL,s->interval),
        UWB_SET_APP_PARAM_VALUE(MAX_RR_RETRY,0),
        UWB_SET_APP_PARAM_VALUE(CHANNEL_NUMBER,TAG_RADIO_CHANNEL),
        UWB_SET_APP_PARAM_VALUE(SFD_ID,TAG_RADIO_SFD),
        UWB_SET_APP_PARAM_VALUE(PREAMBLE_CODE_INDEX,TAG_RADIO_PREAMBLE),
        UWB_SET_APP_PARAM_VALUE(PRF_MODE,kUWB_PrfMode_62_4MHz),
        UWB_SET_APP_PARAM_VALUE(TX_ADAPTIVE_PAYLOAD_POWER,1),
        UWB_SET_APP_PARAM_VALUE(AOA_RESULT_REQ,0),
        UWB_SET_APP_PARAM_VALUE(RNG_DATA_NTF,1),
        UWB_SET_APP_PARAM_VALUE(RANGING_START_OFFSET,s->offset),
        UWB_SET_APP_PARAM_VALUE(STS_CONFIG,0),
        UWB_SET_APP_PARAM_VALUE(VENDOR_ID,TAG_RADIO_VENDOR_ID),
        UWB_SET_APP_PARAM_ARRAY(STATIC_STS_IV,tag_sts_iv,sizeof(tag_sts_iv)),
        UWB_SET_APP_PARAM_VALUE(NUMBER_OF_STS_SEGMENTS,1),
        UWB_SET_APP_PARAM_VALUE(STS_LENGTH,1),
        UWB_SET_APP_PARAM_VALUE(RANGING_ROUND_CONTROL,3),
        UWB_SET_APP_PARAM_VALUE(RESULT_REPORT_CONFIG,1),
        UWB_SET_APP_PARAM_VALUE(MAC_FCS_TYPE,0),
        UWB_SET_APP_PARAM_VALUE(PSDU_DATA_RATE,0),
        UWB_SET_APP_PARAM_VALUE(PREAMBLE_DURATION,1),
        UWB_SET_APP_PARAM_VALUE(RANGING_TIME_STRUCT,1),
        UWB_SET_APP_PARAM_VALUE(HOPPING_MODE,0),
    };
    if(!step_ok("SESSION",UwbApi_SessionInit(s->id,UWBD_RANGING_SESSION),s->id))return false;
    if(!step_ok("CONFIG",UwbApi_SetAppConfigMultipleParams(s->id,COUNT(cfg),cfg),s->id))return false;
    r.deviceRole=s->init?kUWB_DeviceRole_Initiator:kUWB_DeviceRole_Responder;
    r.deviceType=s->init?kUWB_DeviceType_Controller:kUWB_DeviceType_Controlee;
    r.multiNodeMode=kUWB_MultiNodeMode_UniCast;r.noOfControlees=1;r.macAddrMode=0;
    ev_p16(r.deviceMacAddr,TAG_ADDRESS);ev_p16(r.dstMacAddr,s->peer);
    if(!step_ok("PEERS",UwbApi_SetRangingParams(s->id,&r),s->id))return false;
    if(!step_ok("READBACK",UwbApi_GetRangingParams(s->id,&check),s->id))return false;
    if(check.deviceRole!=r.deviceRole || check.deviceType!=r.deviceType || check.multiNodeMode!=r.multiNodeMode ||
       check.noOfControlees!=1 || check.macAddrMode!=0 || memcmp(check.deviceMacAddr,r.deviceMacAddr,2) ||
       memcmp(check.dstMacAddr,r.dstMacAddr,2))return false;
    /* Read actual radio/timing values before START, not just our requested values. */
    const struct {eAppConfig id;uint32_t expected;const char *name;} verify[]={
        {SLOT_DURATION,TAG_RADIO_SLOT_DURATION,"SLOT"},{RANGING_INTERVAL,s->interval,"INTERVAL"},
        {SLOTS_PER_RR,TAG_RADIO_SLOTS,"SLOTS"},{RFRAME_CONFIG,TAG_RADIO_RFRAME,"RFRAME"},
        {SFD_ID,TAG_RADIO_SFD,"SFD"},{RANGING_START_OFFSET,s->offset,"OFFSET"},
    };
    for(unsigned i=0;i<COUNT(verify);i++){
        uint32_t value=0;
        tUWBAPI_STATUS st=UwbApi_GetAppConfig(s->id,verify[i].id,&value);
        PRINTF("CONFIG,NODE=%d,SID=%08lx,PARAM=%s,VALUE=%lu,EXPECTED=%lu,STATUS=%u\r\n",
            TAG_NODE,(unsigned long)s->id,verify[i].name,(unsigned long)value,
            (unsigned long)verify[i].expected,(unsigned)st);
        if(st!=UWBAPI_STATUS_OK || value!=verify[i].expected)return false;
    }
    PRINTF("SESSION,NODE=%d,SID=%08lx,INIT=%u,SELF=%04x,PEER=%04x,INTERVAL_MS=%lu\r\n",
        TAG_NODE,(unsigned long)s->id,s->init,TAG_ADDRESS,s->peer,(unsigned long)s->interval);
    return true;
}
/* START rejection is session-local. Keep collecting and retry with backoff.
 * A successful command RSP alone does not mean that the session became Active. */
static bool start_session(unsigned i,const char *step)
{
    uint8_t state=UWBAPI_SESSION_ERROR;
    reasons[i]=0xff; /* No reason notification received yet. */
    if(start_count[i]<65535u)start_count[i]++;
    tUWBAPI_STATUS st=UwbApi_StartRangingSession(tag_sessions[i].id);
    last_attempt[i]=now_ms();
    tUWBAPI_STATUS query=UwbApi_GetSessionState(tag_sessions[i].id,&state);
    if(query!=UWBAPI_STATUS_OK){
        PRINTF("SESSION_WAIT,NODE=%d,SID=%08lx,QUERY=%u,STATUS=%u\r\n",
            TAG_NODE,(unsigned long)tag_sessions[i].id,(unsigned)query,(unsigned)st);
        return ++api_errors[i]<3;
    }
    api_errors[i]=0;states[i]=state;
    if(state==UWBAPI_SESSION_ACTIVATED)reasons[i]=0;
    PRINTF("START_RESULT,NODE=%d,SID=%08lx,STEP=%s,STATUS=%u,STATE=%u,REASON=%02x\r\n",
        TAG_NODE,(unsigned long)tag_sessions[i].id,step,(unsigned)st,state,reasons[i]);
    if(state==UWBAPI_SESSION_ACTIVATED){
        reasons[i]=0;
        /* Grace period for peer synchronization, not a valid measurement. */
        last_range[i]=now_ms();
        return true;
    }
    if(state==UWBAPI_SESSION_IDLE){
        PRINTF("SESSION_WAIT,NODE=%d,SID=%08lx,REASON=%02x,ACTION=RETRY_NO_REBOOT\r\n",
            TAG_NODE,(unsigned long)tag_sessions[i].id,reasons[i]);
        return true;
    }
    return false; /* Missing/corrupt session needs full reinitialization. */
}
static bool recover_tag_sessions(void)
{
    for(unsigned i=0;i<COUNT(tag_sessions);i++){
        uint8_t state=UWBAPI_SESSION_ERROR;
        tUWBAPI_STATUS st=UwbApi_GetSessionState(tag_sessions[i].id,&state);
        if(st!=UWBAPI_STATUS_OK){if(++api_errors[i]>=3)return false;continue;}
        api_errors[i]=0;states[i]=state;
        uint32_t t=now_ms(),interval=12000u+TAG_NODE*137u+i*1009u;
        bool stale=false;
        /* Ranging health is independent of UWB application-payload delivery. */
        stale=retry_due(t,last_range[i],last_attempt[i],interval);
        if(state==UWBAPI_SESSION_ACTIVATED && stale){
            PRINTF("RECOVER,NODE=%d,SID=%08lx,REASON=NO_PROGRESS,ACTION=RESTART_SESSION\r\n",TAG_NODE,(unsigned long)tag_sessions[i].id);
            last_attempt[i]=t;
            st=UwbApi_StopRangingSession(tag_sessions[i].id);
            if(st!=UWBAPI_STATUS_OK)continue;
            if(!start_session(i,"RESTART"))return false;
        }else if(state==UWBAPI_SESSION_IDLE){
            uint32_t retry_ms=5000u+TAG_NODE*53u+i*211u;
            if((uint32_t)(t-last_attempt[i])>=retry_ms && !start_session(i,"RETRY"))return false;
        }else if(state!=UWBAPI_SESSION_ACTIVATED){return false;}
    }
    return true;
}

static void send_health(void){
    for(unsigned i=0;i<TAG_SESSION_COUNT;i++){
        event_t e={0};e.type=EVENT_HEALTH;e.callback_ms=now_ms();
        e.session_id=tag_sessions[i].id;e.anchor=tag_sessions[i].anchor;
        e.range_cm=EVENT_NO_DISTANCE;e.status=255;e.nlos=255;
        e.session_state=states[i];e.reason=reasons[i];
        e.fault=reset_seen?3:(!TAG_PROFILE_CONFIRMED?4:0);
        event_uart_submit(&e);
    }
}
static OSAL_TASK_RETURN_TYPE tag_guard(void *unused){
    (void)unused;
    for(;;){phOsalUwb_Delay(250);if(now_ms()-heartbeat_ms>(runtime_ready?45000u:180000u))RESET_SystemReset();}
}
static OSAL_TASK_RETURN_TYPE tag_task(void *unused){
    (void)unused;heartbeat_ms=now_ms();
    memset((void*)states,255,sizeof(states));memset((void*)reasons,255,sizeof(reasons));
    if(RNG_Init()!=gRngSuccess_d || RNG_HwGetRandomNo(&boot_id)!=gRngSuccess_d)goto fail;
    PRINTF("BOOT,TYPE2DK_EVENT_V0.2.0,node=%u,boot=%08lx,profile_confirmed=%u,selftest=%u,clock_quantum_ms=%u\r\n",
        TAG_NODE,(unsigned long)boot_id,TAG_PROFILE_CONFIRMED,TAG_SELF_TEST,(unsigned)portTICK_PERIOD_MS);
    if(!event_uart_start(boot_id))goto fail;
    if(TAG_SELF_TEST){
        uint32_t n=0;
        for(;;){
            heartbeat_ms=now_ms();
            event_t e={0};e.type=EVENT_TEST;e.callback_ms=now_ms();e.uci_sequence=n++;
            e.anchor=1;e.range_cm=(uint16_t)(100+TAG_NODE*10+n%10);e.status=255;e.nlos=255;
            e.session_state=255;e.reason=255;event_uart_submit(&e);
            if(n%10==0)event_uart_log();
            phOsalUwb_Delay(200);
        }
    }
    if(!TAG_PROFILE_CONFIRMED){
        PRINTF("CONFIG_REQUIRED,radio=DISABLED,reason=ANCHOR_PROFILE_NOT_CONFIRMED\r\n");
        for(;;){heartbeat_ms=now_ms();send_health();event_uart_log();phOsalUwb_Delay(1000);}
    }
    if(!step_ok("UWB",UwbApi_Init(tag_callback),0))goto fail;
    if(!step_ok("RADIO",RadioConfigFull_GroupDelay(FALSE),0))goto fail;
    if(!step_ok("SWUP",demo_sr040_swup_update_safe(),0))goto fail;
    reset_seen=0;
    for(unsigned i=0;i<TAG_SESSION_COUNT;i++)if(!configure(&tag_sessions[i]))goto fail;
    for(unsigned i=0;i<TAG_SESSION_COUNT;i++)if(!start_session(i,"START"))goto fail;
    runtime_ready=1;uint32_t health_at=now_ms();
    for(;;){
        heartbeat_ms=now_ms();if(reset_seen)goto fail;
        if(now_ms()-health_at>=2000u){
            health_at=now_ms();send_health();event_uart_log();
            if(!recover_tag_sessions())goto fail;
        }
        phOsalUwb_Delay(10);
    }
fail:
    runtime_ready=0;reset_seen=1;send_health();
    PRINTF("RECOVER,node=%u,reason=INIT_OR_API_ERROR,action=AUTO_REBOOT\r\n",TAG_NODE);
    phOsalUwb_Delay(3000u+TAG_NODE*137u);RESET_SystemReset();for(;;){}
}
UWBOSAL_TASK_HANDLE uwb_demo_start(void){
    phOsalUwb_ThreadCreationParams_t p={0};UWBOSAL_TASK_HANDLE h=NULL,guard=NULL;
    heartbeat_ms=now_ms();
    p.stackdepth=512;p.priority=5;PHOSALUWB_SET_TASKNAME(p,"TagGuard");
    if(phOsalUwb_Thread_Create((void**)&guard,tag_guard,&p)!=0)RESET_SystemReset();
    p.stackdepth=2048;p.priority=4;PHOSALUWB_SET_TASKNAME(p,"RangeTag");
    if(phOsalUwb_Thread_Create((void**)&h,tag_task,&p)!=0)RESET_SystemReset();
    return h;
}
