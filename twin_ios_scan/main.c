// MAIZON TWIN IOS SCAN v1.3 - IOS55 GETRHPORTSTATUS + HID v4 inquiry (read-only)
// Para cada IOS candidato (padrao: so 55; compile com -DSCAN_EXTRA para 37/36):
//   recarrega o IOS, sonda os nos /dev/usb/*, faz USB_Initialize (libogc),
//   lista TODOS os dispositivos por ~20 s, abre dispositivos nao-mass-storage, analisa descritores.
//   Captura USB assincrona opcional (-DSCAN_REPORTS), desativada por seguranca no primeiro teste.
// Depois restaura IOS58, monta o pendrive e salva o log. Sem SD, sem WPAD,
// sem escrita na NAND, sem instalar IOS. Totalmente automatico.
//
// Log final: usb:/apps/MAIZON_TWIN_SCAN/log.txt; caso o USB nao monte, leia a tela.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/stat.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/es.h>
#include <ogc/usb.h>
#include <ogc/usbstorage.h>
#include <fat.h>
#include "deep_probe.h"
#include "recovery_journal.h"

#ifndef TITLE_ID
#define TITLE_ID(x,y) (((u64)(x) << 32) | (y))
#endif

#define SCAN_MS 15000
#define BASELINE_MS 7000
#define LEGACY_CLASSES 3
#define MAXD 32
#define MAXT 3

// ------------------------------------------------------------- log
#define LOGSZ (192*1024)
static char logbuf[LOGSZ];
static size_t loglen = 0;
static u32 ms(void) { return ticks_to_millisecs(gettime()); }

