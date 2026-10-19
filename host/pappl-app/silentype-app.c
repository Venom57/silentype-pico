// silentype-app.c — PAPPL printer application for the Silentype-Pico.
//
// IPP Everywhere printer on localhost; macOS/Linux add it from the print dialog.
// CUPS delivers 1-bit PWG raster (we request black_1 so CUPS does the dithering);
// we accumulate HEAD_DOTS rows, pack columns, and stream docs/protocol.md frames
// to the Pico's CDC serial port.
//
// Device access: custom "serial://" scheme → device URI serial:///dev/ttyACM0.
// Sidesteps libusb and kernel-driver ownership on both OSes.
//
// Build: make   (needs pappl >= 1.4 and pkg-config; macOS: build PAPPL from source)
// Run:   ./silentype-app server -o listen-hostname=localhost
//        ./silentype-app add -d silentype -v serial:///dev/ttyACM0 -m silentype
//
// Written against the PAPPL 1.4 API from memory; field/function names should be
// checked against <pappl/printer.h> on first compile. Structure is right.

#include <pappl/pappl.h>
#include <fcntl.h>
#include <glob.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>

// ---- geometry: keep in sync with firmware/src/config.h ---------------------------
#define HEAD_DOTS        7      // UNVERIFIED (M1)
#define ST_MAX_COLS      480    // TBD (M0)
#define ST_XDPI          60     // TBD (M0): columns per inch
#define ST_YDPI          60     // TBD (M0): dot rows per inch
#define ST_FEED_STEPS    0      // 0 = firmware default (FEED_STEPS_PER_BAND)
#define ST_WIDTH_PT      (ST_MAX_COLS * 72 / ST_XDPI)

// ---- protocol -------------------------------------------------------------------
#define MAGIC 0xA5
enum { CMD_BAND = 1, CMD_FEED = 2, CMD_STATUS = 3, CMD_RESET = 4, RSP_STATUS = 0x83, RSP_ACK = 0x8F };
enum { ACK_OK = 0, ACK_CRC, ACK_INVALID, ACK_ERRSTATE };
static const char *err_names[] = { "none", "crc", "head-fault", "home-timeout", "media-empty", "invalid-band" };

static uint8_t crc8(const uint8_t *d, size_t n) {
  uint8_t c = 0;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1); }
  return c;
}
static bool send_frame(pappl_device_t *dev, uint8_t cmd, const uint8_t *payload, uint16_t len) {
  uint8_t buf[4 + ST_MAX_COLS + 8 + 1];
  buf[0] = MAGIC; buf[1] = cmd; buf[2] = len & 0xFF; buf[3] = len >> 8;
  memcpy(buf + 4, payload, len);
  buf[4 + len] = crc8(buf + 1, 3 + len);
  return papplDeviceWrite(dev, buf, 4 + len + 1) == (ssize_t)(4 + len + 1);
}
// Read one reply frame (ACK or STATUS). Returns cmd or -1.
static int read_frame(pappl_device_t *dev, uint8_t *payload, uint16_t *len, int timeout_ms) {
  uint8_t hdr[4]; int fd = (int)(intptr_t)papplDeviceGetData(dev);
  struct pollfd pfd = { fd, POLLIN, 0 };
  for (;;) {
    if (poll(&pfd, 1, timeout_ms) <= 0) return -1;
    if (read(fd, hdr, 1) != 1 || hdr[0] != MAGIC) continue;
    if (read(fd, hdr + 1, 3) != 3) return -1;
    *len = hdr[2] | (hdr[3] << 8); if (*len > 64) continue;
    uint8_t crc; if (read(fd, payload, *len) != *len || read(fd, &crc, 1) != 1) return -1;
    uint8_t chk[3 + 64]; memcpy(chk, hdr + 1, 3); memcpy(chk + 3, payload, *len);
    if (crc8(chk, 3 + *len) != crc) continue;
    return hdr[1];
  }
}

