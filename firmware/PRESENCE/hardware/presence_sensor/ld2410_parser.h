#ifndef LD2410_PARSER_H
#define LD2410_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t target_state; /* 0: empty, 1: moving, 2: stationary, 3: both */
  uint16_t moving_distance_cm;
  uint16_t stationary_distance_cm;
  uint16_t detection_distance_cm;
  uint8_t moving_energy;
  uint8_t stationary_energy;
  bool engineering_mode;
} ld2410_report_t;

typedef struct {
  uint8_t buffer[128];
  size_t used;
} ld2410_parser_t;

/* Feed arbitrary UART chunks byte by byte. Returns true for a valid report. */
bool ld2410_parser_feed(ld2410_parser_t *parser, uint8_t byte,
                        ld2410_report_t *report);

#endif