static void lg(const char *fmt, ...)
{
    char line[256], out[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    int n = snprintf(out, sizeof out, "[%7u] %s\n", (unsigned)ms(), line);
    if (loglen + n < LOGSZ) { memcpy(logbuf + loglen, out, n); loglen += n; }
    printf("%s", out);
}

static int save_to(const char *dev, const char *dir1, const char *dir2, const char *file)
{
    char p[96];
    snprintf(p, sizeof p, "%s:/apps", dev);       mkdir(p, 0777);
    snprintf(p, sizeof p, "%s:/apps/%s", dev, dir1); mkdir(p, 0777);
    (void)dir2;
    snprintf(p, sizeof p, "%s:/apps/%s/%s", dev, dir1, file);
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    size_t written = fwrite(logbuf, 1, loglen, f);
    int rc = fclose(f);
    return (written == loglen && rc == 0) ? 0 : -2;
}

// ------------------------------------------------------- IOS helpers
// 1 = ok, 0 = ausente, -1 = stub
static int ios_state(u32 n, u32 *rev)
{
    u32 sz = 0;
    s32 r = ES_GetStoredTMDSize(TITLE_ID(1, n), &sz);
    if (r < 0 || sz == 0) return 0;
    void *buf = memalign(32, (sz + 31) & ~31);
    if (!buf) return 0;
    r = ES_GetStoredTMD(TITLE_ID(1, n), (signed_blob *)buf, sz);
    if (r < 0) { free(buf); return 0; }
    tmd *t = (tmd *)SIGNATURE_PAYLOAD((signed_blob *)buf);
    *rev = t->title_version;
    free(buf);
    return (*rev >= 65280) ? -1 : 1;
}

static char path_buf[64] ATTRIBUTE_ALIGN(32);

static void probe_nodes(const char *tag)
{
    static const char *list[] = {
        "/dev/usb/hid", "/dev/usb/ven", "/dev/usb/oh0", "/dev/usb/oh1",
        "/dev/usb2", "/dev/usb123", NULL
    };
    lg("--- NODES %s (IOS%d rev %d) ---", tag, IOS_GetVersion(), IOS_GetRevision());
    for (int i = 0; list[i]; i++) {
        strcpy(path_buf, list[i]);
        s32 fd = IOS_Open(path_buf, 0);
        lg("  open %-14s = %d", list[i], (int)fd);
        if (fd >= 0) IOS_Close(fd);
    }
}

// --------------------------------------------------- device tracking
static usb_device_entry cur[MAXD], prv[MAXD], hidl[MAXD], stor[MAXD];
static int ncur = 0, nprv = 0;
static s32 e_all = -99999, e_hid = -99999, e_storage_global = -99999;
static int max_detected_58 = 0, max_detected_55 = 0, seen_twin_all = 0, twin_seen_oh0 = 0;
static int direct_counts[2][LEGACY_CLASSES] = {{-999, -999, -999}, {-999, -999, -999}};
static s32 direct_rc[2][LEGACY_CLASSES] = {{-999, -999, -999}, {-999, -999, -999}};
static int direct_seen[2][LEGACY_CLASSES];
static int legacy_heap = -1;

static int get_list(usb_device_entry *b, u8 cls, s32 *le)
{
    u8 n = 0;
    memset(b, 0, sizeof(usb_device_entry) * MAXD);
    s32 r = USB_GetDeviceList(b, MAXD, cls, &n);
    if (r < 0) {
        if (r != *le) lg("  GetDeviceList(class %d) ERRO %d", cls, (int)r);
        *le = r;
        return -1;
    }
    *le = 0;
    return n;
}

static int same(const usb_device_entry *a, const usb_device_entry *b)
{
    return a->device_id == b->device_id && a->vid == b->vid && a->pid == b->pid;
}
static int in_list(const usb_device_entry *e, const usb_device_entry *l, int n)
{
    for (int i = 0; i < n; i++) if (same(e, &l[i])) return 1;
    return 0;
}

typedef struct {
    int used, dead, retired, naks, errs, nrep;
    s32 devid, fd;
    u16 vid, pid;
    u8 ep, eplen;
    u8 *buf;
    u8 last[16];
    int lastn;
    u32 next_ms;
    volatile int pending;
    volatile s32 res;
} Trk;
static Trk trk[MAXT];
static u8 trkbuf[MAXT][64] ATTRIBUTE_ALIGN(32);   // DMA: nunca liberado

static s32 cb_read(s32 r, void *u)
{
    Trk *t = (Trk *)u;
    t->res = r;
    t->pending = 2;
    return 0;
}

static void issue_read(Trk *t)
{
    memset(t->buf, 0, 64);
    t->pending = 1;
    s32 r = USB_ReadIntrMsgAsync(t->fd, t->ep, t->eplen, t->buf, cb_read, t);
    if (r < 0) {
        lg("  ReadIntrMsgAsync(%04X:%04X) ERRO %d", t->vid, t->pid, (int)r);
        t->pending = 0;
        t->dead = 1;
    }
}

static void close_trk(Trk *t)
{
    if (t->pending == 1) return;         // leitura pendente: nao mexe
    if (t->fd >= 0) USB_CloseDevice(&t->fd);
    t->used = 0;
}

static void open_dev(const usb_device_entry *e)
{
    lg(">> OPEN %04X:%04X devid=%d", e->vid, e->pid, (int)e->device_id);
    s32 fd = -1;
    s32 r = USB_OpenDevice(e->device_id, e->vid, e->pid, &fd);
    lg("   USB_OpenDevice r=%d fd=%d", (int)r, (int)fd);
    if (r < 0) return;

    usb_devdesc d;
    memset(&d, 0, sizeof d);
    r = USB_GetDescriptors(fd, &d);
    lg("   USB_GetDescriptors r=%d", (int)r);
    if (r < 0) { USB_CloseDevice(&fd); return; }
    lg("   bcdUSB=%04X class=%02X maxpkt0=%d nconf=%d", d.bcdUSB, d.bDeviceClass,
       d.bMaxPacketSize0, d.bNumConfigurations);
    if (d.bNumConfigurations > 1) lg("   AVISO: multiplas configuracoes (%u); revisar valores abaixo", d.bNumConfigurations);

    u8 ep = 0, eplen = 0;
    for (int c = 0; d.configurations && c < d.bNumConfigurations; c++) {
        usb_configurationdesc *cf = &d.configurations[c];
        lg("   CONFIG idx=%d value=%u interfaces=%u power=%umA", c,
           cf->bConfigurationValue, cf->bNumInterfaces, (unsigned)cf->bMaxPower * 2);
        for (int i = 0; cf->interfaces && i < cf->bNumInterfaces; i++) {
            usb_interfacedesc *itf = &cf->interfaces[i];
            lg("   IF%d alt%d class=%02X/%02X/%02X nep=%d", itf->bInterfaceNumber,
               itf->bAlternateSetting, itf->bInterfaceClass, itf->bInterfaceSubClass,
               itf->bInterfaceProtocol, itf->bNumEndpoints);
            for (int k = 0; itf->endpoints && k < itf->bNumEndpoints; k++) {
                usb_endpointdesc *en = &itf->endpoints[k];
                lg("     EP %02X attr=%02X maxpkt=%d interval=%d", en->bEndpointAddress,
                   en->bmAttributes, en->wMaxPacketSize, en->bInterval);
                if (!ep && (en->bEndpointAddress & 0x80) && (en->bmAttributes & 3) == 3) {
                    ep = en->bEndpointAddress;
                    eplen = en->wMaxPacketSize > 64 ? 64 : en->wMaxPacketSize;
                }
            }
        }
    }
    USB_FreeDescriptors(&d);
    if (!ep) { lg("   sem EP interrupt-IN"); USB_CloseDevice(&fd); return; }
#ifndef SCAN_REPORTS
    // Primeira passada exclusivamente diagnostica: nenhuma transferência IN fica pendente.
    lg("   EP interrupt-IN %02X (%u B); captura desativada nesta build segura", ep, eplen);
    USB_CloseDevice(&fd);
    return;
#else
    for (int i = 0; i < MAXT; i++) {
        Trk *t = &trk[i];
        if (t->used || t->retired) continue;
        memset(t, 0, sizeof *t);
        t->used = 1; t->fd = fd; t->devid = e->device_id;
        t->vid = e->vid; t->pid = e->pid; t->ep = ep; t->eplen = eplen;
        t->buf = trkbuf[i];
        lg("   lendo EP %02X (%d bytes)", ep, eplen);
        issue_read(t);
        return;
    }
    USB_CloseDevice(&fd);
#endif
}

static void service_trk(u32 now)
{
    for (int i = 0; i < MAXT; i++) {
        Trk *t = &trk[i];
        if (!t->used) continue;
        if (t->pending == 2) {
            s32 r = t->res;
            t->pending = 0;
            if (r > 0) {
                int n = r > 16 ? 16 : r;
                if (n != t->lastn || memcmp(t->buf, t->last, n)) {
                    char hex[64] = {0};
                    for (int k = 0; k < n; k++) sprintf(hex + k * 3, "%02X ", t->buf[k]);
                    lg("  REPORT %04X:%04X: %s", t->vid, t->pid, hex);
                    memcpy(t->last, t->buf, n);
                    t->lastn = n;
                    t->nrep++;
                }
                t->next_ms = now;
            } else if (r == -7005 || r == 0) {      // NAK / vazio: espera e tenta
                t->naks++;
                if (t->naks == 1 || t->naks % 200 == 0)
                    lg("  %04X:%04X NAK/vazio x%d", t->vid, t->pid, t->naks);
                t->next_ms = now + 50;
            } else {
                if (++t->errs <= 3) lg("  READ erro %d (%04X:%04X)", (int)r, t->vid, t->pid);
                if (t->errs >= 3) t->dead = 1;
                t->next_ms = now + 200;
            }
        }
        if (!t->dead && t->pending == 0 && now >= t->next_ms && t->nrep < 300)
            issue_read(t);
        if (t->dead) close_trk(t);
    }
}

static void close_all(void)
{
    u32 t0 = ms();
    for (int i = 0; i < MAXT; i++) {
        trk[i].dead = 1;
        while (trk[i].used && trk[i].pending == 1 && ms() - t0 < 1500) usleep(10000);
        if (trk[i].used && trk[i].pending == 1) {
            // ainda pendente: fecha o device (cancela a transferencia) e
            // aposenta o slot; o buffer e estatico, nada e liberado
            lg("  slot %d ainda pendente; aposentando sem liberar DMA", i);
            if (trk[i].fd >= 0) USB_CloseDevice(&trk[i].fd);
            trk[i].retired = 1;
        } else if (trk[i].used) close_trk(&trk[i]);
    }
}

// --------------------------------------------------- Targeted IOS55 service checks
// IOS55 foi visto retornando HID=-6 em outro teste e HID=3 no log v1.2.
// Abertura por si so nao significa HID v4: ioctl 6 deve retornar 0x40001.
static void probe_legacy_hid_version(void)
{
    static char n[] ATTRIBUTE_ALIGN(32) = "/dev/usb/hid";
    s32 fd=IOS_Open(n,0);
    lg("IOS55 HID version: OPEN=%d",(int)fd);
    if (fd<0) return;
    s32 r=IOS_Ioctl(fd,6,NULL,0,NULL,0); // USBv4 GETVERSION, read-only
    lg("IOS55 HID ioctl6 GETVERSION rc=0x%08X (%d)%s",
       (unsigned)r,(int)r,
       r==0x40001?" CONFIRMADO USBv4": " nao confirma USBv4");
    IOS_Close(fd);
}

// https://wiibrew.org/wiki//dev/usb/oh0:
// IOCTLV20 USBV0_IOCTLV_GETRHPORTSTATUS, IN u8 port, OUT u32 status.
// Detalhes e indexacao ainda marcados "check" na wiki.
// SOMENTE LEITURA: absolutamente NENHUM reset, enable ou comando 25.
// Em IOS55 somente, nunca no IOS58.
static void probe_oh0_port_status(const char *phase)
{
    if (legacy_heap<0) return;
    static char path[] ATTRIBUTE_ALIGN(32)="/dev/usb/oh0";
    s32 fd=IOS_Open(path,0);
    lg("IOS55 PORT STATUS %s: oh0 OPEN=%d",phase,(int)fd);
    if (fd<0) return;
    for (int port=0;port<=1;port++) {
        u32 raw=0xDEADBEEF;
        lg("  IOCTLV20 GETRHPORTSTATUS porta indice=%d: INICIO",port);
        s32 rc=IOS_IoctlvFormat(legacy_heap,fd,20,"b:d",port,&raw,sizeof(raw));
        lg("  IOCTLV20 porta=%d rc=%d raw=%08X",port,(int)rc,(unsigned)raw);
        if(rc>=0 && raw!=0xDEADBEEF) {
            lg("   [OHCI interpretacao PROVISORIA] CCS=%u PES=%u PSS=%u POCI=%u PRS=%u LSDA=%u CSC=%u",
               !!(raw&1),!!(raw&2),!!(raw&4),!!(raw&8),
               !!(raw&16),!!(raw&512),!!(raw&65536));
        } else {
            lg("   Porta %d: sem status confiavel; ioctl ou formato pode nao ser suportado.",port);
        }
    }
    IOS_Close(fd);
}

// ------------------------------------------------------ USBv0 OHCI probe
// Codigo documentado em WiiBrew (/dev/usb/oh0), implementado como na libogc:
// IOS_IoctlvFormat(...,12,"bb:dd", max_devices, interface_class, count, buffer).
// O IOS55 nao tem /dev/usb/hid/ven: por isso consultar OH0 E OH1 em separado.
// Consultas APENAS GETDEVLIST; nao muda porta, configuracao nem endereco.
static void direct_scan_host(int host, int classidx, int detailed)
{
    static const u8 cls[LEGACY_CLASSES] = {0, 3, 8};
    static char node[32] ATTRIBUTE_ALIGN(32);
    u8 cnt = 0;
    int prev = direct_counts[host][classidx];
    s32 prevrc = direct_rc[host][classidx];
    if (legacy_heap < 0) {
        if (detailed) lg("USBv0: sem IOS heap, nao consultado");
        return;
    }
    snprintf(node, sizeof node, "/dev/usb/oh%d", host);
    s32 fd = IOS_Open(node, 0);
    if (fd < 0) {
        if (prevrc != fd || detailed) lg("USBv0 %s class=%u: OPEN=%d", node, cls[classidx], (int)fd);
        direct_rc[host][classidx] = fd;
        return;
    }
    u32 *buf = (u32 *)iosAlloc(legacy_heap, MAXD*8);
    if (!buf) { lg("USBv0 %s: iosAlloc falhou", node); IOS_Close(fd); return; }
    memset(buf, 0, MAXD*8);
    s32 rc = IOS_IoctlvFormat(legacy_heap, fd, 12, "bb:dd",
                            (int)MAXD, (int)cls[classidx],
                            &cnt, sizeof(cnt), buf, MAXD*8);
    if (cnt > MAXD) {
        lg("USBv0 %s classe=%u: count invalido=%u, limitado a %u",
           node, cls[classidx], cnt, MAXD);
        cnt = MAXD;
    }
    if (detailed || prev != cnt || prevrc != rc)
        lg("USBv0 %s GETDEVLIST class=%u: rc=%d count=%u",
           node, cls[classidx], (int)rc, (unsigned)cnt);
    direct_rc[host][classidx] = rc;
    if (rc >= 0) {
        direct_counts[host][classidx] = cnt;
        if ((int)cnt > direct_seen[host][classidx]) direct_seen[host][classidx] = cnt;
        for (u32 i=0; i<cnt; ++i) {
            u16 vid=(u16)(buf[2*i+1]>>16), pid=(u16)buf[2*i+1];
            if (detailed || prev != cnt) {
                lg("   USBv0 oh%d class=%u item=%u vid:pid=%04X:%04X",
                   host, cls[classidx], (unsigned)i, vid, pid);
            }
            if (vid==0x0810 && pid==0x0001) {
                seen_twin_all=1;
                if(host==0)twin_seen_oh0=1;
                lg("!!! TWIN 0810:0001 VIA OH%d IOS55 !!!",host);
            }
            // So tenta descritores ao localizar uma interface HID.
            // Isso evita interferir no pendrive de jogos/logs e deixa outras classes intactas.
            if (cls[classidx]==3 && (detailed || prev != cnt) &&
                vid!=0 && pid!=0) {
                usb_device_entry e;
                memset(&e,0,sizeof e);
                e.vid=vid; e.pid=pid;
                e.device_id=host==0 ? USB_OH0_DEVICE_ID : USB_OH1_DEVICE_ID;
                open_dev(&e);
            }
        }
    } else direct_counts[host][classidx] = -1;
    iosFree(legacy_heap, buf);
    IOS_Close(fd);
}
static void direct_scan_ohci(int detailed)
{
    for (int host=0; host<2; ++host)
        for (int ci=0; ci<LEGACY_CLASSES; ++ci)
            direct_scan_host(host,ci,detailed);
}

// ----------------------------------------------------------- scan
static void scan_loaded_ios(u32 n)
{
    int v4_found=0;
    lg("=========== IOS%u: INICIO SCAN UNIVERSAL ===========",n);
    if(n==55) {
        lg("AUTONOMOUS V2: sondando HIDv4 independentemente da libogc");
        v4_found=maizon_hidv4_discover(lg,1700);
        if(v4_found>0)seen_twin_all=1;
        lg("HIDv4 resultado=%d (1=Twin,0=nenhum,<0=erro)",v4_found);
    }
    probe_nodes("antes USB_Initialize");
    s32 r = USB_Initialize();
    lg("USB_Initialize = %d (nao significa detectar uma porta)",(int)r);
    if (r < 0) { lg("USB init falhou; nao sera feita listagem"); return; }
    probe_nodes("apos USB_Initialize");
    if (n == 55) {
        legacy_heap = iosCreateHeap(8192);
        lg("USBv0 heap=%d | sondagem DIRETA GETDEVLIST OH0 e OH1",legacy_heap);
        probe_legacy_hid_version();
        direct_scan_ohci(1);
        probe_oh0_port_status("ANTES");
    } else {
        lg("IOS58: biblioteca utiliza VEN/HID v5; /dev/usb/oh0 usa protocolo diferente.");
        lg("IOS58: nao chamar IOCTLV 12 legado nem ler registradores fisicos do Starlet.");
    }
    ncur=0; nprv=0;
    e_all=-99999; e_hid=-99999; e_storage_global=-99999;
    memset(prv, 0, sizeof prv);
    u32 t0 = ms(), tp=0, last_direct=ms();
    int seen_twin=0, max_all=0, max_hid=0, max_storage=0;
    int last_all=-999, last_hid=-999, last_storage=-999;
    while (ms() - t0 < (n == 58 ? BASELINE_MS : SCAN_MS)) {
        u32 now=ms();
        if (now-tp >= 650) {
            tp=now;
            int a=get_list(cur,0,&e_all);
            int h=get_list(hidl,3,&e_hid);
            int st=get_list(stor,8,&e_storage_global);
            if (a!=last_all || h!=last_hid || st!=last_storage) {
                lg("LIBOGC IOS%u class0=ALL:%d class3=HID:%d class8=STORAGE_HINT:%d",
                   n,a,h,st);
                last_all=a;last_hid=h;last_storage=st;
            }
            if (a>max_all)max_all=a;
            if (h>max_hid)max_hid=h;
            if (st>max_storage)max_storage=st;
            if (a>=0) {
                ncur=a;
                for (int i=0; i<ncur; i++) {
                    if (in_list(&cur[i],prv,nprv))continue;
                    int in_hid=in_list(&cur[i],hidl,h>0?h:0);
                    lg("+++ LIBOGC IOS%u ATTACH %04X:%04X devid=%d HID=%d",
                       n,cur[i].vid,cur[i].pid,(int)cur[i].device_id,in_hid);
                    if (cur[i].vid==0x0810 && cur[i].pid==0x0001) {
                        seen_twin=1;seen_twin_all=1;
                        lg("!!! TWIN 0810:0001 APARECEU IOS%u !!!",n);
                    }
                    // Classe 8 sob VEN v5 nao e filtro fiel de armazenamento.
                    // Abrir SOMENTE entradas HID: nao interferir no pendrive.
                    if (in_hid) open_dev(&cur[i]);
                }
                for(int i=0;i<nprv;i++)
                    if(!in_list(&prv[i],cur,ncur))
                        lg("--- IOS%u DETACH %04X:%04X",n,prv[i].vid,prv[i].pid);
                memcpy(prv,cur,sizeof cur);nprv=ncur;
            }
        }
        if (n==55 && now-last_direct>=3500) {
            last_direct=now;
            direct_scan_ohci(0);
        }
        VIDEO_WaitVSync();
    }
    if(n==55) {
        probe_oh0_port_status("DEPOIS");
        if(twin_seen_oh0 && v4_found<=0){
            lg("RECOVERY: Twin visto no OH0, mas nao no HIDv4; reset apenas do handle 0810:0001");
            int rr=maizon_reset_known_twin(lg);
            lg("RECOVERY: reset do dispositivo rc=%d",rr);
            if(rr==0) {
                direct_scan_ohci(1);
                maizon_hidv4_discover(lg,1500);
            }
        }else lg("RECOVERY: reset de dispositivo omitido (alvo nao enumerado em OH0 ou ja capturado)");
    }
    lg("IOS%u RESUMO: max_any=%d max_hid=%d max_class8=%d twin=%d",
       n,max_all,max_hid,max_storage,seen_twin);
    if (n==55) {
        for(int host=0;host<2;host++) {
            lg("IOS55 USBv0 OH%d: class0 rc=%d count=%d max=%d",
               host,(int)direct_rc[host][0],direct_counts[host][0],direct_seen[host][0]);
            lg("IOS55 USBv0 OH%d: class3 rc=%d count=%d max=%d",
               host,(int)direct_rc[host][1],direct_counts[host][1],direct_seen[host][1]);
            lg("IOS55 USBv0 OH%d: class8 rc=%d count=%d max=%d",
               host,(int)direct_rc[host][2],direct_counts[host][2],direct_seen[host][2]);
        }
        max_detected_55=max_all;
    } else max_detected_58=max_all;
    close_all();
    USB_Deinitialize();
    usleep(300000);
}

static void scan_ios(u32 n)
{
    lg("=========== SCAN IOS%u ===========", n);
    u32 rev = 0;
    int st=ios_state(n,&rev);
    if(st==0){lg("IOS%u ausente, pulando",n);return;}
    if(st<0){lg("IOS%u stub aparente, pulando",n);return;}
    lg("IOS%u rev %u; IOS_ReloadIOS",n,rev);
    s32 r=IOS_ReloadIOS(n);
    lg("IOS_ReloadIOS(%u)=%d, agora IOS%d rev %d",n,(int)r,IOS_GetVersion(),IOS_GetRevision());
    if (r<0 || IOS_GetVersion()!=(s32)n) return;
    scan_loaded_ios(n);
}

int main(void)
{
    VIDEO_Init();
    GXRModeObj *rmode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth - 40, rmode->xfbHeight - 40,
                 rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    printf("\x1b[2;0H");

    lg("MAIZON USB AUTONOMOUS V2.0 ALPHA | IOS inicial %d rev %d", IOS_GetVersion(), IOS_GetRevision());
    lg("Mantenha o Twin plugado e o controle ligado. Nao precisa tocar em nada.");
    lg("Sem NAND/SD. IOCTLV 12 (somente leitura) em OH0/OH1 de IOS55.");
    lg("IOS55: HIDv4, OH0/OH1, status porta 0/1, reset condicionado ao Twin enumerado.");
    lg("Nao executa IOCTLV25: parametros e indexacao nao validados.");
    if (IOS_GetVersion()==58) scan_loaded_ios(58);
    else lg("IOS inicial nao 58; baseline IOS58 indisponivel.");

#ifdef SCAN_EXTRA
    static const u32 cand[] = { 55, 37, 36 };   // 37/36: so se voce confirmar que nao sao stub
#else
    static const u32 cand[] = { 55 };           // 55 ja comprovado que carrega
#endif
    int skip_legacy=maizon_journal_before_ios55(lg,logbuf,loglen);
    if(skip_legacy) {
        lg("AUTORECOVERY: pulando IOS55 nesta execucao devido a marker pendente");
    } else {
        for (unsigned i = 0; i < sizeof cand / sizeof cand[0]; i++) scan_ios(cand[i]);
    }

    lg("=========== RESUMO GERAL ===========");
    lg("IOS58 max dispositivos libogc=%d | IOS55 max=%d | Twin=%d",max_detected_58,max_detected_55,seen_twin_all);
    lg("OHCI IOS55 informa dispositivos via IOCTLV sem depender do HID service.");
    lg("Port status acessado via interface IOS55 IOCTLV20 quando suportado; campos hipoteticos.");
    if(!seen_twin_all) lg("NAO ENUMERADO pelos metodos testados; causa eletrica NAO determinada.");
    lg("=========== RESTAURANDO IOS58 ===========");
    s32 r = IOS_ReloadIOS(58);
    lg("IOS_ReloadIOS(58) = %d, agora IOS%d", (int)r, IOS_GetVersion());

    if (IOS_GetVersion() != 58) lg("AVISO: IOS58 NAO RESTAURADO; gravacao USB pode falhar");
    int saved = 0;
    for (int tries = 0; tries < 10 && !saved; tries++) {
        if (fatMountSimple("usb", &__io_usbstorage)) {
            lg("pendrive montado (tentativa %d)", tries + 1);
            if (save_to("usb", "MAIZON_TWIN_SCAN", NULL, "log.txt") == 0) saved = 1;
        }
        if (!saved) { fatUnmount("usb:"); sleep(1); }
    }
    lg("log USB salvo=%d (1=sim). Sem SD. Fim em 45 s.", saved);
    if(saved) {
        maizon_journal_commit(lg);
        save_to("usb", "MAIZON_TWIN_SCAN", NULL, "log.txt");
    }
    if (!saved) lg("FALHA DE LOG USB: fotografe as linhas da tela, inclusive NODES e LIST.");
    sleep(45);
    return 0;
}