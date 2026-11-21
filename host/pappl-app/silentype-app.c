// silentype-app.c — PAPPL printer application for the Silentype-Pico.
//
// IPP Everywhere printer on localhost; macOS/Linux add it from the print dialog.
// PAPPL delivers 1-bit black raster (we ask for black_1, so PAPPL/CUPS dither); we
// collect HEAD_DOTS rows, pack them into columns and send docs/protocol.md frames
// to the Pico's CDC serial port.
//
// Device access: custom "serial://" scheme, device URI serial:///dev/ttyACM0 (or
// serial:///dev/silentype with the udev rule). No libusb, no kernel-driver fights.
//
// Build: make   (needs PAPPL >= 1.4 and pkg-config; macOS: build PAPPL from source)
// Run:   ./silentype-app server -o listen-hostname=localhost
//        ./silentype-app add -d silentype -v serial:///dev/ttyACM0 -m silentype
//
// Checked against the PAPPL 1.4.12 headers (pappl/device.h, printer.h, job.h).

#include <pappl/pappl.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// ---- geometry: keep in sync with firmware/src/config.h ---------------------------
#define HEAD_DOTS        7      // ROM-confirmed (docs/firmware-archaeology)
#define ST_MAX_COLS      560    // MAX_COLS: 80 chars x 7 columns (card ROM); ruler check at M1
#define ST_XDPI          75     // ~560 columns over ~7.5 in; ruler check at M1
#define ST_YDPI          40     // one raster row = one paper step; manual: 40 steps ~ 1 in
#define ST_MAX_INTENSITY 7      // MAX_INTENSITY
#define ST_DEFAULT_INTENSITY 1  // DEFAULT_INTENSITY: below Apple's 5 until the head is characterised
#define ST_FEED_STEPS    0      // 0 = firmware default (FEED_STEPS_PER_BAND)
#define ST_PAGE_FEED     40     // steps after each page (~1 in)
#define ST_MAX_LINE      256    // bytes per raster line we keep (8.5 in at 75 dpi = 80 bytes)

// Media: 8.5 in thermal roll, nominal 11 in "pages". PAPPL sizes are hundredths of a mm.
#define ST_MEDIA_NAME    "na_letter_8.5x11in"
#define ST_MEDIA_WIDTH   21590
#define ST_MEDIA_LENGTH  27940
// Side margin so the printable width is ST_MAX_COLS / ST_XDPI (7.47 in) centred on 8.5 in.
#define ST_SIDE_MARGIN   ((ST_MEDIA_WIDTH - ST_MAX_COLS * 2540 / ST_XDPI) / 2)

// ---- protocol -------------------------------------------------------------------
#define MAGIC 0xA5
enum { CMD_BAND = 1, CMD_FEED = 2, CMD_STATUS = 3, CMD_RESET = 4, RSP_STATUS = 0x83, RSP_ACK = 0x8F };
enum { ACK_OK = 0, ACK_CRC, ACK_INVALID, ACK_ERRSTATE };
enum { ST_IDLE = 0, ST_BUSY = 1, ST_ERROR = 2 };
enum { ERR_NONE = 0, ERR_CRC, ERR_HEAD_FAULT, ERR_HOME_TIMEOUT, ERR_PAPER_OUT, ERR_INVALID_BAND };
static const char *err_names[] = { "none", "crc", "head-fault", "home-timeout", "paper-out", "invalid-band" };
#define ERR_NAME(e) ((e) < sizeof err_names / sizeof err_names[0] ? err_names[e] : "unknown")

#define ACK_TIMEOUT_MS   10000  // longer than one band's print time (queue full = back-pressure)
#define IDLE_TIMEOUT_MS  120000 // a full queue of bands plus a feed and a re-home

static uint8_t crc8(const uint8_t *d, size_t n) {
  uint8_t c = 0;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1); }
  return c;
}

static int64_t now_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Write all of buf to fd; a CDC tty may accept a write in pieces.
static bool write_all(int fd, const uint8_t *buf, size_t n) {
  while (n > 0) {
    ssize_t w = write(fd, buf, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN) { struct pollfd p = { fd, POLLOUT, 0 }; if (poll(&p, 1, 1000) <= 0) return false; continue; }
      return false;
    }
    buf += w; n -= (size_t)w;
  }
  return true;
}

