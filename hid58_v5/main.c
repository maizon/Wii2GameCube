/*
 Maizon USB/HID V5 - Wii real-hardware diagnostic.
 READ ONLY: never writes IOS/NAND. Automatic FAT-USB logs + optional UDP.
 USB v5 used via libogc. IOS/USB services are probed before/after startup.
 Async reads use one permanent MEM2 buffer: never freed while pending.
 */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <ogc/ipc.h>
#include <ogc/usb.h>
#include <ogc/usbstorage.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>
#include <network.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LIMIT (256*1024)
#define MAX_DEVS 16
#define REPORT_BYTES 64
#define AUTOSAVE_MS 5000
#define RESCAN_FRAMES 40
#define UDP_PORT 25058

static char logdata[LOG_LIMIT];
static unsigned logsz=0, overwritten=0;
static u64 start_time;
static unsigned frame=0, read_count=0, change_count=0;
static char phase[100]="BOOT", save_status[96]="RAM only";
static int dirty=1, mounted=0, save_requested=0;
static unsigned last_save_ms=0, last_mount_ms=0;
static char p_hid[] __attribute__((aligned(32)))="/dev/usb/hid";
static char p_ven[] __attribute__((aligned(32)))="/dev/usb/ven";
static char p_oh0[] __attribute__((aligned(32)))="/dev/usb/oh0";
static char p_oh1[] __attribute__((aligned(32)))="/dev/usb/oh1";
static u32 versionbuf[8] __attribute__((aligned(32)));
static u8 *readbuf=NULL;
static volatile int read_finished=0;
static volatile s32 read_ret=0;
static int read_pending=0, orphaned=0;
static s32 open_ret=-999, device_fd=-1, usb_ret=-999;
static u8 endpoint=0; static u16 pktlen=0;
static usb_device_entry hid_devs[MAX_DEVS],all_devs[MAX_DEVS],old_hid[MAX_DEVS],old_all[MAX_DEVS];
static int num_hid=0,num_all=0,old_hid_count=-1,old_all_count=-1;
static s32 rc_hid=-999,rc_all=-999;
static unsigned last_report_len=0;
static u8 last_report[REPORT_BYTES];
static volatile int shutdown_flag=0;
static volatile int net_result=-999;
static int net_started=0,net_fd=-1,net_connected=0;
static struct sockaddr_in broadcast_addr;

