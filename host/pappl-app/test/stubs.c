// Minimal stand-ins for the PAPPL calls silentype-app.c makes, so its job and
// device code can run against a fake Pico without libpappl.
#include <pappl/pappl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct _pappl_device_s  { void *data; };
struct _pappl_job_s     { void *data; };
struct _pappl_printer_s { pappl_device_t *dev; pappl_preason_t reasons; };

void *papplDeviceGetData(pappl_device_t *d)          { return d->data; }
void  papplDeviceSetData(pappl_device_t *d, void *v) { d->data = v; }
void  papplDeviceError(pappl_device_t *d, const char *m, ...) {
  (void)d; va_list ap; va_start(ap, m); fputs("device error: ", stderr); vfprintf(stderr, m, ap); fputc('\n', stderr); va_end(ap);
}
void *papplJobGetData(pappl_job_t *j)                { return j->data; }
void  papplJobSetData(pappl_job_t *j, void *v)       { j->data = v; }
void  papplLogJob(pappl_job_t *j, pappl_loglevel_t l, const char *m, ...) {
  (void)j; if (l < PAPPL_LOGLEVEL_INFO) return;
  va_list ap; va_start(ap, m); fputs("  log: ", stderr); vfprintf(stderr, m, ap); fputc('\n', stderr); va_end(ap);
}
pappl_device_t *papplPrinterOpenDevice(pappl_printer_t *p) { return p->dev; }
void  papplPrinterCloseDevice(pappl_printer_t *p) { (void)p; }
void  papplPrinterSetReasons(pappl_printer_t *p, pappl_preason_t add, pappl_preason_t remove) { p->reasons = (p->reasons & ~remove) | add; }
size_t papplCopyString(char *dst, const char *src, size_t n) { size_t l = strlen(src); if (n) { snprintf(dst, n, "%s", src); } return l; }
void  papplDeviceAddScheme(const char *s, pappl_devtype_t t, pappl_devlist_cb_t l, pappl_devopen_cb_t o, pappl_devclose_cb_t c,
                           pappl_devread_cb_t r, pappl_devwrite_cb_t w, pappl_devstatus_cb_t st, pappl_devid_cb_t id) {
  (void)s; (void)t; (void)l; (void)o; (void)c; (void)r; (void)w; (void)st; (void)id;
}
int   papplMainloop(int argc, char *argv[], const char *v, const char *f, int nd, pappl_pr_driver_t *d, pappl_pr_autoadd_cb_t a,
                    pappl_pr_driver_cb_t dc, const char *sn, pappl_ml_subcmd_cb_t sc, pappl_ml_system_cb_t sy, pappl_ml_usage_cb_t u, void *data) {
  (void)argc; (void)argv; (void)v; (void)f; (void)nd; (void)d; (void)a; (void)dc; (void)sn; (void)sc; (void)sy; (void)u; (void)data;
  return 0;
}