// Read exactly n bytes by the deadline (USB may split a reply across packets).
static bool read_exact(int fd, uint8_t *buf, size_t n, int64_t deadline) {
  while (n > 0) {
    int left = (int)(deadline - now_ms());
    if (left <= 0) return false;
    struct pollfd p = { fd, POLLIN, 0 };
    int pr = poll(&p, 1, left);
    if (pr < 0 && errno == EINTR) continue;
    if (pr <= 0) return false;
    ssize_t r = read(fd, buf, n);
    if (r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (r <= 0) return false;
    buf += r; n -= (size_t)r;
  }
  return true;
}

static bool fd_send_frame(int fd, uint8_t cmd, const uint8_t *payload, uint16_t len) {
  uint8_t buf[4 + 6 + ST_MAX_COLS + 1];
  if (len > sizeof buf - 5) return false;
  buf[0] = MAGIC; buf[1] = cmd; buf[2] = len & 0xFF; buf[3] = len >> 8;
  if (len) memcpy(buf + 4, payload, len);
  buf[4 + len] = crc8(buf + 1, 3 + len);
  return write_all(fd, buf, 4 + (size_t)len + 1);
}

// Read one reply frame (ACK or STATUS). Returns its cmd byte, or -1 on time-out.
// Bytes before a MAGIC, oversized lengths and bad CRCs are skipped (resync).
static int fd_read_frame(int fd, uint8_t *payload, uint16_t *len, int timeout_ms) {
  int64_t deadline = now_ms() + timeout_ms;
  uint8_t hdr[4], crc, chk[3 + 64];
  for (;;) {
    if (!read_exact(fd, hdr, 1, deadline)) return -1;
    if (hdr[0] != MAGIC) continue;
    if (!read_exact(fd, hdr + 1, 3, deadline)) return -1;
    *len = (uint16_t)(hdr[2] | (hdr[3] << 8));
    if (*len > 64) continue;
    if (!read_exact(fd, payload, *len, deadline) || !read_exact(fd, &crc, 1, deadline)) return -1;
    memcpy(chk, hdr + 1, 3); memcpy(chk + 3, payload, *len);
    if (crc8(chk, 3 + (size_t)*len) != crc) continue;
    return hdr[1];
  }
}

static int dev_fd(pappl_device_t *dev) { return (int)(intptr_t)papplDeviceGetData(dev); }

// Frames bypass papplDeviceWrite(): its buffer is only emptied by papplDeviceFlush(),
// and every frame here must reach the Pico before we wait for its reply.
static bool send_frame(pappl_device_t *dev, uint8_t cmd, const uint8_t *payload, uint16_t len) {
  return fd_send_frame(dev_fd(dev), cmd, payload, len);
}
static int read_frame(pappl_device_t *dev, uint8_t *payload, uint16_t *len, int timeout_ms) {
  return fd_read_frame(dev_fd(dev), payload, len, timeout_ms);
}

// STATUS: fills state/error. false = no valid reply.
static bool get_status(pappl_device_t *dev, uint8_t *state, uint8_t *error) {
  uint8_t p[64]; uint16_t len = 0;
  if (!send_frame(dev, CMD_STATUS, NULL, 0)) return false;
  if (read_frame(dev, p, &len, 1000) != RSP_STATUS || len < 2) return false;
  *state = p[0]; *error = p[1];
  return true;
}

static int open_port(const char *path, int extra_flags) {
  int fd = open(path, O_RDWR | O_NOCTTY | extra_flags);
  if (fd < 0) return -1;
  struct termios t;
  if (tcgetattr(fd, &t) == 0) { cfmakeraw(&t); t.c_cflag |= CLOCAL | CREAD; tcsetattr(fd, TCSANOW, &t); }
  tcflush(fd, TCIOFLUSH);                 // drop replies left from a previous session
  return fd;
}

// ---- serial:// device scheme -----------------------------------------------------

// On Linux, true if the tty's USB device is the Silentype-Pico (VID 1209, PID 0001),
// so listing never sends bytes to someone else's serial device. Elsewhere: true.
static bool is_silentype_usb(const char *path) {
#ifdef __linux__
  const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
  char f[256], vid[8] = "", pid[8] = "";
  FILE *fp;
  snprintf(f, sizeof f, "/sys/class/tty/%s/device/../idVendor", name);
  if ((fp = fopen(f, "r")) != NULL) { if (!fgets(vid, sizeof vid, fp)) vid[0] = 0; fclose(fp); }
  snprintf(f, sizeof f, "/sys/class/tty/%s/device/../idProduct", name);
  if ((fp = fopen(f, "r")) != NULL) { if (!fgets(pid, sizeof pid, fp)) pid[0] = 0; fclose(fp); }
  return !strncmp(vid, "1209", 4) && !strncmp(pid, "0001", 4);
#else
  (void)path;
  return true;
#endif
}

// Device-list callback: PAPPL's cb returns true to STOP listing; we return true if it did.
static bool st_serial_list(pappl_device_cb_t cb, void *data, pappl_deverror_cb_t err_cb, void *err_data) {
  glob_t g; memset(&g, 0, sizeof g);
  int flags = 0;
  if (glob("/dev/ttyACM*", flags, NULL, &g) == 0) flags = GLOB_APPEND;
  glob("/dev/cu.usbmodem*", flags, NULL, &g);

  bool stop = false;
  char uri[300];
  for (size_t i = 0; i < g.gl_pathc && !stop; i++) {
    const char *path = g.gl_pathv[i];
    if (!is_silentype_usb(path)) continue;
    // Probe with STATUS; report only ports that answer like a Silentype-Pico.
    int fd = open_port(path, O_NONBLOCK);
    if (fd < 0) {
      if (err_cb) { char msg[320]; snprintf(msg, sizeof msg, "Unable to open %s: %s", path, strerror(errno)); err_cb(msg, err_data); }
      continue;
    }
    uint8_t p[64]; uint16_t len = 0;
    bool ok = fd_send_frame(fd, CMD_STATUS, NULL, 0) && fd_read_frame(fd, p, &len, 500) == RSP_STATUS;
    close(fd);
    if (!ok) continue;
    snprintf(uri, sizeof uri, "serial://%s", path);
    stop = cb("Silentype-Pico", uri, "MFG:Apple;MDL:Silentype-Pico;CMD:SILENTYPEBAND;", data);
  }
  globfree(&g);
  return stop;
}

static bool st_serial_open(pappl_device_t *device, const char *device_uri, const char *name) {
  (void)name;
  if (strncmp(device_uri, "serial://", 9)) return false;
  int fd = open_port(device_uri + 9, 0);
  if (fd < 0) { papplDeviceError(device, "Unable to open %s: %s", device_uri + 9, strerror(errno)); return false; }
  papplDeviceSetData(device, (void *)(intptr_t)fd);
  return true;
}
static void st_serial_close(pappl_device_t *d) { close(dev_fd(d)); }
static ssize_t st_serial_read(pappl_device_t *d, void *b, size_t n) { return read(dev_fd(d), b, n); }
static ssize_t st_serial_write(pappl_device_t *d, const void *b, size_t n) {
  return write_all(dev_fd(d), b, n) ? (ssize_t)n : -1;
}
static pappl_preason_t st_serial_status(pappl_device_t *d) {
  uint8_t state, error;
  if (!get_status(d, &state, &error)) return PAPPL_PREASON_OTHER;
  if (state != ST_ERROR) return PAPPL_PREASON_NONE;
  return error == ERR_PAPER_OUT ? PAPPL_PREASON_MEDIA_EMPTY : PAPPL_PREASON_OTHER;
}

// ---- raster job ----------------------------------------------------------------
typedef struct {
  unsigned width;                      // columns sent per band (<= ST_MAX_COLS)
  unsigned x_offset;                   // raster pixels skipped on the left (centring)
  unsigned rows_in_band;
  uint8_t  rows[HEAD_DOTS][ST_MAX_LINE];   // 1-bit rows as PAPPL delivers them
  uint8_t  intensity;
} st_job_t;

// Wait for the ACK that echoes `cmd`. The firmware ACKs BAND and FEED when they are
// *queued*, and withholds the ACK while its queue is full (back-pressure), so allow a
// band's print time. Every command we send is ACKed, so an ACK echoing a different
// command means the stream is out of step: fail rather than mis-attribute it.
static bool expect_ack(pappl_job_t *job, pappl_device_t *dev, uint8_t cmd, int timeout_ms) {
  uint8_t ack[64]; uint16_t len = 0;
  int r = read_frame(dev, ack, &len, timeout_ms);
  if (r != RSP_ACK || len < 2) {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "No ACK from Pico for cmd 0x%02x (%s)", cmd, r < 0 ? "timeout" : "unexpected reply");
    return false;
  }
  if (ack[0] != cmd) {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "ACK out of step: expected cmd 0x%02x, got 0x%02x", cmd, ack[0]);
    return false;
  }
  if (ack[1] != ACK_OK) {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Pico rejected cmd 0x%02x: %s", cmd,
                ack[1] == ACK_ERRSTATE ? "printer in error state" : ack[1] == ACK_CRC ? "crc" : "invalid");
    return false;
  }
  return true;
}

