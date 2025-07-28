#pragma once
#include <stdint.h>

typedef struct {
    uint16_t l, t, r, b;
} UIBox;

void uibox_draw_border(UIBox box, int tb, int lr, int corner);
UIBox uibox_inner(UIBox box);
UIBox uibox_chop_left(UIBox* box, uint16_t n);
UIBox uibox_chop_bottom(UIBox* box, uint16_t n);
