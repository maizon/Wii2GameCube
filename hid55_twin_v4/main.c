/*
 Maizon Twin HID55 v1.0 -- Wii IOS55 /dev/usb/hid (v4) bridge.
 Experiment: switch IOS58->IOS55 in RAM (no NAND writes), query v4 HID
 device changes, recognize Twin VID:PID 0810:0001, parse descriptors and
 read USB interrupt IN reports. Return to IOS58 to log to FAT USB.
 USB v4 is NOT implemented by libogc USB_Initialize().
 No WPAD input required. No ROMs, IOS WADs, or NAND changes.
 Reference: https://wiibrew.org/wiki//dev/usb/hid_(v4)
*/
#include <gccore.h>
#include <ogc/ipc.h>
#include <ogc/ios.h>
#include <ogc/es.h>
#include <ogc/usb.h>
#include <ogc/usbstorage.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define IOS_TARGET 55
#define IOS_RESTORE 58
#define V4_DESCRIPTOR_BYTES 0x600
#define REPORT_BYTES 64
#define LOG_BYTES (96*1024)
#define RUN_MS 25000
#define READ_TIMEOUT_MS 1800
#define TARGET_VID 0x0810
#define TARGET_PID 0x0001

typedef struct v4_request {
    u8 reserved[16];
    s32 device_id;
    union {
        struct {
            u8 bmRequestType;
            u8 bmRequest;
            u16 wValue;
            u16 wIndex;
            u16 wLength;
        } control;
        struct {u32 endpoint;u32 length;} intr;
    } cmd;
    void *data;
} v4_request;

static char records[LOG_BYTES],status_text[90]="Initializing";
static size_t used=0;
static u64 timer0;
static u8 *change_buffer=NULL,*report_buffer=NULL;
static v4_request *request=NULL;
static u8 *cancel_buffer=NULL;
static char hid_path[] ATTRIBUTE_ALIGN(32)="/dev/usb/hid";
static u8 *mem2_chunk=NULL;
static s32 hid_fd=-1, twin_id=-1;
static u8 twin_ep=0;
static u16 twin_packet=0;
static u32 twin_reports=0;
static int change_inflight=0,read_inflight=0;
static volatile s32 change_ret=-999,read_ret=-999;
static volatile int change_done=0,read_done=0;
static u8 last_report[REPORT_BYTES];
static int last_len=-1;
static u32 next_read_at=0;
static s32 last_error=0;
static u32 error_count=0;
static int found_twin=0,successful_commands=0;
static unsigned event_count=0;
static GXRModeObj *video_mode;
static void *framebuffer;
static int log_saved=0;
static int fatal=0;

