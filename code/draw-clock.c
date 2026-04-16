#include <math.h>
#include <time.h>
#include "draw-clock.h"

static
void
draw_line(cairo_t *cr, double r1, double r2, double width, double deg) {
  cairo_save(cr);
  double rad = deg * M_PI / 180.0;
  cairo_rotate(cr, rad);
  cairo_set_line_width(cr, width);
  cairo_move_to(cr, 0, -r1);
  cairo_line_to(cr, 0, -r2);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void
draw_clock(cairo_surface_t *surface) {
  int width = cairo_image_surface_get_width(surface);
  int height = cairo_image_surface_get_height(surface);
  int size = (width < height) ? width : height;

  cairo_t *cr = cairo_create(surface);

  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);

  cairo_translate(cr, width/2.0, height/2.0);
  cairo_scale(cr, size/2.0, size/2.0);

  cairo_set_source_rgb(cr, 0, 0, 0);

  cairo_set_line_width(cr, 0.02);
  cairo_arc(cr, 0, 0, 0.9, 0, 2 * M_PI);
  cairo_stroke(cr);

  cairo_arc(cr, 0, 0, 0.04, 0, 2 * M_PI);
  cairo_fill(cr);

  for (int i = 0; i < 60; i++)
    draw_line(cr, 0.85, 0.9, 0.01, i * 6);

  for (int i = 0; i < 12; i++)
    draw_line(cr, 0.80, 0.9, 0.02, i * 30);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int h = t->tm_hour % 12;
  int m = t->tm_min;
  int s = t->tm_sec;

  m = m * 60 + s;
  h = h * 3600 + m;

  draw_line(cr, -0.09, 0.45, 0.03, h / 120.0);
  draw_line(cr, -0.12, 0.60, 0.02, m / 10.0);
  draw_line(cr, -0.15, 0.75, 0.01, s * 6.0);

  cairo_destroy(cr);
}

int
__attribute__((weak))
main() {
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 400);
  draw_clock(surface);
  cairo_surface_write_to_png(surface, "clock.png");
  cairo_surface_destroy(surface);
  return 0;
}
