/*
 * HID58 Probe: USB HID v5 diagnostic for ORIGINAL Wii IOS58.
 * Independent test application. Does not install WADs or write NAND.
 * Original code for this diagnostic, using libogc's supported USB v5 layer.
 */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <ogc/usb.h>
#include <ogc/ipc.h>
#include <ogc/usbstorage.h>
#include <fat.h>
#include <stdarg.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_MAX 64
#define LOG_CAP 131072

static volatile int g_exit_requested = 0;
static volatile int g_done = 0;
static volatile s32 g_read_result = 0;
static u8 *g_report = NULL;
static u8 g_last[REPORT_MAX];
static int g_last_length = 0;
static s32 g_device = -1;
static u8 g_ep = 0;
static u16 g_packet = 0;
static unsigned g_packets = 0;
static s32 g_usb_init = -999, g_usb_list = -999, g_open_result = -999;
static int g_count = 0;
static usb_device_entry g_entries[16];
static usb_device_entry g_all_entries[16];
static int g_all_count=0;
static s32 g_usb_all_list=-999;
static s32 g_hid_fd=-999, g_ven_fd=-999;
static s32 g_hid_v4=-999, g_hid_v5=-999, g_ven_v5=-999;
static u32 g_hid_ver=0, g_ven_ver=0;
static u32 g_probe_buffer[8] __attribute__((aligned(32)));
static char g_stage[120] = "Starting";
static char logbuf[LOG_CAP];
static unsigned loglen=0,frames=0,last_logged_reports=0;
static char save_state[80]="not saved";
static u64 t0;
static int last_count_hid=-1,last_count_all=-1;
static usb_device_entry last_hid_entries[16],last_all_entries[16];
static void record(const char *fmt,...) {
    if(loglen>LOG_CAP-420)return;
    char msg[360];
    va_list a; va_start(a,fmt);
    vsnprintf(msg,sizeof(msg),fmt,a);
    va_end(a);
    u32 ms=diff_msec(t0,gettime());
    int n=snprintf(logbuf+loglen,LOG_CAP-loglen,"[%8lu ms] %s\n",(unsigned long)ms,msg);
    if(n>0 && (unsigned)n<LOG_CAP-loglen)loglen+=(unsigned)n;
}
static void record_list(const char *name,usb_device_entry *list,int n,s32 status) {
    record("ENUM %s result=%ld count=%d",name,(long)status,n);
    for(int i=0;i<n && i<16;i++)
        record("ENUM %s[%d] vid=%04X pid=%04X id=%08lX token=%08lX",
          name,i,list[i].vid,list[i].pid,(unsigned long)list[i].device_id,
          (unsigned long)list[i].token);
}
static void save_log(void) {
    record("USER save log requested");
    snprintf(save_state,sizeof(save_state),"mounting USB FAT...");
    fatUnmount("usb:");
    if(!fatMountSimple("usb",&__io_usbstorage)) {
        snprintf(save_state,sizeof(save_state),"FAIL USB FAT mount: reinsert and retry PLUS");
        record("SAVE error: no FAT USB mount");
        return;
    }
    FILE *f=fopen("usb:/HID58_FULLLOG_V3.txt","wb");
    if(!f) {
        snprintf(save_state,sizeof(save_state),"FAIL opening log file");
        record("SAVE error: fopen failed");
        fatUnmount("usb:");
        return;
    }
    record("SAVE writing %u bytes",loglen);
    unsigned sz=loglen;
    size_t written=fwrite(logbuf,1,sz,f);
    int flush=fflush(f),close=fclose(f);
    fatUnmount("usb:");
    if(written==sz && flush==0 && close==0)
        snprintf(save_state,sizeof(save_state),"OK USB:/HID58_FULLLOG_V3.txt (%uB)",sz);
    else
        snprintf(save_state,sizeof(save_state),"FAIL USB write %u/%u f=%d c=%d",
                 (unsigned)written,sz,flush,close);
    record("SAVE result: %s",save_state);
}
static void on_power(void) { g_exit_requested = 1; }
static s32 on_read(s32 result, void *unused) {
    (void)unused;
    g_read_result = result;
    g_done = 1;
    return 0;
}
static void set_stage(const char *value) {
    strncpy(g_stage, value, sizeof(g_stage)-1);
    g_stage[sizeof(g_stage)-1] = '\0';
}
static void render(void) {
    printf("\x1b[2J\x1b[H");
    printf("HID58 FULLLOG V3 29OCT26 - NO NAND WRITES\n");
    printf("Running IOS %u rev %u\n", (unsigned)IOS_GetVersion(), (unsigned)IOS_GetRevision());
    printf("USB init=%ld HID=%ld/%d ALL=%ld/%d\n",
        (long)g_usb_init,(long)g_usb_list,g_count,(long)g_usb_all_list,g_all_count);
    printf("HID open=%ld v4=%ld v5=%ld ver=%08lX\n",
        (long)g_hid_fd,(long)g_hid_v4,(long)g_hid_v5,(unsigned long)g_hid_ver);
    printf("VEN open=%ld v5=%ld ver=%08lX\n",
        (long)g_ven_fd,(long)g_ven_v5,(unsigned long)g_ven_ver);
    printf("Stage: %s\n", g_stage);
    printf("HID interfaces: %d; all USB: %d\n", g_count,g_all_count);
    for (int i=0; i<g_count && i<4; i++) {
        printf("  %d: %04X:%04X id=%08lX\n", i,
               (unsigned)g_entries[i].vid, (unsigned)g_entries[i].pid,
               (unsigned long)g_entries[i].device_id);
    }
    for (int i=0;i<g_all_count && i<3;i++)
        printf(" ALL%d %04X:%04X id=%08lX\n",i,g_all_entries[i].vid,
               g_all_entries[i].pid,(unsigned long)g_all_entries[i].device_id);
    printf("USB_OpenDevice: %ld  fd=%ld  ep=%02X  pkt=%u\n",
           (long)g_open_result, (long)g_device, (unsigned)g_ep, (unsigned)g_packet);
    printf("Input reports: %u ; last status: %ld\n",
           g_packets, (long)g_read_result);
    if (g_last_length>0) {
        printf("Latest %d byte report:\n", g_last_length);
        for (int i=0; i<g_last_length && i<32; i++) {
            printf("%02X ", g_last[i]);
            if ((i%16)==15) printf("\n");
        }
        printf("\n");
    } else {
        printf("No USB report yet. Connect HID gamepad now.\n");
    }
    printf("\nTwin USB expected VID:PID = 0810:0001\n");
    printf("PLUS=SAVE LOG USB | 1=MARK | HOME=EXIT\n");
    printf("Save status: %.65s\n",save_state);
    printf("This is a raw diagnostic, NOT a gamepad mapper.\n");
    fflush(stdout);
}
static u8 select_ep(const usb_devdesc *dev, u16 *out_size) {
    if (!dev || !dev->configurations) return 0;
    for (u32 a=0; a<dev->bNumConfigurations; a++) {
        const usb_configurationdesc *cfg=&dev->configurations[a];
        if (!cfg->interfaces) continue;
        for (u32 b=0; b<cfg->bNumInterfaces; b++) {
            const usb_interfacedesc *it=&cfg->interfaces[b];
            if (!it->endpoints) continue;
            for (u32 c=0; c<it->bNumEndpoints; c++) {
                const usb_endpointdesc *ep=&it->endpoints[c];
                if ((ep->bEndpointAddress & 0x80) && ((ep->bmAttributes & 3)==3)) {
                    *out_size=ep->wMaxPacketSize & 0x7ff;
                    return ep->bEndpointAddress;
                }
            }
        }
    }
    return 0;
}
int main(void) {
    VIDEO_Init();
    GXRModeObj *rmode=VIDEO_GetPreferredMode(NULL);
    void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_Configure(rmode);
    console_init(fb,20,20,rmode->fbWidth-40,rmode->xfbHeight-40,rmode->fbWidth*2);
    VIDEO_ClearFrameBuffer(rmode,fb,COLOR_BLACK);
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    WPAD_Init();
    SYS_SetPowerCallback(on_power);
    t0=gettime();
    record("START HID58 FULLLOG V3 commit 29OCT26");
    record("IOS current=%u revision=%u",(unsigned)IOS_GetVersion(),(unsigned)IOS_GetRevision());
    record("expected USB HID Twin VID=0810 PID=0001");
    render();
    // MEM2 is mandatory for USB v5 DMA buffers, always 32-byte aligned.
    // Reserve 32-byte aligned DMA memory directly from Wii MEM2 arena.
    // Avoid dependence on newer libogc SYS_AllocArena2MemLo versions.
    u32 lo=(u32)SYS_GetArena2Lo();
    u32 aligned=(lo+31u)&~31u;
    if(aligned+REPORT_MAX<=(u32)SYS_GetArena2Hi()) {
        g_report=(u8*)aligned;
        SYS_SetArena2Lo((void*)(aligned+REPORT_MAX));
    }
    if (!g_report) { set_stage("Cannot allocate MEM2 buffer"); render(); goto idle; }
    memset(g_report,0,REPORT_MAX);
    set_stage("USB_Initialize (supports HID v5 via libogc)");
    render();
    // Probe IOS device nodes BEFORE libogc claims the USB v5 hosts.
    // USB_Initialize() returns 0 even if neither HID nor VEN opened!
    g_hid_fd=IOS_Open("/dev/usb/hid", IPC_OPEN_NONE);
    if(g_hid_fd>=0) {
        g_hid_v4=IOS_Ioctl(g_hid_fd,6,NULL,0,NULL,0);
        memset(g_probe_buffer,0,sizeof(g_probe_buffer));
        g_hid_v5=IOS_Ioctl(g_hid_fd,0,NULL,0,g_probe_buffer,32);
        g_hid_ver=g_probe_buffer[0];
        IOS_Close(g_hid_fd);
    }
    g_ven_fd=IOS_Open("/dev/usb/ven", IPC_OPEN_NONE);
    if(g_ven_fd>=0) {
        memset(g_probe_buffer,0,sizeof(g_probe_buffer));
        g_ven_v5=IOS_Ioctl(g_ven_fd,0,NULL,0,g_probe_buffer,32);
        g_ven_ver=g_probe_buffer[0];
        IOS_Close(g_ven_fd);
    }
    record("HID NODE open=%ld ioctl6(v4)=%ld ioctl0(v5)=%ld out=%08lX",
       (long)g_hid_fd,(long)g_hid_v4,(long)g_hid_v5,(unsigned long)g_hid_ver);
    record("VEN NODE open=%ld ioctl0(v5)=%ld out=%08lX",
       (long)g_ven_fd,(long)g_ven_v5,(unsigned long)g_ven_ver);
    g_usb_init=USB_Initialize();
    record("USB_Initialize returned %ld",(long)g_usb_init);
    if(g_usb_init<0) { set_stage("USB_Initialize failed"); render(); goto idle; }

    unsigned ticks=0;
    for (;;) {
        WPAD_ScanPads();
        u32 pressed=WPAD_ButtonsDown(0);
        if(pressed & WPAD_BUTTON_1) record("USER MARK: USB port/control changed here");
        if(pressed & WPAD_BUTTON_PLUS) save_log();
        if (g_exit_requested || (pressed & WPAD_BUTTON_HOME)) break;
        if (g_done) {
            g_done=0;
            if (g_read_result>=0) {
                // IOS returns number of transferred bytes, <= requested size.
                int n=g_read_result;
                if (n>REPORT_MAX) n=REPORT_MAX;
                if (n>0) {
                    memcpy(g_last,g_report,n);g_last_length=n;g_packets++;
                    if(g_packets==1 || g_packets-last_logged_reports>=20) {
                        char raw[3*REPORT_MAX+1]={0};
                        for(int j=0;j<n;j++)sprintf(raw+3*j,"%02X ",g_report[j]);
                        record("REPORT size=%d packet=%u data=%s",n,g_packets,raw);
                        last_logged_reports=g_packets;
                    }
                }
                set_stage("Receiving HID reports");
            } else {
                set_stage("Async HID read returned an error; retrying");
                record("READ COMPLETE ERROR rc=%ld",(long)g_read_result);
            }
        }
        if ((ticks++%45)==0) {
            u8 n=0;
            memset(g_entries,0,sizeof(g_entries));
            g_usb_list=USB_GetDeviceList(g_entries,16,USB_CLASS_HID,&n);
            g_count=(g_usb_list>=0)?n:0;
            n=0;
            memset(g_all_entries,0,sizeof(g_all_entries));
            g_usb_all_list=USB_GetDeviceList(g_all_entries,16,0,&n);
            g_all_count=(g_usb_all_list>=0)?n:0;
            if(last_count_hid!=g_count || memcmp(last_hid_entries,g_entries,sizeof(g_entries))!=0){
                record_list("HID",g_entries,g_count,g_usb_list);
                memcpy(last_hid_entries,g_entries,sizeof(g_entries));
                last_count_hid=g_count;
            }
            if(last_count_all!=g_all_count || memcmp(last_all_entries,g_all_entries,sizeof(g_all_entries))!=0){
                record_list("ALL",g_all_entries,g_all_count,g_usb_all_list);
                memcpy(last_all_entries,g_all_entries,sizeof(g_all_entries));
                last_count_all=g_all_count;
            }
            if((++frames%20)==0)record("HEARTBEAT HID=%d ALL=%d reports=%u",g_count,g_all_count,g_packets);
            if (g_device<0 && g_count>0) {
                int index=0;
                for (int i=0;i<g_count;i++) {
                    if(g_entries[i].vid==0x0810 && g_entries[i].pid==0x0001) {index=i;break;}
                }
                set_stage("Opening HID via libogc USB v5");
                render();
                record("OPEN request vid=%04X pid=%04X id=%08lX",g_entries[index].vid,g_entries[index].pid,
                    (unsigned long)g_entries[index].device_id);
                g_open_result=USB_OpenDevice(g_entries[index].device_id,
                          g_entries[index].vid,g_entries[index].pid,&g_device);
                record("OPEN result=%ld fd=%ld",(long)g_open_result,(long)g_device);
                if (g_open_result>=0 && g_device>=0) {
                    usb_devdesc dev;
                    set_stage("Reading descriptors / endpoint");
                    render();
                    s32 d=USB_GetDescriptors(g_device,&dev);
                    record("USB_GetDescriptors rc=%ld",(long)d);
                    if(d>=0) {
                        record("DESCRIPTOR vendor=%04X product=%04X class=%u configurations=%u",
                             dev.idVendor,dev.idProduct,dev.bDeviceClass,dev.bNumConfigurations);
                        g_ep=select_ep(&dev,&g_packet);
                        record("ENDPOINT selected=%02X packet=%u",g_ep,(unsigned)g_packet);
                        USB_FreeDescriptors(&dev);
                    }
                    if(!g_ep) set_stage("No interrupt-IN endpoint found");
                    else {
                        if(g_packet<1) g_packet=8;
                        if(g_packet>REPORT_MAX) g_packet=REPORT_MAX;
                        set_stage("Ready: reading interrupt-IN reports");
                    }
                } else {
                    set_stage("USB_OpenDevice failed; check VID:PID");
                    g_device=-1;
                }
            } else if(g_count==0 && g_device<0) {
                set_stage("No HID gamepad detected; rescan every 0.75s");
            }
            render();
        }
        if (g_device>=0 && g_ep && !g_done && g_read_result!=-8888) {
            // Mark request as pending without blocking the UI.
            s32 ret=USB_ReadIntrMsgAsync(g_device,g_ep,g_packet,g_report,on_read,NULL);
            if(ret>=0)g_read_result=-8888;
            else { g_read_result=ret;set_stage("USB_ReadIntrMsgAsync submit error");
                   record("USB_ReadIntrMsgAsync submit failed rc=%ld",(long)ret);}
        }
        VIDEO_WaitVSync();
        if ((ticks%30)==0) render();
    }
    record("EXIT user chose HOME/power; save first or RAM log is lost");
    if (g_exit_requested) SYS_ResetSystem(SYS_POWEROFF,0,0);
    return 0;
idle:
    while(!g_exit_requested) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0)&WPAD_BUTTON_HOME) break;
        VIDEO_WaitVSync();
    }
    if (g_exit_requested) SYS_ResetSystem(SYS_POWEROFF,0,0);
    return 0;
}
