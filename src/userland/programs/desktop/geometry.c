#include "desktop.h"

int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

rect_t make_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    rect_t r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

bool rect_valid(rect_t r) {
    return r.w > 0 && r.h > 0;
}

rect_t rect_expand(rect_t r, int32_t pad_x, int32_t pad_y) {
    r.x -= pad_x;
    r.y -= pad_y;
    r.w += pad_x * 2;
    r.h += pad_y * 2;
    return r;
}

bool rect_contains(rect_t outer, rect_t inner) {
    if (!rect_valid(outer) || !rect_valid(inner)) return false;
    if (inner.x < outer.x) return false;
    if (inner.y < outer.y) return false;
    if (inner.x + inner.w > outer.x + outer.w) return false;
    if (inner.y + inner.h > outer.y + outer.h) return false;
    return true;
}

bool point_in_rect(int32_t x, int32_t y, rect_t r) {
    if (!rect_valid(r)) return false;
    return (x >= r.x) && (y >= r.y) && (x < r.x + r.w) && (y < r.y + r.h);
}

bool rect_intersects(rect_t a, rect_t b) {
    if (!rect_valid(a) || !rect_valid(b)) return false;
    if (a.x + a.w <= b.x) return false;
    if (b.x + b.w <= a.x) return false;
    if (a.y + a.h <= b.y) return false;
    if (b.y + b.h <= a.y) return false;
    return true;
}

rect_t rect_intersection(rect_t a, rect_t b) {
    rect_t out;
    int32_t x1 = (a.x > b.x) ? a.x : b.x;
    int32_t y1 = (a.y > b.y) ? a.y : b.y;
    int32_t x2 = (a.x + a.w < b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y2 = (a.y + a.h < b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    out.x = x1;
    out.y = y1;
    out.w = x2 - x1;
    out.h = y2 - y1;
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

rect_t rect_union(rect_t a, rect_t b) {
    if (!rect_valid(a)) return b;
    if (!rect_valid(b)) return a;

    int32_t x1 = (a.x < b.x) ? a.x : b.x;
    int32_t y1 = (a.y < b.y) ? a.y : b.y;
    int32_t x2 = (a.x + a.w > b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y2 = (a.y + a.h > b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return make_rect(x1, y1, x2 - x1, y2 - y1);
}

rect_t clip_to_fb(const gui_fb_info_t* fb, rect_t r) {
    rect_t fb_rect = make_rect(0, 0, (int32_t)fb->width, (int32_t)fb->height);
    return rect_intersection(r, fb_rect);
}

rect_t desktop_top_rect(const gui_fb_info_t* fb) {
    return make_rect(0, 0, (int32_t)fb->width, DESKTOP_TOP_H);
}

rect_t desktop_side_rect(const gui_fb_info_t* fb) {
    int32_t h = (int32_t)fb->height - DESKTOP_TOP_H;
    if (h < 0) h = 0;
    return make_rect(0, DESKTOP_TOP_H, DESKTOP_SIDE_W, h);
}

rect_t icon_rect(uint32_t index) {
    return make_rect(ICON_X, ICON_Y0 + (int32_t)index * (ICON_H + ICON_GAP), ICON_W, ICON_H);
}

bool point_in_fb(const gui_fb_info_t* fb, int32_t x, int32_t y) {
    if (!fb) return false;
    if (x < 0 || y < 0) return false;
    if (x >= (int32_t)fb->width) return false;
    if (y >= (int32_t)fb->height) return false;
    return true;
}