// Wait until the printer is idle: homed, queue empty, nothing printing. A previous
// job's bands and page feed may still be printing when this job starts, so never
// RESET a busy printer (RESET discards its queue). RESET only to clear an error,
// then wait for the re-home to finish.
static bool wait_ready(pappl_job_t *job, pappl_device_t *dev) {
  int64_t deadline = now_ms() + IDLE_TIMEOUT_MS;
  bool reset_sent = false;
  while (now_ms() < deadline) {
    uint8_t state, error;
    if (!get_status(dev, &state, &error)) {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "No STATUS reply from Pico");
      return false;
    }
    if (state == ST_IDLE) return true;
    if (state == ST_ERROR) {
      if (reset_sent) {   // the RESET's re-home failed too
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Printer error after reset: %s", ERR_NAME(error));
        return false;
      }
      papplLogJob(job, PAPPL_LOGLEVEL_WARN, "Printer in error (%s); resetting", ERR_NAME(error));
      if (!send_frame(dev, CMD_RESET, NULL, 0) || !expect_ack(job, dev, CMD_RESET, 2000)) return false;
      reset_sent = true;
    }
    usleep(200 * 1000);   // busy (printing, feeding or homing): poll again
  }
  papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Printer still busy after %d s", IDLE_TIMEOUT_MS / 1000);
  return false;
}

