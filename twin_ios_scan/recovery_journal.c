/* Persistent FAT breadcrumb. The Wii's NAND is never written.
   A hard IOS55 hang may prevent the final log: snapshot is saved beforehand.
   On next launch, the unfinished marker means skip IOS55 once. */
#include <gccore.h>
#include <ogc/usbstorage.h>
#include <fat.h>
#include <stdio.h>
#include <sys/stat.h>
#include "recovery_journal.h"
#define MARKER "usb:/apps/MAIZON_TWIN_SCAN/ios55_pending.txt"
#define SNAPSHOT "usb:/apps/MAIZON_TWIN_SCAN/log_before_ios55.txt"
int maizon_journal_before_ios55(maizon_log_fn log,const void *logbuf,size_t logsize)
{
    if(IOS_GetVersion()!=58) {
        log("JOURNAL: IOS58 nao esta ativo; sem checkpoint");
        return 0;
    }
    if(!fatMountSimple("usb",&__io_usbstorage)) {
        log("JOURNAL: pendrive indisponivel, sem checkpoint");
        return 0;
    }
    mkdir("usb:/apps",0777);
    mkdir("usb:/apps/MAIZON_TWIN_SCAN",0777);
    FILE *f=fopen(SNAPSHOT,"wb");
    if(f) {
        size_t n=fwrite(logbuf,1,logsize,f);
        int rc=fclose(f);
        log("JOURNAL: snapshot %u/%u bytes rc=%d",
            (unsigned)n,(unsigned)logsize,rc);
    } else log("JOURNAL: snapshot nao gravado");
    f=fopen(MARKER,"rb");
    if(f) {
        fclose(f);
        log("JOURNAL: IOS55 ficou pendente na execucao anterior; pulando-o agora");
        fatUnmount("usb:");
        return 1;
    }
    f=fopen(MARKER,"wb");
    if(f) {
        fputs("MAIZON: ultimo IOS55 sem retorno confirmado. Nao prova crash.\n",f);
        log("JOURNAL: marker rc=%d",fclose(f));
    } else log("JOURNAL: falha ao criar marker");
    fatUnmount("usb:");
    return 0;
}
void maizon_journal_commit(maizon_log_fn log)
{
    if(IOS_GetVersion()==58)
        log("JOURNAL: remover marker rc=%d (0=OK)",remove(MARKER));
}