static unsigned clockms(void) { return diff_msec(start_time,gettime()); }
static void power_cb(void){ shutdown_flag=1; }
static s32 udp_done(s32 result,void *unused){(void)unused;net_result=result;return 0;}
static s32 read_done(s32 result,void *unused){
    (void)unused;
    read_ret=result;
    read_finished=1;
    return 0;
}
static void broadcast_line(const char *line,int length){
    if(!net_connected||net_fd<0||length<=0)return;
    if(length>350)length=350;
    // Datagram, no ACK or retransmission. Keep logs in RAM/USB as fallback.
    net_sendto(net_fd,line,length,0,(struct sockaddr*)&broadcast_addr,sizeof(broadcast_addr));
}
static void logmsg(const char *fmt,...){
    char body[420],line[470];
    va_list args;
    va_start(args,fmt);vsnprintf(body,sizeof(body),fmt,args);va_end(args);
    int n=snprintf(line,sizeof(line),"[%08u] %s\n",clockms(),body);
    if(n<=0)return;
    if((unsigned)n>=sizeof(line))n=sizeof(line)-1;
    if(logsz+n+1>=LOG_LIMIT){
        // Keep most recent ~128K rather than silently lose final cause.
        unsigned discard=LOG_LIMIT/2;
        memmove(logdata,logdata+discard,logsz-discard);
        logsz-=discard;
        overwritten++;
    }
    memcpy(logdata+logsz,line,n);
    logsz+=n;logdata[logsz]=0;dirty=1;
    broadcast_line(line,n);
}
static void setphase(const char *p){
    if(strcmp(phase,p)){
        snprintf(phase,sizeof(phase),"%s",p);
        logmsg("PHASE %s",phase);
    }
}
static void log_devlist(const char *which,usb_device_entry *items,int n,s32 rc){
    logmsg("USB_LIST %s rc=%ld count=%d",which,(long)rc,n);
    for(int i=0;i<n && i<MAX_DEVS;i++)
      logmsg("USB_DEVICE %s[%d] VID=%04X PID=%04X dev_id=%08lX token=%08lX",
             which,i,items[i].vid,items[i].pid,
             (unsigned long)items[i].device_id,(unsigned long)items[i].token);
}
static void render(void){
    printf("\x1b[2J\x1b[H");
    printf("MAIZON USB HID V5 - FULL DIAGNOSTIC\n");
    printf("IOS %u rev %u | %u s | Log %u bytes | resets %u\n",
      (unsigned)IOS_GetVersion(),(unsigned)IOS_GetRevision(),
      clockms()/1000,logsz,overwritten);
    printf("Phase: %.65s\n",phase);
    printf("USB init %ld; HID list %ld/%d; ALL list %ld/%d\n",
       (long)usb_ret,(long)rc_hid,num_hid,(long)rc_all,num_all);
    printf("HID reports %u; device=%ld ep=%02X size=%u\n",
      read_count,(long)device_fd,endpoint,(unsigned)pktlen);
    for(int i=0;i<num_hid && i<3;i++)
      printf("HID%d %04X:%04X id=%08lX\n",i,
         hid_devs[i].vid,hid_devs[i].pid,(unsigned long)hid_devs[i].device_id);
    for(int i=0;i<num_all && i<3;i++)
      printf("ALL%d %04X:%04X id=%08lX\n",i,
         all_devs[i].vid,all_devs[i].pid,(unsigned long)all_devs[i].device_id);
    if(last_report_len){
      printf("RAW: ");
      for(unsigned i=0;i<last_report_len && i<16;i++)printf("%02X ",last_report[i]);
      printf("\n");
    }
    printf("Log: %.69s\n",save_status);
    printf("WiFi: %s | UDP port %u\n",net_connected?"ON":(net_started?"CONNECTING":"OFF"),UDP_PORT);
    printf("Buttons: A=save PLUS=retry FAT 1=mark\n");
    printf("2=enable WiFi UDP HOME=exit (no NAND writes)\n");
    fflush(stdout);
}
static void probe_one(const char *name,char *path){
    s32 fd=IOS_Open(path,IPC_OPEN_NONE);
    if(fd<0){
        logmsg("IOS_NODE %s open=%ld",name,(long)fd);
        return;
    }
    if(!strcmp(name,"HID") || !strcmp(name,"VEN")){
        // IOCTL 6 is NOT a version query on the VEN/USBv5 controller.
        // Follow libogc's safe HID version probing; VEN: GETVERSION=0 only.
        memset(versionbuf,0,sizeof(versionbuf));
        s32 v4=-999;
        if(!strcmp(name,"HID"))
            v4=IOS_Ioctl(fd,6,NULL,0,NULL,0);
        s32 v5=-999;
        if(v4!=0x40001)
            v5=IOS_Ioctl(fd,0,NULL,0,versionbuf,sizeof(versionbuf));
        logmsg("IOS_NODE %s open=%ld v4_ioctl6=%ld v5_ioctl0=%ld version_out=%08lX",
           name,(long)fd,(long)v4,(long)v5,(unsigned long)versionbuf[0]);
    } else {
        // OH0/OH1 have different ioctl numbers, so only probe IOS_Open.
        logmsg("IOS_NODE %s open=%ld (no unsupported ioctls)",name,(long)fd);
    }
    IOS_Close(fd);
}
static void probe_nodes(const char *phase_name){
    logmsg("NODE_PROBE BEGIN %s",phase_name);
    probe_one("HID",p_hid);probe_one("VEN",p_ven);
    probe_one("OH0",p_oh0);probe_one("OH1",p_oh1);
    logmsg("NODE_PROBE END %s",phase_name);
}
static int save_usb(int try_mount){
    unsigned now=clockms();
    if(!mounted){
        if(!try_mount && now-last_mount_ms<10000)return 0;
        last_mount_ms=now;
        // Never format any disk. Mount only existing FAT filesystem.
        if(!fatMountSimple("usb",&__io_usbstorage)){
            snprintf(save_status,sizeof(save_status),"NO FAT USB; RAM + optional WiFi");
            return 0;
        }
        mounted=1;
        mkdir("usb:/apps",0777);
        mkdir("usb:/apps/MAIZON_USB_V5",0777);
        mkdir("usb:/apps/MAIZON_USB_V5/logs",0777);
        logmsg("USB FAT MOUNT OK /apps/MAIZON_USB_V5/logs");
    }
    FILE *f=fopen("usb:/apps/MAIZON_USB_V5/logs/USB_DIAG_V5.txt","wb");
    if(!f){
        mounted=0;
        fatUnmount("usb:");
        snprintf(save_status,sizeof(save_status),"FAT write failed; RAM retained");
        return 0;
    }
    unsigned count=logsz;
    size_t wrote=fwrite(logdata,1,count,f);
    int flush=fflush(f),closed=fclose(f);
    if(wrote==count && flush==0 && closed==0){
        snprintf(save_status,sizeof(save_status),"SAVED usb:/apps/MAIZON_USB_V5/logs");
        last_save_ms=now;dirty=0;save_requested=0;
        return 1;
    }
    snprintf(save_status,sizeof(save_status),"FAT save ERROR; RAM retained");
    mounted=0;fatUnmount("usb:");
    return 0;
}
static void enable_wifi(void){
    if(net_started)return;
    net_started=1;
    logmsg("NETWORK user enabled; USB baseline recorded BEFORE network");
    net_result=-999;
    int r=net_init_async(udp_done,NULL);
    logmsg("NET net_init_async returned %d",r);
    if(r<0)net_started=0;
}
static void poll_wifi(void){
    if(!net_started||net_connected||net_result==-999)return;
    if(net_result<0){
       logmsg("NET initialization failed result=%ld",(long)net_result);
       net_started=0;net_result=-999;return;
    }
    net_fd=net_socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    logmsg("NET UDP socket fd=%d",net_fd);
    if(net_fd<0){net_started=0;return;}
    int yes=1;
    int opt=net_setsockopt(net_fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
    memset(&broadcast_addr,0,sizeof(broadcast_addr));
    broadcast_addr.sin_family=AF_INET;
    broadcast_addr.sin_port=htons(UDP_PORT);
    broadcast_addr.sin_addr.s_addr=inet_addr("255.255.255.255");
    net_connected=1;
    logmsg("NET active ip=%08lX broadcast_setsockopt=%d port=%d",
      (unsigned long)net_gethostip(),opt,UDP_PORT);
}
static u8 find_endpoint(const usb_devdesc *d,u16 *len){
    if(!d || !d->configurations)return 0;
    for(unsigned c=0;c<d->bNumConfigurations;c++){
        usb_configurationdesc *cfg=&d->configurations[c];
        logmsg("DESC CONFIG[%u] interfaces=%u",c,cfg->bNumInterfaces);
        for(unsigned i=0;cfg->interfaces && i<cfg->bNumInterfaces;i++){
            usb_interfacedesc *itf=&cfg->interfaces[i];
            logmsg("DESC IF[%u] number=%u alt=%u class=%02X sub=%02X proto=%02X eps=%u",
               i,itf->bInterfaceNumber,itf->bAlternateSetting,
               itf->bInterfaceClass,itf->bInterfaceSubClass,
               itf->bInterfaceProtocol,itf->bNumEndpoints);
            for(unsigned k=0;itf->endpoints && k<itf->bNumEndpoints;k++){
                usb_endpointdesc *ep=&itf->endpoints[k];
                logmsg("DESC ENDPOINT %02X attr=%02X size=%u interval=%u",
                   ep->bEndpointAddress,ep->bmAttributes,
                   (unsigned)ep->wMaxPacketSize,ep->bInterval);
                if((ep->bEndpointAddress&0x80) && ((ep->bmAttributes&3)==3)){
                    *len=ep->wMaxPacketSize&0x7ff;
                    return ep->bEndpointAddress;
                }
            }
        }
    }
    return 0;
}
static void attempt_open(usb_device_entry *e,int from_hid_list){
    if(read_pending || orphaned)return;
    logmsg("DEVICE_OPEN attempt vid=%04X pid=%04X id=%08lX HID=%d",
       e->vid,e->pid,(unsigned long)e->device_id,from_hid_list);
    setphase("Opening USB device & descriptors");
    open_ret=USB_OpenDevice(e->device_id,e->vid,e->pid,&device_fd);
    logmsg("DEVICE_OPEN rc=%ld fd=%ld",(long)open_ret,(long)device_fd);
    if(open_ret<0 || device_fd<0){device_fd=-1;return;}
    usb_devdesc desc;
    memset(&desc,0,sizeof(desc));
    s32 r=USB_GetDescriptors(device_fd,&desc);
    logmsg("DESCRIPTOR rc=%ld",(long)r);
    if(r>=0){
        logmsg("DESC VID=%04X PID=%04X USB=%04X CLASS=%02X CONFS=%u",
         desc.idVendor,desc.idProduct,desc.bcdUSB,
         desc.bDeviceClass,desc.bNumConfigurations);
        endpoint=find_endpoint(&desc,&pktlen);
        USB_FreeDescriptors(&desc);
    }
    if(!endpoint){
        logmsg("DEVICE no interrupt-IN endpoint");
        USB_CloseDevice(&device_fd);
        device_fd=-1;
    }else{
        if(pktlen<1)pktlen=8;
        if(pktlen>REPORT_BYTES)pktlen=REPORT_BYTES;
        setphase("Reading HID input data asynchronously");
    }
}
static void enumerate(void){
    u8 n=0;
    memset(hid_devs,0,sizeof(hid_devs));
    rc_hid=USB_GetDeviceList(hid_devs,MAX_DEVS,USB_CLASS_HID,&n);
    num_hid=(rc_hid>=0)?n:0;
    n=0;
    memset(all_devs,0,sizeof(all_devs));
    rc_all=USB_GetDeviceList(all_devs,MAX_DEVS,0,&n);
    num_all=(rc_all>=0)?n:0;
    if(num_hid!=old_hid_count || memcmp(hid_devs,old_hid,sizeof(old_hid))!=0){
        change_count++;log_devlist("HID",hid_devs,num_hid,rc_hid);
        memcpy(old_hid,hid_devs,sizeof(old_hid));old_hid_count=num_hid;save_requested=1;
    }
    if(num_all!=old_all_count || memcmp(all_devs,old_all,sizeof(old_all))!=0){
        change_count++;log_devlist("ALL",all_devs,num_all,rc_all);
        memcpy(old_all,all_devs,sizeof(old_all));old_all_count=num_all;save_requested=1;
    }
    // If Twin is visible in ALL (but not HID), test its descriptors too.
    int selected=-1;
    usb_device_entry *arr=hid_devs;
    int nh=num_hid;
    for(int i=0;i<num_hid;i++){
        if(hid_devs[i].vid==0x0810 && hid_devs[i].pid==0x0001){selected=i;break;}
    }
    if(selected<0)for(int i=0;i<num_all;i++){
        if(all_devs[i].vid==0x0810 && all_devs[i].pid==0x0001){
            arr=all_devs;nh=num_all;selected=i;break;
        }
    }
    if(selected<0 && num_hid>0)selected=0;
    if(selected>=0 && selected<nh && device_fd<0 && !orphaned)
        attempt_open(&arr[selected],arr==hid_devs);
    if(device_fd<0 && selected<0)setphase("No HID gamepad present; scan every 0.7s");
}
static void handle_reports(void){
    if(read_finished){
        read_finished=0;read_pending=0;
        if(orphaned){logmsg("READ returned after detach rc=%ld",(long)read_ret);return;}
        int n=read_ret;
        if(n>REPORT_BYTES)n=REPORT_BYTES;
        if(n>0){
            read_count++;
            if(last_report_len!=(unsigned)n || memcmp(last_report,readbuf,n)!=0 || read_count%120==0){
                char hex[REPORT_BYTES*3+1]={0};
                for(int i=0;i<n;i++)sprintf(hex+i*3,"%02X ",readbuf[i]);
                logmsg("REPORT seq=%u len=%d hex=%s",read_count,n,hex);
                memcpy(last_report,readbuf,n);last_report_len=n;
            }
        }else if(n<0){
            logmsg("USB_READ callback error=%ld",(long)read_ret);
        }
    }
    if(device_fd>=0 && endpoint && !read_pending && !orphaned){
        s32 r=USB_ReadIntrMsgAsync(device_fd,endpoint,pktlen,readbuf,read_done,NULL);
        if(r>=0)read_pending=1;
        else{logmsg("USB_READ submit error=%ld",(long)r);setphase("USB read failed");}
    }
}
int main(void){
    VIDEO_Init();
    GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
    void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);
    console_init(fb,20,20,mode->fbWidth-40,mode->xfbHeight-40,mode->fbWidth*2);
    VIDEO_ClearFrameBuffer(mode,fb,COLOR_BLACK);
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    start_time=gettime();
    logmsg("MAIZON USB-HID V5 start IOS=%u REV=%u",
       (unsigned)IOS_GetVersion(),(unsigned)IOS_GetRevision());
    logmsg("BUILD 2026-10-29 | USB read only; no NAND/IOS writes");
    logmsg("EXPECTED Twin VID=0810 PID=0001; run with other HID reference");
    setphase("Probing USB IOS before initializing libogc USB/WPAD");
    probe_nodes("BEFORE USB_Init / WPAD_Init");
    usb_ret=USB_Initialize();
    logmsg("USB_Initialize rc=%ld",(long)usb_ret);
    probe_nodes("AFTER USB_Init BEFORE WPAD_Init");
    WPAD_Init();
    SYS_SetPowerCallback(power_cb);
    probe_nodes("AFTER WPAD_Init");
    // MEM2 DMA buffer, 32-byte alignment. Keep until process exits.
    u32 lo=(u32)SYS_GetArena2Lo();
    u32 aligned=(lo+31u)&~31u;
    if(aligned+REPORT_BYTES<=(u32)SYS_GetArena2Hi()){
        readbuf=(u8*)aligned;
        SYS_SetArena2Lo((void*)(aligned+REPORT_BYTES));
        memset(readbuf,0,REPORT_BYTES);
        logmsg("DMA buffer MEM2 at %08lX",(unsigned long)aligned);
    } else { logmsg("ERROR no DMA MEM2");setphase("Cannot allocate MEM2");}
    if(usb_ret>=0)enumerate();
    logmsg("BASELINE completed; auto FAT USB every 5s, network OFF");
    render();
    while(!shutdown_flag){
        WPAD_ScanPads();
        u32 buttons=WPAD_ButtonsDown(0);
        if(buttons&WPAD_BUTTON_HOME)break;
        if(buttons&WPAD_BUTTON_1){
            logmsg("USER MARK unplug/plug/port switch at %u ms",clockms());
            probe_nodes("USER MARK (may report already-open services)");
            save_requested=1;
        }
        if(buttons&WPAD_BUTTON_2)enable_wifi();
        if(buttons&WPAD_BUTTON_PLUS){
            mounted=0;
            fatUnmount("usb:");
            last_mount_ms=0;
            logmsg("USER forced FAT remount");
            save_requested=1;
        }
        if(buttons&WPAD_BUTTON_A){logmsg("USER save requested");save_requested=1;}
        if(frame%RESCAN_FRAMES==0 && usb_ret>=0)enumerate();
        if(readbuf)handle_reports();
        poll_wifi();
        unsigned now=clockms();
        if((save_requested || (dirty && now-last_save_ms>=AUTOSAVE_MS)) && now-last_mount_ms>=1000){
            // If no USB is available, keep scanning and keep the log in RAM.
            save_usb(0);
        }
        if(frame%60==0)render();
        if(frame%600==0)logmsg("HEARTBEAT USB=%ld HID=%d ALL=%d reports=%u wifi=%d mount=%d",
            (long)usb_ret,num_hid,num_all,read_count,net_connected,mounted);
        frame++;
        VIDEO_WaitVSync();
    }
    logmsg("EXIT requested; saving if USB FAT still present");
    save_usb(1);
    // Do not free async USB read buffers when an IOS request may be pending.
    if(net_fd>=0)net_close(net_fd);
    if(shutdown_flag)SYS_ResetSystem(SYS_POWEROFF,0,0);
    return 0;
}
