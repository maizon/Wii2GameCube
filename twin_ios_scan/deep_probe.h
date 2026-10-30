#ifndef MAIZON_DEEP_PROBE_H
#define MAIZON_DEEP_PROBE_H
typedef void (*maizon_log_fn)(const char *, ...);
/* Independent IOS55 HID v4 discovery, with bounded asynchronous waits.
   Returns 1 on matched Twin, 0 if absent, negative on probe error. */
int maizon_hidv4_discover(maizon_log_fn log, unsigned timeout_ms);
/* Reset only a previously enumerated Twin child handle, NEVER a root port. */
int maizon_reset_known_twin(maizon_log_fn log);
#endif
