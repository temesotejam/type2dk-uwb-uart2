/* Type2BP SR150 anchor: two independent controller/initiator sessions.
 * Settings are generated from the same JSON as the two SR040 tags.
 * No BLE, sensor polling or UWB application data transfer. */
#ifdef ANCHOR_HOST_TEST
#include "mock_anchor_sdk.h"
#else
#include "phUwb_BuildConfig.h"
#include "UwbApi.h"
#include "AppInternal.h"
#include "AppRecovery.h"
#include "phOsalUwb_Thread.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fsl_debug_console.h"
#include "fsl_reset.h"
#endif
#include "anchor_profile.h"
#include <string.h>
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
AppContext_t appContext;
static uint32_t handles[2]={0xffffffffu,0xffffffffu};
static volatile uint8_t states[2]={255,255},reasons[2]={255,255},reset_seen;
static volatile uint32_t range_ok[2],range_bad[2],last_good[2],heartbeat;
static uint32_t attempts[2];static uint8_t api_errors[2];
static uint32_t now_ms(void){return xTaskGetTickCount()*portTICK_PERIOD_MS;}
static int index_of(uint32_t h){for(unsigned i=0;i<2;i++)if(handles[i]!=0xffffffffu&&handles[i]==h)return (int)i;return -1;}
static uint16_t short_addr(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void callback(eNotificationType type,void *data){
    if(type==UWBD_RANGING_DATA){
        const phRangingData_t *r=data;
        if(!r||r->ranging_measure_type!=MEASUREMENT_TYPE_TWOWAY||r->mac_addr_mode_indicator!=0||r->no_of_measurements>MAX_NUM_RESPONDERS)return;
        int i=index_of(r->sessionHandle);if(i<0)return;
        for(unsigned j=0;j<r->no_of_measurements;j++){
            const phRangingMesr_t *m=&r->ranging_meas.range_meas_twr[j];
            if(short_addr(m->mac_addr)!=anchor_sessions[i].peer)continue;
            if(m->status==UWBAPI_STATUS_OK&&m->distance!=65535){range_ok[i]++;last_good[i]=now_ms();}
            else range_bad[i]++;
        }
        return;
    }
    if(type==UWBD_SESSION_DATA&&data){const phUwbSessionInfo_t *s=data;int i=index_of(s->sessionHandle);if(i>=0){states[i]=s->state;reasons[i]=s->reason_code;}return;}
    if(type==UWBD_DEVICE_RESET||type==UWBD_RECOVERY_NTF){reset_seen=1;return;}
    if(type==UWBD_DATA_RCV_NTF||type==UWBD_DATA_TRANSMIT_NTF)return;
    AppCallback(type,data);
}
static bool ok(const char *step,tUWBAPI_STATUS s,uint32_t sid){
    PRINTF("BP_INIT,id=%u,step=%s,sid=%08lx,status=%u\r\n",ANCHOR_ID,step,(unsigned long)sid,(unsigned)s);
    return s==UWBAPI_STATUS_OK;
}
#ifndef ANCHOR_HOST_TEST
static bool calibrate(void){
    /* Murata v04.08.01 patch: apply each board's OTP in RAM. Never write OTP.
     * TX1 RMS correction (2.1 - 0.6 + 0.5) dB = 8 quarter-dB units. */
    phCalibPayload_t c;
    const uint8_t channels[]={5,9};
    for(unsigned i=0;i<COUNT(channels);i++){
        memset(&c,0,sizeof(c));
        if(!ok("OTP_TX",UwbApi_ReadOtpCalibDataCmd(channels[i],1u<<1,&c),0))return false;
        uint8_t tx[11]={2,1,0,0,0,0,2,0,0,0,0};
        tx[2]=c.TX_POWER_ID[1];tx[4]=(uint8_t)(c.TX_POWER_ID[0]+8u);
        if(!ok("CAL_TX",UwbApi_SetCalibration(channels[i],TX_POWER_PER_ANTENNA,tx,sizeof(tx)),0))return false;
    }
    memset(&c,0,sizeof(c));
    if(!ok("OTP_XTAL",UwbApi_ReadOtpCalibDataCmd(9,1u<<2,&c),0))return false;
    uint8_t clk[7]={3,0,0,0,0,0,0};clk[1]=c.XTAL_CAP_VALUES[0];clk[3]=c.XTAL_CAP_VALUES[1];clk[5]=c.XTAL_CAP_VALUES[2];
    return ok("CAL_XTAL",UwbApi_SetCalibration(9,RF_CLK_ACCURACY_CALIB,clk,sizeof(clk)),0);
}
#endif
static bool configure(unsigned i){
    const anchor_session_t *s=&anchor_sessions[i];
    uint8_t peer[2]={(uint8_t)s->peer,(uint8_t)(s->peer>>8)};
    phRangingParams_t r={0},check={0};
    if(!ok("SESSION",UwbApi_SessionInit(s->id,UWBD_RANGING_SESSION,&handles[i]),s->id))return false;
    r.deviceRole=kUWB_DeviceRole_Initiator;r.deviceType=kUWB_DeviceType_Controller;
    r.multiNodeMode=kUWB_MultiNodeMode_UniCast;r.macAddrMode=0;
    r.deviceMacAddr[0]=(uint8_t)ANCHOR_ADDRESS;r.deviceMacAddr[1]=(uint8_t)(ANCHOR_ADDRESS>>8);
    r.scheduledMode=kUWB_ScheduledMode_TimeScheduled;r.rangingRoundUsage=kUWB_RangingRoundUsage_DS_TWR;
    if(!ok("ROLE",UwbApi_SetRangingParams(handles[i],&r),s->id))return false;
    const UWB_AppParams_List_t cfg[]={
        UWB_SET_APP_PARAM_VALUE(NO_OF_CONTROLEES,1),
        UWB_SET_APP_PARAM_ARRAY(DST_MAC_ADDRESS,peer,sizeof(peer)),
        UWB_SET_APP_PARAM_VALUE(RFRAME_CONFIG,TAG_RADIO_RFRAME),
        UWB_SET_APP_PARAM_VALUE(SLOTS_PER_RR,TAG_RADIO_SLOTS),
        UWB_SET_APP_PARAM_VALUE(SLOT_DURATION,TAG_RADIO_SLOT_DURATION),
        UWB_SET_APP_PARAM_VALUE(RANGING_DURATION,s->interval),
        UWB_SET_APP_PARAM_VALUE(MAX_RR_RETRY,0),
        UWB_SET_APP_PARAM_VALUE(CHANNEL_NUMBER,TAG_RADIO_CHANNEL),
        UWB_SET_APP_PARAM_VALUE(SFD_ID,TAG_RADIO_SFD),
        UWB_SET_APP_PARAM_VALUE(PREAMBLE_CODE_INDEX,TAG_RADIO_PREAMBLE),
        UWB_SET_APP_PARAM_VALUE(PRF_MODE,0),
        UWB_SET_APP_PARAM_VALUE(AOA_RESULT_REQ,0),
        UWB_SET_APP_PARAM_VALUE(SESSION_INFO_NTF,1),
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
    if(!ok("CONFIG",UwbApi_SetAppConfigMultipleParams(handles[i],COUNT(cfg),cfg),s->id))return false;
    if(!ok("READBACK",UwbApi_GetRangingParams(handles[i],&check),s->id))return false;
    if(check.deviceRole!=r.deviceRole||check.deviceType!=r.deviceType||check.multiNodeMode!=0||check.macAddrMode!=0||
       check.scheduledMode!=r.scheduledMode||check.rangingRoundUsage!=r.rangingRoundUsage||memcmp(check.deviceMacAddr,r.deviceMacAddr,2))return false;
    const struct {eAppConfig id;uint32_t value;} verify[]={
        {CHANNEL_NUMBER,TAG_RADIO_CHANNEL},{SFD_ID,TAG_RADIO_SFD},{PREAMBLE_CODE_INDEX,TAG_RADIO_PREAMBLE},
        {RFRAME_CONFIG,TAG_RADIO_RFRAME},{RANGING_DURATION,s->interval},{SLOT_DURATION,TAG_RADIO_SLOT_DURATION},
        {STS_CONFIG,0},{VENDOR_ID,TAG_RADIO_VENDOR_ID},{RANGING_ROUND_CONTROL,3},{RESULT_REPORT_CONFIG,1},
    };
    for(unsigned j=0;j<COUNT(verify);j++){
        uint32_t value=0; tUWBAPI_STATUS st=UwbApi_GetAppConfig(handles[i],verify[j].id,&value);
        if(st!=UWBAPI_STATUS_OK||value!=verify[j].value){PRINTF("BP_CONFIG_ERROR,id=%u,param=%u,value=%lu,status=%u\r\n",ANCHOR_ID,(unsigned)verify[j].id,(unsigned long)value,(unsigned)st);return false;}
    }
    PRINTF("BP_SESSION,id=%u,node=%u,sid=%08lx,handle=%08lx,peer=%04x\r\n",ANCHOR_ID,s->node,(unsigned long)s->id,(unsigned long)handles[i],s->peer);
    return true;
}
static bool start(unsigned i){
    tUWBAPI_STATUS s=UwbApi_StartRangingSession(handles[i]);attempts[i]=now_ms();
    uint8_t state=255;tUWBAPI_STATUS query=UwbApi_GetSessionState(handles[i],&state);
    ok("START",s,anchor_sessions[i].id);
    if(query!=UWBAPI_STATUS_OK)return false;
    states[i]=state;
    if(state==UWBAPI_SESSION_ACTIVATED){last_good[i]=now_ms();return true;}
    return state==UWBAPI_SESSION_IDLE;
}
static bool recover(void){
    for(unsigned i=0;i<2;i++){
        uint8_t state=255;
        if(UwbApi_GetSessionState(handles[i],&state)!=UWBAPI_STATUS_OK){if(++api_errors[i]>=3)return false;continue;}
        states[i]=state;api_errors[i]=0;uint32_t t=now_ms();
        if(state==UWBAPI_SESSION_IDLE){if(t-attempts[i]>5000u&&!start(i))return false;}
        else if(state==UWBAPI_SESSION_ACTIVATED){
            uint32_t timeout=15000u+ANCHOR_ID*211u+i*131u;
            if(t-last_good[i]>timeout&&t-attempts[i]>timeout){attempts[i]=t;if(UwbApi_StopRangingSession(handles[i])==UWBAPI_STATUS_OK&&!start(i))return false;}
        }else return false;
    }
    return true;
}
#ifndef ANCHOR_HOST_TEST
static OSAL_TASK_RETURN_TYPE guard(void *p){(void)p;for(;;){phOsalUwb_Delay(500);if(now_ms()-heartbeat>180000u)RESET_SystemReset();}}
static OSAL_TASK_RETURN_TYPE run(void *p){
    (void)p;heartbeat=now_ms();
    PRINTF("BOOT,TYPE2BP_ANCHOR_V0.2.0,id=%u,address=%04x\r\n",ANCHOR_ID,ANCHOR_ADDRESS);
    phUwbappContext_t ctx={0};
#if UWB_BLD_CFG_FW_DNLD_DIRECTLY_FROM_HOST
    ctx.fwImageCtx.fwImage=(uint8_t*)heliosEncryptedMainlineFwImage;ctx.fwImageCtx.fwImgSize=heliosEncryptedMainlineFwImageLen;
#endif
    ctx.fwImageCtx.fwMode=MAINLINE_FW;ctx.pCallback=callback;
    if(!ok("UWB",UwbApi_Init_New(&ctx),0)||!calibrate())goto fail;
    reset_seen=0;
    for(unsigned i=0;i<2;i++)if(!configure(i))goto fail;
    for(unsigned i=0;i<2;i++){if(!start(i))goto fail;phOsalUwb_Delay(150);}
    for(;;){
        heartbeat=now_ms();if(reset_seen||!recover())goto fail;
        for(unsigned i=0;i<2;i++)PRINTF("BP_STAT,id=%u,node=%u,state=%u,reason=%u,ok=%lu,fail=%lu\r\n",ANCHOR_ID,anchor_sessions[i].node,states[i],reasons[i],(unsigned long)range_ok[i],(unsigned long)range_bad[i]);
        phOsalUwb_Delay(2000);
    }
fail:
    PRINTF("BP_RECOVER,id=%u,action=REBOOT\r\n",ANCHOR_ID);phOsalUwb_Delay(3000+ANCHOR_ID*211u);RESET_SystemReset();for(;;){}
}
UWBOSAL_TASK_HANDLE uwb_demo_start(void){
    UWBOSAL_TASK_HANDLE h=NULL,g=NULL;phOsalUwb_ThreadCreationParams_t p={0};heartbeat=now_ms();
    p.stackdepth=256;p.priority=5;PHOSALUWB_SET_TASKNAME(p,"BpGuard");if(phOsalUwb_Thread_Create((void**)&g,guard,&p)!=0)RESET_SystemReset();
    p.stackdepth=2048;p.priority=4;PHOSALUWB_SET_TASKNAME(p,"BpRange");if(phOsalUwb_Thread_Create((void**)&h,run,&p)!=0)RESET_SystemReset();return h;
}
#endif