static u32 ms(void){return diff_msec(timer0,gettime());}
static void logline(const char *fmt,...){
    char body[370],line[420];va_list ap;int n;
    va_start(ap,fmt);vsnprintf(body,sizeof(body),fmt,ap);va_end(ap);
    n=snprintf(line,sizeof(line),"[%08lu] %s\n",(unsigned long)ms(),body);
    if(n<=0)return;
    if((size_t)n>=sizeof(line))n=sizeof(line)-1;
    if(used+(size_t)n+1>=sizeof(records))return;
    memcpy(records+used,line,n);
    used+=(size_t)n;records[used]=0;
}
static void display(const char *phase){
    printf("\x1b[2J\x1b[H");
    printf("MAIZON - TWIN 0810:0001 / IOS55 USB V4\n");
    printf("--------------------------------------\n");
    printf("Current IOS: %u revision %u\n",
           (unsigned)IOS_GetVersion(),(unsigned)IOS_GetRevision());
    printf("Stage: %.46s\n",phase);
    printf("Time: %lu s   Events: %u\n",(unsigned long)(ms()/1000),event_count);
    printf("Twin: %s / Device ID: %ld\n",found_twin?"FOUND":"NOT YET",(long)twin_id);
    printf("Interrupt-IN ep: %02X packet: %u\n",twin_ep,twin_packet);
    printf("Input reports: %lu, last error: %ld (%lu times)\n",
      (unsigned long)twin_reports,(long)last_error,(unsigned long)error_count);
    if(last_len>0){
       printf("Last packet: ");
       for(int i=0;i<last_len && i<12;i++)printf("%02X ",last_report[i]);
       printf("\n");
    }
    printf("Log: %.65s\n",status_text);
    printf("\nAUTOMATIC: no Wiimote buttons required.\n");
    printf("IOS55 scan 25s -> restore IOS58 -> USB log.\n");
    printf("No IOS/WAD installation. NAND unchanged.\n");
    fflush(stdout);
}
static s32 changed(s32 result,void *arg){
    (void)arg;change_ret=result;change_done=1;return 0;
}
static s32 readcb(s32 result,void *arg){
    (void)arg;read_ret=result;read_done=1;return 0;
}
static u16 le16(const u8 *p){return (u16)p[0]|((u16)p[1]<<8);}
static u32 be32(const u8 *p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static u32 align4(u32 n){return (n+3u)&~3u;}

static void parse_device_change(void){
    twin_id=-1;twin_ep=0;twin_packet=0;found_twin=0;
    u32 off=0, devices=0;
    // A USBv4 block: big-endian total length, device ID, padded descriptors.
    while(off+8<=V4_DESCRIPTOR_BYTES && devices<24){
        u32 size=be32(change_buffer+off);
        if(size==0xffffffffu || size==0)break;
        if(size<16 || size>V4_DESCRIPTOR_BYTES-off || size%4){
           logline("V4_DESCRIPTOR malformed at=%u len=%u",(unsigned)off,(unsigned)size);
           break;
        }
        s32 id=(s32)be32(change_buffer+off+4);
        u16 vid=0,pid=0;u8 ep=0;u16 maxpkt=0;u8 intf=0xff;
        for(u32 p=off+8;p+2<=off+size;){
           u8 len=change_buffer[p],type=change_buffer[p+1];
           if(len<2 || p+len>off+size)break;
           if(type==1 && len>=18){
             vid=le16(change_buffer+p+8);
             pid=le16(change_buffer+p+10);
           }else if(type==4 && len>=9){
             intf=change_buffer[p+2];
           }else if(type==5 && len>=7){
             u8 addr=change_buffer[p+2];
             u8 transfer=change_buffer[p+3]&3;
             if((addr&0x80) && transfer==3 && ep==0){
               ep=addr;maxpkt=le16(change_buffer+p+4)&0x7ff;
             }
           }
           p+=align4(len);
        }
        logline("IOS55 HID_DEVICE[%u] id=%ld vid=%04X pid=%04X intf=%u ep=%02X packet=%u",
           (unsigned)devices,(long)id,vid,pid,intf,ep,maxpkt);
        if(vid==TARGET_VID && pid==TARGET_PID && twin_id<0){
            twin_id=id;twin_ep=ep;twin_packet=maxpkt;found_twin=1;
            if(twin_packet==0)twin_packet=8;
            if(twin_packet>REPORT_BYTES)twin_packet=REPORT_BYTES;
            logline("TARGET FOUND ON IOS55 HID v4! ep=%02X packet=%u",twin_ep,twin_packet);
        }
        off+=size;devices++;
    }
    logline("IOS55 v4 device-change: count=%u TWIN=%s",
       (unsigned)devices,found_twin?"FOUND":"ABSENT");
}
static void request_changes(void){
    if(change_inflight || hid_fd<0)return;
    memset(change_buffer,0xff,V4_DESCRIPTOR_BYTES);
    DCFlushRange(change_buffer,V4_DESCRIPTOR_BYTES);
    change_done=0;change_ret=-999;
    s32 ret=IOS_IoctlAsync(hid_fd,0,NULL,0,change_buffer,
                             V4_DESCRIPTOR_BYTES,changed,NULL);
    if(ret>=0){change_inflight=1;logline("IOS55 V4 GET_DEVICE_CHANGE async issued");}
    else {logline("IOS55 V4 GET_DEVICE_CHANGE submit failed=%ld",(long)ret);fatal=1;}
}
static void process_changes(void){
    if(!change_done)return;
    change_done=0;change_inflight=0;event_count++;
    logline("IOS55 V4 GET_DEVICE_CHANGE callback rc=%ld",(long)change_ret);
    if(change_ret>=0){
       DCInvalidateRange(change_buffer,V4_DESCRIPTOR_BYTES);
       if(read_inflight){
          // Do not overwrite an in-flight IOCTL's shared request.
          // Wait for read completion before scheduling a new one.
          logline("DEVICE CHANGE while read pending; read result may be stale");
       }
       parse_device_change();
       request_changes();
    }else{
       logline("IOS55 change callback negative=%ld; not retrying immediately",
           (long)change_ret);
    }
}
static void start_report(void){
    if(hid_fd<0 || read_inflight || twin_id<0 || !twin_ep || ms()<next_read_at)return;
    memset(request,0,sizeof(*request));
    request->device_id=twin_id;
    request->cmd.intr.endpoint=twin_ep;
    request->cmd.intr.length=twin_packet;
    request->data=report_buffer;
    memset(report_buffer,0,REPORT_BYTES);
    DCFlushRange(request,32);
    DCFlushRange(report_buffer,REPORT_BYTES);
    read_done=0;read_ret=-999;
    s32 result=IOS_IoctlAsync(hid_fd,3,request,32,NULL,0,readcb,NULL);
    if(result<0){
       logline("IOS55 V4 INTERRUPT IN submit failed=%ld",(long)result);
       last_error=result;error_count++;next_read_at=ms()+200;
    }else read_inflight=1;
}
static void process_report(void){
    if(!read_done)return;
    read_done=0;read_inflight=0;
    if(read_ret>=0){
        DCInvalidateRange(report_buffer,REPORT_BYTES);
        // On v4, successful interrupt IOCTL may return 0, not the length.
        int n=(read_ret>0 && read_ret<=REPORT_BYTES)?read_ret:twin_packet;
        if(n>REPORT_BYTES)n=REPORT_BYTES;
        if(last_len!=n || memcmp(last_report,report_buffer,n)!=0){
            char hex[REPORT_BYTES*3+1]={0};
            for(int i=0;i<n;i++)sprintf(hex+i*3,"%02X ",report_buffer[i]);
            logline("TWIN INPUT report=%lu len=%d bytes=%s",
              (unsigned long)(twin_reports+1),n,hex);
            memcpy(last_report,report_buffer,n);last_len=n;
        }
        twin_reports++;successful_commands=1;error_count=0;last_error=0;
        next_read_at=ms()+8;
    }else{
        last_error=read_ret;error_count++;
        if(error_count==1 || error_count%25==0)
           logline("IOS55 V4 INPUT error=%ld repeats=%lu",
                    (long)read_ret,(unsigned long)error_count);
        next_read_at=ms()+80;
    }
}
static void read_cancel_if_stuck(void){
    // Cancel only on shutdown, not at every NAK: avoid lockups.
    if(!read_inflight)return;
    memset(cancel_buffer,0,32);
    *(u32*)cancel_buffer=(u32)twin_id;
    cancel_buffer[4]=twin_ep;
    s32 r=IOS_Ioctl(hid_fd,8,cancel_buffer,8,NULL,0);
    logline("IOS55 V4 CANCEL_INTERRUPT returned=%ld",(long)r);
}
static int save_log(void){
    int ret=USB_Initialize();logline("RESTORED IOS58 USB_Initialize=%d",ret);
    if(ret<0)return 0;
    for(int retry=0;retry<14;retry++){
       if(fatMountSimple("usb",&__io_usbstorage)){
          mkdir("usb:/apps",0777);
          mkdir("usb:/apps/MAIZON_HID55_TWIN",0777);
          mkdir("usb:/apps/MAIZON_HID55_TWIN/logs",0777);
          FILE *out=fopen("usb:/apps/MAIZON_HID55_TWIN/logs/IOS55_TWIN.txt","wb");
          if(!out)out=fopen("usb:/IOS55_TWIN.txt","wb");
          if(out){
             size_t done=fwrite(records,1,used,out);
             int flush=fflush(out);
             int close_result=fclose(out);
             if(done==used && flush==0 && close_result==0){
                 log_saved=1;
                 snprintf(status_text,sizeof(status_text),"SAVED: apps/MAIZON_HID55_TWIN/logs/IOS55_TWIN.txt");
                 return 1;
             }
          }
          snprintf(status_text,sizeof(status_text),"USB write failed; log retained in RAM");
          fatUnmount("usb:");
       }
       VIDEO_WaitVSync();
       for(int j=0;j<30;j++)VIDEO_WaitVSync();
    }
    snprintf(status_text,sizeof(status_text),"Cannot mount USB after IOS reload");
    return 0;
}
int main(void){
    VIDEO_Init();
    video_mode=VIDEO_GetPreferredMode(NULL);
    framebuffer=MEM_K0_TO_K1(SYS_AllocateFramebuffer(video_mode));
    VIDEO_Configure(video_mode);
    console_init(framebuffer,20,20,video_mode->fbWidth-40,
        video_mode->xfbHeight-40,video_mode->fbWidth*2);
    VIDEO_ClearFrameBuffer(video_mode,framebuffer,COLOR_BLACK);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    timer0=gettime();
    logline("MAIZON TWIN IOS55 USBv4 v1.0 original IOS=%u rev=%u",
            (unsigned)IOS_GetVersion(),(unsigned)IOS_GetRevision());
    logline("MISSION: enumerate 0810:0001 and read button report on IOS55 v4");
    logline("NO NAND writes, no WAD, no SD, no WPAD dependency");
    // Preallocate the DMA and descriptor buffer from MEM2. Never free while
    // an IOS async IOCTL is pending; the buffers survive until IOS58 reload.
    u32 arena=(u32)SYS_GetArena2Lo();
    arena=(arena+31u)&~31u;
    if(arena+V4_DESCRIPTOR_BYTES+REPORT_BYTES+32+32 < (u32)SYS_GetArena2Hi()){
      mem2_chunk=(u8*)arena;
      change_buffer=mem2_chunk;
      report_buffer=mem2_chunk+V4_DESCRIPTOR_BYTES;
      request=(v4_request*)(mem2_chunk+V4_DESCRIPTOR_BYTES+REPORT_BYTES);
      cancel_buffer=mem2_chunk+V4_DESCRIPTOR_BYTES+REPORT_BYTES+32;
      SYS_SetArena2Lo((void*)(arena+V4_DESCRIPTOR_BYTES+REPORT_BYTES+64));
      logline("MEM2 allocated descriptors=%08lX reports=%08lX args=%08lX",
       (unsigned long)(u32)change_buffer,(unsigned long)(u32)report_buffer,
       (unsigned long)(u32)request);
    }else{
      logline("ERROR: Cannot allocate USB v4 MEM2");
      fatal=1;
    }
    // Title check: READ ONLY. If title absent do NOT reload.
    u32 tmd_size=0;
    s32 chk=ES_GetStoredTMDSize((1ULL<<32)|IOS_TARGET,&tmd_size);
    logline("IOS55 ES_GetStoredTMDSize=%ld size=%lu",
         (long)chk,(unsigned long)tmd_size);
    int reloaded=0;
    if(!fatal && chk>=0 && tmd_size>0){
       snprintf(status_text,sizeof(status_text),"Reloading installed IOS55 in RAM");
       display("Switch to original IOS55");
       logline("IOS_ReloadIOS(55) about to run");
       s32 rc=IOS_ReloadIOS(IOS_TARGET);
       logline("IOS_ReloadIOS(55) returned=%ld now IOS=%u",(long)rc,(unsigned)IOS_GetVersion());
       if(rc>=0 && IOS_GetVersion()==IOS_TARGET){
           reloaded=1;
           snprintf(status_text,sizeof(status_text),"IOS55 HID v4 direct IOCTL");
           display("Read legacy USB/HID v4");
           hid_fd=IOS_Open(hid_path,IPC_OPEN_NONE);
           logline("IOS55 HID IOS_Open fd=%ld",(long)hid_fd);
           if(hid_fd>=0){
               s32 v=IOS_Ioctl(hid_fd,6,NULL,0,NULL,0);
               logline("IOS55 HID_GETVERSION ioctl6=%ld expected=262145",(long)v);
               if(v==0x40001){
                   logline("SUCCESS: IOS55 legacy HID v4 is active");
                   request_changes();
                   u32 scan_started=ms(),last_screen=0;
                   u32 read_started=0;
                   while(ms()-scan_started<RUN_MS && !fatal){
                      process_changes();
                      process_report();
                      start_report();
                      if(read_inflight){
                        if(read_started==0)read_started=ms();
                        if(ms()-read_started>READ_TIMEOUT_MS){
                           logline("INPUT request exceeded %ums; cancel after scan",READ_TIMEOUT_MS);
                           // Stop issuing new reads; keep scanning while the buffer
                           // remains allocated (do not abandon transfer memory).
                           read_inflight=2;
                           break;
                        }
                      }else read_started=0;
                      if(ms()-last_screen>700){
                         display("Legacy HID v4 active - 25s automatic");
                         last_screen=ms();
                      }
                      VIDEO_WaitVSync();
                   }
                   if(read_inflight)read_cancel_if_stuck();
                   if(change_inflight){
                       s32 stop=IOS_Ioctl(hid_fd,7,NULL,0,NULL,0);
                       logline("IOS55 HID SHUTDOWN ioctl7 rc=%ld",(long)stop);
                   }
                   // Allow async IOCTL callbacks to finish without freeing memory.
                   u32 wait_started=ms();
                   while(ms()-wait_started<900 && (read_inflight || change_inflight)){
                      if(read_done){read_done=0;read_inflight=0;}
                      if(change_done){change_done=0;change_inflight=0;}
                      VIDEO_WaitVSync();
                   }
               }else{
                   logline("IOS55 HID is NOT v4 (ioctl6=%ld), refusing v4 commands",(long)v);
               }
               // IOS reload invalidates old fd, no need to close with pending callbacks.
               if(!read_inflight && !change_inflight)IOS_Close(hid_fd);
               hid_fd=-1;
           }
       }
    }else if(!fatal){
       logline("IOS55 not found or tmd size invalid; no reload attempted");
    }
    if(reloaded){
       logline("RESTORING IOS58, no NAND install/change");
       display("Restoring IOS58 before USB FAT logging");
       s32 rc=IOS_ReloadIOS(IOS_RESTORE);
       logline("IOS_ReloadIOS(58) rc=%ld current=%u",(long)rc,(unsigned)IOS_GetVersion());
    }
    logline("RESULT v4 FOUND=%d raw_inputs=%lu IOS=%u",
          found_twin,(unsigned long)twin_reports,(unsigned)IOS_GetVersion());
    if(IOS_GetVersion()==IOS_RESTORE){
       display("Restored IOS58: saving USB log");
       save_log();
    }else snprintf(status_text,sizeof(status_text),
        "NOT IOS58: cannot guarantee FAT logging");
    logline("FINISH saved=%d; run fully automatic",log_saved);
    display("Finished - will exit to Homebrew Channel");
    for(int frames=0;frames<600;frames++)VIDEO_WaitVSync();
    return 0;
}
