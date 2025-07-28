#include "stui.h"
#include "uibox.h"

void uibox_draw_border(UIBox box, int tb, int lr, int corner) {
    stui_window_border(box.l, box.t, box.r - box.l, box.b - box.t, tb, lr, corner);
}
// The box that represents the content that would go within the border
UIBox uibox_inner(UIBox box) {
    return (UIBox){box.l + 1, box.t + 1, box.r - 1, box.b - 1};
}
UIBox uibox_chop_left(UIBox* box, uint16_t n) {
    uint16_t l = box->l;
    box->l += n;
    return (UIBox){l, box->t, box->l, box->b };
}
UIBox uibox_chop_bottom(UIBox* box, uint16_t n) {
    uint16_t b = box->b;
    box->b -= n;
    return (UIBox){box->l, box->b, box->r, b };
}
