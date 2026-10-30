// MAIZON TWIN IOS SCAN v1.1 (mesmo projeto: enumeração universal e correções)
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
#include <ogc/es.h>
#include <ogc/usb.h>
#include <ogc/usbstorage.h>
#include <fat.h>

#ifndef TITLE_ID
#define TITLE_ID(x,y) (((u64)(x) << 32) | (y))
#endif

#define SCAN_MS 20000
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
static s32 e_all = 0, e_hid = 0;

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

// ----------------------------------------------------------- scan
static void scan_ios(u32 n)
{
    lg("=========== SCAN IOS%u ===========", n);
    u32 rev = 0;
    int st = ios_state(n, &rev);
    if (st == 0) { lg("IOS%u nao instalado: pulando", n); return; }
    if (st < 0)  { lg("IOS%u e STUB (rev %u): pulando", n, rev); return; }
    lg("IOS%u rev %u instalado; recarregando", n, rev);

    s32 r = IOS_ReloadIOS(n);
    lg("IOS_ReloadIOS(%u) = %d, agora IOS%d rev %d", n, (int)r, IOS_GetVersion(), IOS_GetRevision());
    if (IOS_GetVersion() != (s32)n) return;

    probe_nodes("apos reload");
    r = USB_Initialize();
    lg("USB_Initialize = %d (nao prova existencia de OHCI/HID)", (int)r);
    if (r < 0) { lg("USB init falhou; pulando lista"); return; }
    nprv = 0;
    memset(prv, 0, sizeof prv);

    u32 t0 = ms(), tp = 0;
    int seen_twin = 0;
    while (ms() - t0 < SCAN_MS) {
        u32 now = ms();
        if (now - tp >= 500) {
            tp = now;
            int a = get_list(cur, 0, &e_all);
            int h = get_list(hidl, 3, &e_hid);
            s32 e_storage = 0;
            int s = get_list(stor, 8, &e_storage);
            if (a >= 0) {
                ncur = a;
                int nh = h > 0 ? h : 0;
                int ns = s > 0 ? s : 0;
                for (int i = 0; i < ncur; i++) {
                    if (in_list(&cur[i], prv, nprv)) continue;
                    int ish = in_list(&cur[i], hidl, nh);
                    int is_mass = in_list(&cur[i], stor, ns);
                    lg("+++ ATTACH %04X:%04X devid=%d HID=%d STORAGE=%d", cur[i].vid, cur[i].pid,
                       (int)cur[i].device_id, ish, is_mass);
                    if (cur[i].vid == 0x0810 && cur[i].pid == 0x0001) {
                        seen_twin = 1;
                        lg("!!! TWIN 0810:0001 APARECEU NO IOS%u !!!", n);
                    }
                    // Sem filtro VID/PID: qualquer dispositivo nao identificado como armazenamento.
                    // Evita mexer no pendrive usado para salvar o log.
                    if (!is_mass) open_dev(&cur[i]);
                }
                for (int i = 0; i < nprv; i++) {
                    if (in_list(&prv[i], cur, ncur)) continue;
                    lg("--- DETACH %04X:%04X", prv[i].vid, prv[i].pid);
                }
                memcpy(prv, cur, sizeof cur);
                nprv = ncur;
            }
        }
        service_trk(now);
        VIDEO_WaitVSync();
    }
    lg("IOS%u: fim da janela. dispositivos=%d, Twin visto=%d", n, ncur, seen_twin);
    close_all();
    USB_Deinitialize();
    usleep(300000);
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

    lg("MAIZON TWIN IOS SCAN v1.1 | IOS inicial %d rev %d", IOS_GetVersion(), IOS_GetRevision());
    lg("Mantenha o Twin plugado e o controle ligado. Nao precisa tocar em nada.");
    lg("Sem escrita na NAND; so IOS_ReloadIOS em IOS ja instalados.");

#ifdef SCAN_EXTRA
    static const u32 cand[] = { 55, 37, 36 };   // 37/36: so se voce confirmar que nao sao stub
#else
    static const u32 cand[] = { 55 };           // 55 ja comprovado que carrega
#endif
    for (unsigned i = 0; i < sizeof cand / sizeof cand[0]; i++) scan_ios(cand[i]);

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
    if (saved) save_to("usb", "MAIZON_TWIN_SCAN", NULL, "log.txt");
    if (!saved) lg("FALHA DE LOG USB: fotografe as linhas da tela, inclusive NODES e LIST.");
    sleep(45);
    return 0;
}