static bool flush_band(pappl_job_t *job, pappl_device_t *dev, st_job_t *j) {
  if (j->rows_in_band == 0) return true;
  // Pad missing rows (end of page) with white.
  for (unsigned r = j->rows_in_band; r < HEAD_DOTS; r++) memset(j->rows[r], 0, sizeof j->rows[r]);
  uint8_t payload[6 + ST_MAX_COLS];
  payload[0] = j->width & 0xFF; payload[1] = (uint8_t)(j->width >> 8);
  payload[2] = j->intensity; payload[3] = ST_FEED_STEPS; payload[4] = 0; payload[5] = 0;
  // Column byte: bit 6 = TOP head element, bit 0 = BOTTOM, bit 7 unused (docs/protocol.md).
  // Raster row r of the band (0 = top) therefore goes to bit HEAD_DOTS-1-r.
  for (unsigned x = 0; x < j->width; x++) {
    unsigned px = x + j->x_offset;
    uint8_t col = 0;
    for (unsigned r = 0; r < HEAD_DOTS; r++)
      if (j->rows[r][px >> 3] & (0x80 >> (px & 7))) col |= (uint8_t)(1u << (HEAD_DOTS - 1 - r));   // black_1: 1 = black
    payload[6 + x] = col;
  }
  j->rows_in_band = 0;
  return send_frame(dev, CMD_BAND, payload, (uint16_t)(6 + j->width)) && expect_ack(job, dev, CMD_BAND, ACK_TIMEOUT_MS);
}

static bool st_rstartjob(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev) {
  (void)opt;
  st_job_t *j = calloc(1, sizeof *j);
  if (!j) return false;
  papplJobSetData(job, j);
  return wait_ready(job, dev);
}

static bool st_rstartpage(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev, unsigned page) {
  (void)dev; (void)page;
  st_job_t *j = papplJobGetData(job);
  unsigned w = opt->header.cupsWidth;
  if (w > ST_MAX_LINE * 8) w = ST_MAX_LINE * 8;
  // Centre the printable columns on the page (margins are already blank in the raster).
  j->width    = w > ST_MAX_COLS ? ST_MAX_COLS : w;
  j->x_offset = (w - j->width) / 2;
  j->rows_in_band = 0;
  // printer-darkness-configured (0..100) + print-darkness (-100..100) -> level 0..100
  // -> firmware intensity 0..7.
  int level = opt->darkness_configured + opt->print_darkness;
  if (level < 0) level = 0; else if (level > 100) level = 100;
  j->intensity = (uint8_t)((level * ST_MAX_INTENSITY + 50) / 100);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Page %u: %u x %u raster, %u columns from x=%u, intensity %u",
              page, opt->header.cupsWidth, opt->header.cupsHeight, j->width, j->x_offset, j->intensity);
  return true;
}

