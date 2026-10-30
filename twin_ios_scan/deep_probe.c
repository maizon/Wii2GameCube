/* Read-mostly HID v4 backend for IOS55. No NAND, no root port writes.
   GETDEVICECHANGE(0), GETVERSION(6), SHUTDOWN(7), CANCELINTERRUPT(8).
   Device child RESET(29) only if previously enumerated VID:PID.
   Buffers are static MEM2 across any outstanding asynchronous IPC.
   Source protocol: https://wiibrew.org/wiki//dev/usb/hid_(v4) */
#include <gccore.h>
#include <ogc/ipc.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <ogc/cache.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "deep_probe.h"
static char hid_path[] ATTRIBUTE_ALIGN(32)="/dev/usb/hid";
static char twin_path[] ATTRIBUTE_ALIGN(32)="/dev/usb/oh0/810/1";
static volatile s32 ipc_result;
static volatile int ipc_done;
static unsigned char *devbuf,*inbuf,*argbuf,*cancelbuf;
static int mem_ready=0, backend_quarantined=0;
static u32 now_ms(void){return ticks_to_millisecs(gettime());}
static s32 ipc_cb(s32 r,void *ctx){(void)ctx;ipc_result=r;ipc_done=1;return 0;}
static void await_reply(u32 limit){
    u32 start=now_ms();
    while(!ipc_done && (u32)(now_ms()-start)<limit) usleep(10000);
}
static int prepare_mem2(maizon_log_fn log)
{
    if(mem_ready)return 1;
    uintptr_t start=(uintptr_t)SYS_GetArena2Lo();
    uintptr_t hi=(uintptr_t)SYS_GetArena2Hi();
    uintptr_t p=(start+31u)&~(uintptr_t)31u;
    const size_t needed=0x600+64+32+32;
    if(p<start || p+needed<p || p+needed>hi){
        log("HIDv4: MEM2 insuficiente (%08X..%08X)",(u32)start,(u32)hi);
        return 0;
    }
    devbuf=(unsigned char *)p;
    inbuf=devbuf+0x600;
    argbuf=inbuf+64;
    cancelbuf=argbuf+32;
    SYS_SetArena2Lo((void *)(p+needed));
    memset(devbuf,0,needed);
    mem_ready=1;
    log("HIDv4: buffers MEM2 %u bytes, DMA alinhado",(unsigned)needed);
    return 1;
}
static u32 be32(const unsigned char *p){
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static u16 be16(const unsigned char *p){return ((u16)p[0]<<8)|p[1];}
typedef struct {
    u8 prefix[16];s32 device_no;
    union{struct{u32 endpoint,length;}intr;u8 raw[8];}kind;
    void *data;
} v4_request;
typedef struct{int found;s32 id;u8 ep;u16 ep_len;} v4_target;
static v4_target parse_devices(maizon_log_fn log,s32 result)
{
    v4_target t={0,-1,0,0};
    log("HIDv4 GETDEVICECHANGE retorno=%d",(int)result);
    if(result<0)return t;
    for(u32 off=0,idx=0;off+8<=0x600 && idx<32;idx++) {
        const u8 *d=devbuf+off;
        u32 total=be32(d),id=be32(d+4);
        if(total==0xFFFFFFFFu)break;
        if(total<8 || total>0x600-off || (total&3)) {
            log("HIDv4: bloco invalido off=%u total=%08X",(unsigned)off,(unsigned)total);
            break;
        }
        if(total>=28 && d[8]==18 && d[9]==1) {
            u16 vid=be16(d+16),pid=be16(d+18);
            log("HIDv4 #%u id=%u VID:PID=%04X:%04X bytes=%u",
                (unsigned)idx,(unsigned)id,vid,pid,(unsigned)total);
            if(vid==0x0810 && pid==0x0001) {
                t.found=1;t.id=(s32)id;
                log("*** TWIN ENCONTRADO NO HID v4 id=%u ***",(unsigned)id);
                for(u32 off2=8;off2+2<total;){
                    u8 len=d[off2],type=d[off2+1];
                    if(len<2 || off2+len>total)break;
                    if(type==5 && len>=7 && (d[off2+2]&0x80) &&
                       (d[off2+3]&3)==3) {
                        t.ep=d[off2+2];
                        t.ep_len=be16(d+off2+4);
                        log("TWIN endpoint IN=%02X maxpacket=%u",t.ep,t.ep_len);
                        break;
                    }
                    off2+=(len+3u)&~3u;
                }
            }
        }
        off+=total;
    }
    return t;
}
static int capture_input(maizon_log_fn log,s32 fd,const v4_target *t)
{
    if(!t->found || !t->ep || !t->ep_len || t->ep_len>64) {
        log("TWIN: sem endpoint IN valido para captura");
        return 1;
    }
    v4_request req;
    memset(&req,0,sizeof(req));
    req.device_no=t->id;
    req.kind.intr.endpoint=t->ep;
    req.kind.intr.length=t->ep_len;
    req.data=inbuf;
    memcpy(argbuf,&req,sizeof(req));
    memset(inbuf,0,64);
    DCFlushRange(inbuf,64);
    ipc_done=0;ipc_result=-1;
    log("TWIN HIDv4 INPUT: requisitando %u bytes, limite 1000ms",t->ep_len);
    s32 r=IOS_IoctlAsync(fd,3,argbuf,32,NULL,0,ipc_cb,NULL);
    if(r<0){log("TWIN: IOCTL3 falhou %d",(int)r);return 1;}
    await_reply(1000);
    if(!ipc_done) {
        memset(cancelbuf,0,32);
        memcpy(cancelbuf,&t->id,4);
        cancelbuf[4]=t->ep;
        log("TWIN: CANCELINTERRUPT 8 apos timeout");
        s32 rc=IOS_Ioctl(fd,8,cancelbuf,8,NULL,0);
        log("TWIN: cancel rc=%d",(int)rc);
        await_reply(700);
    }
    if(!ipc_done) {
        log("TWIN: IPC pendente; mantendo FD e MEM2, quarantena.");
        backend_quarantined=1;
        return 0;
    }
    DCInvalidateRange(inbuf,64);
    log("TWIN: report rc=%d",(int)ipc_result);
    if(ipc_result>0) {
        unsigned n=ipc_result>32?32:(unsigned)ipc_result;
        char str[100]={0};
        for(unsigned j=0;j<n;j++) snprintf(str+j*3,sizeof(str)-j*3,"%02X ",inbuf[j]);
        log("TWIN report bytes: %s",str);
    }
    return 1;
}
int maizon_hidv4_discover(maizon_log_fn log,unsigned timeout_ms)
{
    if(IOS_GetVersion()!=55)return 0;
    if(backend_quarantined){log("HIDv4: quarantena; nao repetir");return -3;}
    if(!prepare_mem2(log))return -1;
    s32 fd=IOS_Open(hid_path,0);
    log("HIDv4 OPEN=%d",(int)fd);
    if(fd<0)return -1;
    s32 version=IOS_Ioctl(fd,6,NULL,0,NULL,0);
    log("HIDv4 GETVERSION=%08X",(u32)version);
    if(version!=0x40001){IOS_Close(fd);return -1;}
    memset(devbuf,0xFF,0x600);
    ipc_done=0;ipc_result=-1;
    s32 r=IOS_IoctlAsync(fd,0,NULL,0,devbuf,0x600,ipc_cb,NULL);
    log("HIDv4 GETDEVICECHANGE start rc=%d",(int)r);
    if(r<0){IOS_Close(fd);return -1;}
    await_reply(timeout_ms);
    if(!ipc_done) {
        log("HIDv4: timeout, SHUTDOWN IOCTL7");
        s32 shutdown=IOS_Ioctl(fd,7,NULL,0,NULL,0);
        log("HIDv4: shutdown rc=%d",(int)shutdown);
        await_reply(700);
    }
    if(!ipc_done){
        backend_quarantined=1;
        log("HIDv4 sem callback; abandonando FD aberto para preservar MEM2");
        return -2;
    }
    v4_target target=parse_devices(log,ipc_result);
    int can_close=1;
    if(target.found)can_close=capture_input(log,fd,&target);
    if(can_close)IOS_Close(fd);
    return target.found;
}
int maizon_reset_known_twin(maizon_log_fn log)
{
    if(IOS_GetVersion()!=55)return -1;
    s32 fd=IOS_Open(twin_path,0);
    if(fd<0) {
        log("RESET TWIN: sem handle de 0810:0001, rc=%d",(int)fd);
        return 1;
    }
    log("RESET TWIN (somente handle VID 0810 PID 0001), IOCTL29");
    s32 r=IOS_Ioctl(fd,29,NULL,0,NULL,0);
    log("RESET TWIN: rc=%d",(int)r);
    IOS_Close(fd);
    return (int)r;
}
