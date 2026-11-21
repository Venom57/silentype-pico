// Drives silentype-app.c's real device and raster callbacks against a fake Pico
// on a pseudo-terminal (fake_pico.py). Usage: test_app SCENARIO TTY
#define main silentype_app_main
#include "../silentype-app.c"
#undef main

struct _pappl_device_s  { void *data; };
struct _pappl_job_s     { void *data; };
struct _pappl_printer_s { pappl_device_t *dev; pappl_preason_t reasons; };

#define PAGE_W   637          // 8.5 in at 75 dpi
#define OFFSET   ((PAGE_W - ST_MAX_COLS) / 2)

// Band row r is black where (column % 8) == r, measured from the printable left edge.
static void make_line(unsigned char *line, unsigned r) {
  memset(line, 0, (PAGE_W + 7) / 8);
  for (unsigned x = 0; x < ST_MAX_COLS; x++)
    if (x % 8 == r) { unsigned px = x + OFFSET; line[px >> 3] |= (unsigned char)(0x80 >> (px & 7)); }
}

static int print_page(pappl_device_t *dev, unsigned rows, int darkness) {
  pappl_job_t job = { 0 };
  pappl_pr_options_t opt; memset(&opt, 0, sizeof opt);
  opt.header.cupsWidth = PAGE_W; opt.header.cupsHeight = rows; opt.header.cupsBytesPerLine = (PAGE_W + 7) / 8;
  opt.darkness_configured = 14; opt.print_darkness = darkness;
  unsigned char line[(PAGE_W + 7) / 8];
  if (!st_rstartjob(&job, &opt, dev)) { fprintf(stderr, "  startjob failed\n"); st_rendjob(&job, &opt, dev); return 2; }
  if (!st_rstartpage(&job, &opt, dev, 1)) return 3;
  for (unsigned y = 0; y < rows; y++) {
    make_line(line, y % HEAD_DOTS);
    if (!st_rwriteline(&job, &opt, dev, y, line)) { fprintf(stderr, "  writeline %u failed\n", y); st_rendjob(&job, &opt, dev); return 4; }
  }
  if (!st_rendpage(&job, &opt, dev, 1)) { fprintf(stderr, "  endpage failed\n"); st_rendjob(&job, &opt, dev); return 5; }
  st_rendjob(&job, &opt, dev);
  return 0;
}

static int darkness_table(void) {
  // darkness_configured as the driver sets it, print-darkness -100..100 -> intensity 0..7
  pappl_pr_driver_data_t d; memset(&d, 0, sizeof d);
  if (!st_driver_cb(NULL, "silentype", "", "", &d, NULL, NULL)) return 1;
  struct { int dk; unsigned want; } t[] = { { -100, 0 }, { 0, 1 }, { 30, 3 }, { 86, 7 }, { 100, 7 } };
  int bad = 0;
  for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
    pappl_job_t job = { 0 }; st_job_t j; memset(&j, 0, sizeof j); job.data = &j;
    pappl_pr_options_t opt; memset(&opt, 0, sizeof opt);
    opt.header.cupsWidth = PAGE_W; opt.darkness_configured = d.darkness_configured; opt.print_darkness = t[i].dk;
    st_rstartpage(&job, &opt, NULL, 1);
    if (j.intensity != t[i].want) { fprintf(stderr, "  darkness %d -> %u, want %u\n", t[i].dk, j.intensity, t[i].want); bad = 1; }
    if (j.width != ST_MAX_COLS || j.x_offset != OFFSET) { fprintf(stderr, "  width %u offset %u\n", j.width, j.x_offset); bad = 1; }
  }
  if (d.media_default.size_width != 21590 || d.left_right != d.media_default.left_margin ||
      d.x_default != 75 || d.y_default != 40 || d.darkness_supported != 8) { fprintf(stderr, "  driver data wrong\n"); bad = 1; }
  fprintf(stderr, "  margins %d (1/100 mm), darkness_configured %d\n", d.left_right, d.darkness_configured);
  return bad;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "darkness")) return darkness_table();
  if (argc < 3) { fprintf(stderr, "usage: test_app SCENARIO TTY\n"); return 64; }
  char uri[300]; snprintf(uri, sizeof uri, "serial://%s", argv[2]);
  pappl_device_t dev = { 0 };
  if (!st_serial_open(&dev, uri, "test")) return 10;
  int rc;
  if (!strcmp(argv[1], "status")) {
    pappl_printer_t pr = { &dev, 0 };
    st_status(&pr);
    printf("reasons=0x%x\n", (unsigned)pr.reasons);
    rc = 0;
  } else {
    rc = print_page(&dev, !strcmp(argv[1], "tall") ? 21 : 10, 0);
  }
  st_serial_close(&dev);
  return rc;
}