static bool st_rwriteline(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev, unsigned y, const unsigned char *line) {
  (void)y;
  st_job_t *j = papplJobGetData(job);
  size_t nbytes = opt->header.cupsBytesPerLine;
  if (nbytes > ST_MAX_LINE) nbytes = ST_MAX_LINE;
  memcpy(j->rows[j->rows_in_band], line, nbytes);
  if (nbytes < ST_MAX_LINE) memset(j->rows[j->rows_in_band] + nbytes, 0, ST_MAX_LINE - nbytes);
  if (++j->rows_in_band == HEAD_DOTS) return flush_band(job, dev, j);
  return true;
}

static bool st_rendpage(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev, unsigned page) {
  (void)opt; (void)page;
  st_job_t *j = papplJobGetData(job);
  if (!flush_band(job, dev, j)) return false;
  uint8_t feed[2] = { ST_PAGE_FEED & 0xFF, ST_PAGE_FEED >> 8 };
  // FEED is queued in order behind the page's bands and back-pressured like a band.
  return send_frame(dev, CMD_FEED, feed, 2) && expect_ack(job, dev, CMD_FEED, ACK_TIMEOUT_MS);
}

static bool st_rendjob(pappl_job_t *job, pappl_pr_options_t *opt, pappl_device_t *dev) {
  (void)opt; (void)dev;
  free(papplJobGetData(job)); papplJobSetData(job, NULL);
  return true;
}

static bool st_status(pappl_printer_t *printer) {
  pappl_device_t *dev = papplPrinterOpenDevice(printer);
  if (!dev) return true;          // busy with a job: keep the current reasons
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
  d->rendpage_cb  = st_rendpage;  d->rendjob_cb    = st_rendjob;    d->status_cb     = st_status;
  // No d->format: this driver takes raster only (a raw format needs a printfile_cb).
  d->orient_default = IPP_ORIENT_PORTRAIT;
  d->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1;           // PAPPL dithers; we just pack
  d->color_supported = PAPPL_COLOR_MODE_MONOCHROME; d->color_default = PAPPL_COLOR_MODE_MONOCHROME;
  d->num_resolution = 1; d->x_resolution[0] = ST_XDPI; d->y_resolution[0] = ST_YDPI;
  d->x_default = ST_XDPI; d->y_default = ST_YDPI;
  d->left_right = ST_SIDE_MARGIN; d->bottom_top = 0;
  // printer-darkness-supported = 8 levels; configured 14/100 -> intensity 1 by default.
  d->darkness_supported = ST_MAX_INTENSITY + 1;
  d->darkness_configured = (ST_DEFAULT_INTENSITY * 100 + ST_MAX_INTENSITY / 2) / ST_MAX_INTENSITY;
  d->darkness_default = 0;
  d->num_media = 1; d->media[0] = ST_MEDIA_NAME;
  papplCopyString(d->media_default.size_name, ST_MEDIA_NAME, sizeof d->media_default.size_name);
  d->media_default.size_width  = ST_MEDIA_WIDTH;
  d->media_default.size_length = ST_MEDIA_LENGTH;           // roll: length is nominal
  d->media_default.left_margin = d->media_default.right_margin = ST_SIDE_MARGIN;
  d->media_default.top_margin  = d->media_default.bottom_margin = 0;
  papplCopyString(d->media_default.source, "main-roll", sizeof d->media_default.source);
  papplCopyString(d->media_default.type, "continuous", sizeof d->media_default.type);
  d->media_ready[0] = d->media_default;
  d->num_source = 1; d->source[0] = "main-roll";
  d->num_type = 1;   d->type[0] = "continuous";
  return true;
}

static pappl_pr_driver_t drivers[] = {
  { "silentype", "Apple Silentype (Pico)", "MFG:Apple;MDL:Silentype-Pico;CMD:SILENTYPEBAND;", NULL },
};

int main(int argc, char *argv[]) {
  papplDeviceAddScheme("serial", PAPPL_DEVTYPE_CUSTOM_LOCAL, st_serial_list, st_serial_open,
                       st_serial_close, st_serial_read, st_serial_write, st_serial_status, /*id*/NULL);
  return papplMainloop(argc, argv, "0.3", NULL, (int)(sizeof drivers / sizeof drivers[0]), drivers,
                       /*autoadd_cb*/NULL, st_driver_cb, /*subcmd_name*/NULL, /*subcmd_cb*/NULL,
                       /*system_cb*/NULL, /*usage_cb*/NULL, /*data*/NULL);
}
