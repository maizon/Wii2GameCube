#ifndef MAIZON_RECOVERY_JOURNAL_H
#define MAIZON_RECOVERY_JOURNAL_H
#include <stddef.h>
#include "deep_probe.h"
int maizon_journal_before_ios55(maizon_log_fn log,const void *logbuf,size_t logsize);
void maizon_journal_commit(maizon_log_fn log);
#endif