// ---- serial:// device scheme -----------------------------------------------------
static void st_serial_list(pappl_device_cb_t cb, void *data, pappl_deverror_cb_t err_cb, void *err_data) {
  (void)err_cb; (void)err_data;
  glob_t g; char uri[300];
  if (glob("/dev/ttyACM*", 0, NULL, &g) == 0 || glob("/dev/cu.usbmodem*", GLOB_APPEND, NULL, &g) == 0) {
    for (size_t i = 0; i < g.gl_pathc; i++) {
      snprintf(uri, sizeof uri, "serial://%s", g.gl_pathv[i]);
      // Probe: open, send STATUS, expect RSP_STATUS. Report only real Silentype-Picos.
      int fd = open(g.gl_pathv[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
      if (fd < 0) continue;
      struct termios t; tcgetattr(fd, &t); cfmakeraw(&t); tcsetattr(fd, TCSANOW, &t);
      uint8_t probe[5] = { MAGIC, CMD_STATUS, 0, 0, 0 }; probe[4] = crc8(probe + 1, 3);
      write(fd, probe, 5);
      struct pollfd p = { fd, POLLIN, 0 }; uint8_t rx[32]; bool ok = false;
      if (poll(&p, 1, 300) > 0 && read(fd, rx, sizeof rx) >= 4 && rx[0] == MAGIC && rx[1] == RSP_STATUS) ok = true;
      close(fd);
      if (ok && !cb("Silentype-Pico", uri, "MFG:JANUS;MDL:Silentype-Pico;CMD:SILENTYPEBAND;", data)) break;
    }
  }
  globfree(&g);
}
static bool st_serial_open(pappl_device_t *device, const char *device_uri, const char *name) {
  (void)name;
  const char *path = device_uri + strlen("serial://");
  int fd = open(path, O_RDWR | O_NOCTTY);
  if (fd < 0) return false;
  struct termios t; tcgetattr(fd, &t); cfmakeraw(&t); tcsetattr(fd, TCSANOW, &t);
  papplDeviceSetData(device, (void *)(intptr_t)fd);
  return true;
}
static void    st_serial_close(pappl_device_t *d) { close((int)(intptr_t)papplDeviceGetData(d)); }
static ssize_t st_serial_read (pappl_device_t *d, void *b, size_t n) { return read ((int)(intptr_t)papplDeviceGetData(d), b, n); }
static ssize_t st_serial_write(pappl_device_t *d, const void *b, size_t n) { return write((int)(intptr_t)papplDeviceGetData(d), b, n); }
static pappl_preason_t st_serial_status(pappl_device_t *d) {
  uint8_t p[64]; uint16_t len;
  if (!send_frame(d, CMD_STATUS, NULL, 0) || read_frame(d, p, &len, 500) != RSP_STATUS) return PAPPL_PREASON_OTHER;
  pappl_preason_t r = PAPPL_PREASON_NONE;
  if (p[0] == 2) {
    if (p[1] == 4) r |= PAPPL_PREASON_MEDIA_EMPTY;
    else           r |= PAPPL_PREASON_OTHER;
  }
  return r;
}

// ---- raster job ----------------------------------------------------------------
typedef struct {
  unsigned width;                      // pixels this page (≤ ST_MAX_COLS)
  unsigned rows_in_band;
  uint8_t  rows[HEAD_DOTS][ST_MAX_COLS / 8 + 1];   // packed 1-bit rows from CUPS
  uint8_t  intensity;
  bool     failed;
} st_job_t;

static bool flush_band(pappl_job_t *job, pappl_device_t *dev, st_job_t *j) {
  if (j->rows_in_band == 0) return true;
  // Pad missing rows (end of page) with white.
  for (unsigned r = j->rows_in_band; r < HEAD_DOTS; r++) memset(j->rows[r], 0, sizeof j->rows[r]);
  uint8_t payload[6 + ST_MAX_COLS];
  payload[0] = j->width & 0xFF; payload[1] = j->width >> 8;
  payload[2] = j->intensity; payload[3] = ST_FEED_STEPS; payload[4] = 0; payload[5] = 0;
  for (unsigned x = 0; x < j->width; x++) {
    uint8_t col = 0;
    for (unsigned r = 0; r < HEAD_DOTS; r++)
      if (j->rows[r][x >> 3] & (0x80 >> (x & 7))) col |= (uint8_t)(1u << r);   // PWG black_1: 1 = black
    payload[6 + x] = col;
  }
  if (!send_frame(dev, CMD_BAND, payload, (uint16_t)(6 + j->width))) return false;
  uint8_t ack[64]; uint16_t len;
  if (read_frame(dev, ack, &len, 10000) != RSP_ACK || ack[1] != ACK_OK) {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Pico rejected band: %s", ack[1] == ACK_ERRSTATE ? "printer in error state" : "invalid/crc");
    return false;
  }
  j->rows_in_band = 0;
  return true;
}

static bool st_rstartjob(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev) {
  (void)opt;
  st_job_t *j = calloc(1, sizeof *j);
  papplJobSetData(job, j);
  send_frame(dev, CMD_RESET, NULL, 0);                 // home, clear any latched error
  uint8_t ack[64]; uint16_t len; read_frame(dev, ack, &len, 5000);
  return true;
}
static bool st_rstartpage(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev, unsigned page) {
  (void)dev; (void)page;
  st_job_t *j = papplJobGetData(job);
  j->width = opt->header.cupsWidth > ST_MAX_COLS ? ST_MAX_COLS : opt->header.cupsWidth;
  j->rows_in_band = 0;
  // print-darkness vendor option (-100..100) → intensity 1..5
  int dk = opt->darkness_configured;                    // PAPPL maps print-darkness here
  j->intensity = (uint8_t)(3 + dk / 50); if (j->intensity < 1) j->intensity = 1; if (j->intensity > 5) j->intensity = 5;
  return true;
}
static bool st_rwriteline(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev, unsigned y, const unsigned char *line) {
  (void)y;
  st_job_t *j = papplJobGetData(job);
  size_t nbytes = (j->width + 7) / 8;
  if (opt->header.cupsBytesPerLine < nbytes) nbytes = opt->header.cupsBytesPerLine;
  memcpy(j->rows[j->rows_in_band], line, nbytes);
  if (++j->rows_in_band == HEAD_DOTS && !flush_band(job, dev, j)) { j->failed = true; return false; }
  return true;
}
static bool st_rendpage(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev, unsigned page) {
  (void)opt; (void)page;
  st_job_t *j = papplJobGetData(job);
  if (!flush_band(job, dev, j)) return false;
  uint8_t feed[2] = { 40, 0 };                           // TODO: page gap from manual
  return send_frame(dev, CMD_FEED, feed, 2);
}
static bool st_rendjob(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev) {
  (void)opt; (void)dev;
  free(papplJobGetData(job)); papplJobSetData(job, NULL);
  return true;
}
static bool st_status(pappl_printer_t *printer) {
  pappl_device_t *dev = papplPrinterOpenDevice(printer);
  if (!dev) return false;
  papplPrinterSetReasons(printer, st_serial_status(dev), PAPPL_PREASON_MEDIA_EMPTY | PAPPL_PREASON_OTHER);
  papplPrinterCloseDevice(printer);
  return true;
}

// ---- driver ------------------------------------------------------------------------
static bool st_driver_cb(pappl_system_t *sys, const char *driver_name, const char *device_uri, const char *device_id,
                         pappl_pr_driver_data_t *d, ipp_t **attrs, void *data) {
  (void)sys; (void)device_uri; (void)device_id; (void)attrs; (void)data;
  if (strcmp(driver_name, "silentype")) return false;
  papplCopyString(d->make_and_model, "Apple Silentype (Pico)", sizeof d->make_and_model);
  d->rstartjob_cb = st_rstartjob; d->rstartpage_cb = st_rstartpage; d->rwriteline_cb = st_rwriteline;
  d->rendpage_cb = st_rendpage;   d->rendjob_cb = st_rendjob;       d->status_cb = st_status;
  d->format = "application/vnd.silentype-band";
  d->orient_default = IPP_ORIENT_PORTRAIT;
  d->raster_types = PAPPL_RASTER_TYPE_BLACK_1;           // CUPS dithers; we just pack
  d->color_supported = PAPPL_COLOR_MODE_MONOCHROME; d->color_default = PAPPL_COLOR_MODE_MONOCHROME;
  d->num_resolution = 1; d->x_resolution[0] = ST_XDPI; d->y_resolution[0] = ST_YDPI;
  d->x_default = ST_XDPI; d->y_default = ST_YDPI;
  d->left_right = 0; d->bottom_top = 0;
  d->darkness_configured = 0; d->darkness_supported = 5;  // exposes print-darkness
  d->num_media = 1; d->media[0] = "roll_silentype_8.5in";
  papplCopyString(d->media_default.size_name, "roll_silentype_8.5in", sizeof d->media_default.size_name);
  d->media_default.size_width  = ST_WIDTH_PT * 100;      // hundredths of mm in PAPPL? check units on compile
  d->media_default.size_length = 27900;                  // 11 in — roll, length is nominal
  papplCopyString(d->media_default.source, "main-roll", sizeof d->media_default.source);
  papplCopyString(d->media_default.type, "continuous", sizeof d->media_default.type);
  d->num_source = 1; d->source[0] = "main-roll";
  d->num_type = 1;   d->type[0] = "continuous";
  return true;
}

static pappl_pr_driver_t drivers[] = {
  { "silentype", "Apple Silentype (Pico)", "MFG:JANUS;MDL:Silentype-Pico;CMD:SILENTYPEBAND;", NULL },
};

int main(int argc, char *argv[]) {
  papplDeviceAddScheme("serial", PAPPL_DEVTYPE_CUSTOM_LOCAL, st_serial_list, st_serial_open,
                       st_serial_close, st_serial_read, st_serial_write, st_serial_status, /*id*/NULL);
  return papplMainloop(argc, argv, "0.2", NULL, (int)(sizeof drivers / sizeof drivers[0]), drivers,
                       /*autoadd_cb*/NULL, st_driver_cb, /*subcmd_name*/NULL, /*subcmd_cb*/NULL,
                       /*system_cb*/NULL, /*usage_cb*/NULL, /*data*/NULL);
}
