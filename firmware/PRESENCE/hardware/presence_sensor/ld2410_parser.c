#include "ld2410_parser.h"
#include <string.h>

static uint16_t read_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void discard(ld2410_parser_t *p, size_t count) {
  p->used -= count;
  memmove(p->buffer, p->buffer + count, p->used);
}

bool ld2410_parser_feed(ld2410_parser_t *p, uint8_t byte,
                        ld2410_report_t *report) {
  static const uint8_t header[] = {0xF4, 0xF3, 0xF2, 0xF1};
  static const uint8_t footer[] = {0xF8, 0xF7, 0xF6, 0xF5};
  if (!p || !report) return false;
  if (p->used == sizeof(p->buffer)) discard(p, 1);
  p->buffer[p->used++] = byte;
  while (p->used >= 4) {
    if (memcmp(p->buffer, header, sizeof(header)) != 0) {
      discard(p, 1);
      continue;
    }
    if (p->used < 6) return false;
    size_t length = read_le16(p->buffer + 4);
    if (length < 13 || length > sizeof(p->buffer) - 10) {
      discard(p, 1);
      continue;
    }
    size_t total = length + 10;
    if (p->used < total) return false;
    const uint8_t *d = p->buffer + 6;
    bool valid = ((d[0] == 2 && length == 13) ||
                  (d[0] == 1 && length >= 17)) &&
                 d[1] == 0xAA && d[2] <= 3 &&
                 d[length - 2] == 0x55 && d[length - 1] == 0 &&
                 memcmp(p->buffer + 6 + length, footer, 4) == 0;
    if (valid && d[0] == 1) {
      /* Engineering reports append gate counts and their energy arrays. */
      valid = d[11] <= 8 && d[12] <= 8 &&
              length >= (size_t)(17 + d[11] + d[12]);
    }
    if (!valid) {
      discard(p, 1);
      continue;
    }
    *report = (ld2410_report_t){
        .target_state = d[2],
        .moving_distance_cm = read_le16(d + 3),
        .moving_energy = d[5],
        .stationary_distance_cm = read_le16(d + 6),
        .stationary_energy = d[8],
        .detection_distance_cm = read_le16(d + 9),
        .engineering_mode = d[0] == 1,
    };
    discard(p, total);
    return true;
  }
  return false;
}
