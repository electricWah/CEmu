#ifndef ASIC_H
#define ASIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    TI84PCE = 0,
    TI83PCE = 1
} ti_device_t;

typedef enum {
    ASIC_REV_AUTO = 0, /* Used only with set_asic_revision() */
    ASIC_REV_A = 1,
    ASIC_REV_I = 2,
    ASIC_REV_M = 3
} asic_rev_t;

typedef struct asic_state {
    ti_device_t device;
    asic_rev_t auto_revision; /* The revision to use for ASIC_REV_AUTO */
    /* The following are only updated on reset */
    asic_rev_t revision;
    bool im2;
    bool serFlash;
} asic_state_t;

extern asic_state_t asic;

void asic_init(void);
void asic_free(void);
void asic_reset(void);
bool asic_restore(FILE *image);
bool asic_save(FILE *image);
void set_cpu_clock(uint32_t new_rate);
void set_device_type(ti_device_t device);
void set_asic_revision(asic_rev_t revision);
void set_asic_auto_revision(asic_rev_t revision);
ti_device_t get_device_type(void);
asic_rev_t get_asic_revision(void);

#ifdef __cplusplus
}
#endif

#endif
