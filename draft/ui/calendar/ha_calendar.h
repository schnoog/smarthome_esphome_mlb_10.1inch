#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// HA calendar view for ESPHome + LVGL 9
// Month grid (own design, not lv_calendar) + agenda list, prev/next month.
// Events come from a Home Assistant template sensor as text lines:
//   cal_index|YYYY-MM-DD(start)|YYYY-MM-DD(end, inclusive)|HH:MM|HH:MM|title
// ─────────────────────────────────────────────────────────────────────────────
#include <lvgl.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace hacal {

// ── Look & feel ─────────────────────────────────────────────────────────────
static const uint32_t COL_PANEL = 0x111827;
static const uint32_t COL_CELL = 0x1F2937;
static const uint32_t COL_CELL_OUT = 0x141B26;
static const uint32_t COL_TEXT = 0xF8FAFC;
static const uint32_t COL_DIM = 0x6B7280;
static const uint32_t COL_WEEKEND = 0xFCA5A5;
static const uint32_t COL_TODAY = 0xF97316;
static const uint32_t COL_SELECT = 0x38BDF8;
static const uint32_t COL_BTN = 0x374151;
// one color per calendar (order = order of entities in the HA template)
static const uint32_t CAL_COLORS[6] = {0xF97316, 0x38BDF8, 0x22C55E, 0xE879F9, 0xFACC15, 0xF87171};
static const int MAX_DOTS = 3;
static const int MAX_AGENDA = 40;

static const char *const MONTHS[12] = {"Januar", "Februar", "März",      "April",   "Mai",      "Juni",
                                       "Juli",   "August",  "September", "Oktober", "November", "Dezember"};
static const char *const WD_SHORT[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
static const char *const WD_LONG[7] = {"Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"};

// ── Date helpers (dates as int yyyymmdd) ────────────────────────────────────
inline int ymd(int y, int m, int d) { return y * 10000 + m * 100 + d; }
inline int ymd_y(int v) { return v / 10000; }
inline int ymd_m(int v) { return (v / 100) % 100; }
inline int ymd_d(int v) { return v % 100; }
inline bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
inline int days_in_month(int y, int m) {
  static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}
// 0 = Monday ... 6 = Sunday
inline int weekday(int y, int m, int d) {
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;  // 0 = Sunday
  return (w + 6) % 7;
}
inline int parse_date(const std::string &s) {  // "YYYY-MM-DD"
  if (s.size() < 10) return 0;
  return ymd(atoi(s.substr(0, 4).c_str()), atoi(s.substr(5, 2).c_str()), atoi(s.substr(8, 2).c_str()));
}

struct Event {
  int cal;
  int start;
  int end;
  std::string t0, t1, title;
};

class CalendarView {
 public:
  void set_fonts(const lv_font_t *small, const lv_font_t *mid, const lv_font_t *big) {
    f_small_ = small;
    f_mid_ = mid;
    f_big_ = big;
  }

  // ── Build the UI once into an existing LVGL object ──────────────────────
  void build(lv_obj_t *parent) {
    if (built_ || parent == nullptr) return;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 10, 0);
    lv_obj_set_style_pad_column(parent, 14, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);

    // Month panel (left)
    lv_obj_t *mp = box_(parent);
    lv_obj_set_size(mp, LV_PCT(62), LV_PCT(100));
    lv_obj_set_flex_flow(mp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(mp, 6, 0);

    lv_obj_t *hd = box_(mp);
    lv_obj_set_size(hd, LV_PCT(100), 56);
    lv_obj_t *prev = round_btn_(hd, "<", -1);
    lv_obj_align(prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *next = round_btn_(hd, ">", 1);
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);
    title_ = label_(hd, f_big_, COL_TEXT);
    lv_obj_align(title_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(title_, LV_OBJ_FLAG_CLICKABLE);  // tap title = back to today
    lv_obj_set_user_data(title_, (void *) (intptr_t) 0);
    lv_obj_add_event_cb(title_, nav_cb_, LV_EVENT_CLICKED, this);

    static const int32_t COLS[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t ROW_WD[] = {28, LV_GRID_TEMPLATE_LAST};
    static const int32_t ROWS[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_t *wr = box_(mp);
    lv_obj_set_size(wr, LV_PCT(100), 28);
    lv_obj_set_grid_dsc_array(wr, COLS, ROW_WD);
    lv_obj_set_layout(wr, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(wr, 5, 0);
    for (int i = 0; i < 7; i++) {
      lv_obj_t *l = label_(wr, f_small_, i >= 5 ? COL_WEEKEND : COL_DIM);
      lv_label_set_text(l, WD_SHORT[i]);
      lv_obj_set_grid_cell(l, LV_GRID_ALIGN_CENTER, i, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    }

    lv_obj_t *gr = box_(mp);
    lv_obj_set_width(gr, LV_PCT(100));
    lv_obj_set_flex_grow(gr, 1);
    lv_obj_set_grid_dsc_array(gr, COLS, ROWS);
    lv_obj_set_layout(gr, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(gr, 5, 0);
    lv_obj_set_style_pad_row(gr, 5, 0);

    for (int i = 0; i < 42; i++) {
      lv_obj_t *c = box_(gr);
      lv_obj_set_grid_cell(c, LV_GRID_ALIGN_STRETCH, i % 7, 1, LV_GRID_ALIGN_STRETCH, i / 7, 1);
      lv_obj_set_style_radius(c, 10, 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(c, lv_color_hex(COL_CELL), 0);
      lv_obj_set_style_border_color(c, lv_color_hex(COL_SELECT), 0);
      lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_user_data(c, (void *) (intptr_t) i);
      lv_obj_add_event_cb(c, cell_cb_, LV_EVENT_CLICKED, this);
      cells_[i] = c;

      lv_obj_t *n = label_(c, f_mid_, COL_TEXT);
      lv_obj_set_style_radius(n, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(n, lv_color_hex(COL_TODAY), 0);
      lv_obj_set_style_pad_hor(n, 8, 0);
      lv_obj_set_style_pad_ver(n, 1, 0);
      lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 4);
      nums_[i] = n;

      lv_obj_t *dr = box_(c);
      lv_obj_set_size(dr, LV_SIZE_CONTENT, 8);
      lv_obj_set_flex_flow(dr, LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_column(dr, 4, 0);
      lv_obj_align(dr, LV_ALIGN_BOTTOM_MID, 0, -7);
      for (int k = 0; k < MAX_DOTS; k++) {
        lv_obj_t *d = box_(dr);
        lv_obj_set_size(d, 8, 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        dots_[i][k] = d;
      }
    }

    // Agenda panel (right)
    lv_obj_t *ap = box_(parent);
    lv_obj_set_height(ap, LV_PCT(100));
    lv_obj_set_flex_grow(ap, 1);
    lv_obj_set_style_bg_opa(ap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ap, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_radius(ap, 14, 0);
    lv_obj_set_style_pad_all(ap, 12, 0);
    lv_obj_set_style_pad_row(ap, 8, 0);
    lv_obj_set_flex_flow(ap, LV_FLEX_FLOW_COLUMN);

    agenda_title_ = label_(ap, f_mid_, COL_TEXT);
    lv_obj_set_width(agenda_title_, LV_PCT(100));

    agenda_list_ = box_(ap);
    lv_obj_set_width(agenda_list_, LV_PCT(100));
    lv_obj_set_flex_grow(agenda_list_, 1);
    lv_obj_set_flex_flow(agenda_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(agenda_list_, 6, 0);
    lv_obj_add_flag(agenda_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(agenda_list_, LV_DIR_VER);

    built_ = true;
    render();
  }

  // ── Data input ──────────────────────────────────────────────────────────
  void set_events(const std::string &raw) {
    events_.clear();
    size_t pos = 0;
    while (pos < raw.size()) {
      size_t nl = raw.find('\n', pos);
      if (nl == std::string::npos) nl = raw.size();
      std::string line = raw.substr(pos, nl - pos);
      pos = nl + 1;
      std::vector<std::string> f;
      size_t p = 0;
      while (f.size() < 5) {
        size_t bar = line.find('|', p);
        if (bar == std::string::npos) break;
        f.push_back(line.substr(p, bar - p));
        p = bar + 1;
      }
      if (f.size() < 5) continue;
      Event e;
      e.cal = atoi(f[0].c_str());
      e.start = parse_date(f[1]);
      e.end = parse_date(f[2]);
      if (e.start == 0) continue;
      if (e.end < e.start) e.end = e.start;
      e.t0 = f[3];
      e.t1 = f[4];
      e.title = line.substr(p);
      events_.push_back(e);
    }
    std::sort(events_.begin(), events_.end(), [](const Event &a, const Event &b) {
      if (a.start != b.start) return a.start < b.start;
      return a.t0 < b.t0;  // all-day ("") first
    });
    render();
  }

  void set_today(int y, int m, int d) {
    int t = ymd(y, m, d);
    if (t == today_) return;
    bool follow = today_ == 0 || (view_y_ == ymd_y(today_) && view_m_ == ymd_m(today_));
    today_ = t;
    if (follow) {
      view_y_ = y;
      view_m_ = m;
    }
    render();
  }

  // ── Navigation ──────────────────────────────────────────────────────────
  void shift_month(int delta) {
    if (view_y_ == 0) return;
    view_m_ += delta;
    while (view_m_ < 1) { view_m_ += 12; view_y_--; }
    while (view_m_ > 12) { view_m_ -= 12; view_y_++; }
    selected_ = 0;
    render();
  }
  void go_today() {
    if (today_ == 0) return;
    view_y_ = ymd_y(today_);
    view_m_ = ymd_m(today_);
    selected_ = 0;
    render();
  }
  void select_cell(int idx) {
    if (idx < 0 || idx >= 42) return;
    int d = cell_date_[idx];
    if (ymd_y(d) != view_y_ || ymd_m(d) != view_m_) {  // tap on neighbour month day
      view_y_ = ymd_y(d);
      view_m_ = ymd_m(d);
      selected_ = d;
    } else {
      selected_ = (selected_ == d) ? 0 : d;
    }
    render();
  }

  // ── Drawing ─────────────────────────────────────────────────────────────
  void render() {
    if (!built_ || view_y_ == 0) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d", MONTHS[view_m_ - 1], view_y_);
    lv_label_set_text(title_, buf);

    int first = weekday(view_y_, view_m_, 1);
    int dim = days_in_month(view_y_, view_m_);
    int py = view_m_ == 1 ? view_y_ - 1 : view_y_, pm = view_m_ == 1 ? 12 : view_m_ - 1;
    int ny = view_m_ == 12 ? view_y_ + 1 : view_y_, nm = view_m_ == 12 ? 1 : view_m_ + 1;
    int pdim = days_in_month(py, pm);

    for (int i = 0; i < 42; i++) {
      int dn = i - first + 1;
      int date;
      bool in = true;
      if (dn < 1) {
        date = ymd(py, pm, pdim + dn);
        in = false;
      } else if (dn > dim) {
        date = ymd(ny, nm, dn - dim);
        in = false;
      } else {
        date = ymd(view_y_, view_m_, dn);
      }
      cell_date_[i] = date;

      snprintf(buf, sizeof(buf), "%d", ymd_d(date));
      lv_label_set_text(nums_[i], buf);
      bool today = date == today_;
      uint32_t tc = !in ? COL_DIM : (today ? COL_TEXT : ((i % 7) >= 5 ? COL_WEEKEND : COL_TEXT));
      lv_obj_set_style_text_color(nums_[i], lv_color_hex(tc), 0);
      lv_obj_set_style_bg_opa(nums_[i], today ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(cells_[i], lv_color_hex(in ? COL_CELL : COL_CELL_OUT), 0);
      lv_obj_set_style_border_width(cells_[i], (selected_ != 0 && date == selected_) ? 2 : 0, 0);

      uint32_t cols[MAX_DOTS];
      int n = 0;
      day_colors_(date, cols, n);
      for (int k = 0; k < MAX_DOTS; k++) {
        if (k < n) {
          lv_obj_set_style_bg_color(dots_[i][k], lv_color_hex(cols[k]), 0);
          lv_obj_set_style_bg_opa(dots_[i][k], in ? LV_OPA_COVER : LV_OPA_50, 0);
          lv_obj_remove_flag(dots_[i][k], LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(dots_[i][k], LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    render_agenda_();
  }

 protected:
  static lv_obj_t *box_(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
  }
  lv_obj_t *label_(lv_obj_t *p, const lv_font_t *f, uint32_t col) {
    lv_obj_t *l = lv_label_create(p);
    if (f != nullptr) lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    return l;
  }
  lv_obj_t *round_btn_(lv_obj_t *p, const char *txt, intptr_t code) {
    lv_obj_t *b = box_(p);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_BTN), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_TODAY), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, (void *) code);
    lv_obj_add_event_cb(b, nav_cb_, LV_EVENT_CLICKED, this);
    lv_obj_t *l = label_(b, f_big_, COL_TEXT);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
  }
  static void nav_cb_(lv_event_t *e) {
    auto *self = static_cast<CalendarView *>(lv_event_get_user_data(e));
    auto *t = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    int code = (int) (intptr_t) lv_obj_get_user_data(t);
    if (code == 0) self->go_today();
    else self->shift_month(code);
  }
  static void cell_cb_(lv_event_t *e) {
    auto *self = static_cast<CalendarView *>(lv_event_get_user_data(e));
    auto *t = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    self->select_cell((int) (intptr_t) lv_obj_get_user_data(t));
  }

  void day_colors_(int date, uint32_t *out, int &n) {
    n = 0;
    for (const auto &e : events_) {
      if (e.start > date) break;  // sorted by start
      if (e.end < date) continue;
      uint32_t c = CAL_COLORS[(e.cal < 0 ? 0 : e.cal) % 6];
      bool dup = false;
      for (int k = 0; k < n; k++) dup |= out[k] == c;
      if (!dup) out[n++] = c;
      if (n >= MAX_DOTS) return;
    }
  }

  void render_agenda_() {
    char buf[64];
    int m0 = ymd(view_y_, view_m_, 1), m1 = ymd(view_y_, view_m_, days_in_month(view_y_, view_m_));
    bool current_month = today_ != 0 && ymd_y(today_) == view_y_ && ymd_m(today_) == view_m_;
    int from = m0, to = m1;
    if (selected_ != 0) {
      from = to = selected_;
      int wd = weekday(ymd_y(selected_), ymd_m(selected_), ymd_d(selected_));
      snprintf(buf, sizeof(buf), "%s, %d. %s", WD_LONG[wd], ymd_d(selected_), MONTHS[ymd_m(selected_) - 1]);
    } else if (current_month) {
      from = today_;
      snprintf(buf, sizeof(buf), "Kommende Termine");
    } else {
      snprintf(buf, sizeof(buf), "Termine im %s", MONTHS[view_m_ - 1]);
    }
    lv_label_set_text(agenda_title_, buf);

    lv_obj_clean(agenda_list_);
    int count = 0;
    for (const auto &e : events_) {
      if (e.start > to) break;
      if (e.end < from) continue;
      if (count++ >= MAX_AGENDA) break;

      lv_obj_t *row = box_(agenda_list_);
      lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(row, lv_color_hex(COL_CELL), 0);
      lv_obj_set_style_radius(row, 10, 0);
      lv_obj_set_style_pad_all(row, 8, 0);
      lv_obj_set_style_pad_column(row, 10, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t *bar = box_(row);
      lv_obj_set_size(bar, 5, 40);
      lv_obj_set_style_radius(bar, 3, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(bar, lv_color_hex(CAL_COLORS[(e.cal < 0 ? 0 : e.cal) % 6]), 0);

      lv_obj_t *col = box_(row);
      lv_obj_set_height(col, LV_SIZE_CONTENT);
      lv_obj_set_flex_grow(col, 1);
      lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(col, 2, 0);

      // when
      std::string when;
      if (selected_ == 0) {
        int wd = weekday(ymd_y(e.start), ymd_m(e.start), ymd_d(e.start));
        snprintf(buf, sizeof(buf), "%s %d.%d.", WD_SHORT[wd], ymd_d(e.start), ymd_m(e.start));
        when = buf;
        if (e.end != e.start) {
          snprintf(buf, sizeof(buf), " - %d.%d.", ymd_d(e.end), ymd_m(e.end));
          when += buf;
        }
        when += "  ";
      }
      if (e.t0.empty()) when += "ganztägig";
      else when += e.t0 + (e.t1.empty() ? "" : " - " + e.t1);

      lv_obj_t *lw = label_(col, f_small_, COL_DIM);
      lv_label_set_text(lw, when.c_str());
      lv_obj_t *lt = label_(col, f_mid_, COL_TEXT);
      lv_obj_set_width(lt, LV_PCT(100));
      lv_label_set_text(lt, e.title.c_str());
    }
    if (count == 0) {
      lv_obj_t *l = label_(agenda_list_, f_mid_, COL_DIM);
      lv_label_set_text(l, "Keine Termine");
    }
  }

  const lv_font_t *f_small_{nullptr}, *f_mid_{nullptr}, *f_big_{nullptr};
  lv_obj_t *title_{nullptr}, *agenda_title_{nullptr}, *agenda_list_{nullptr};
  lv_obj_t *cells_[42]{}, *nums_[42]{}, *dots_[42][MAX_DOTS]{};
  int cell_date_[42]{};
  int view_y_{0}, view_m_{0}, today_{0}, selected_{0};
  bool built_{false};
  std::vector<Event> events_;
};

}  // namespace hacal

// single global instance used by calendar.yaml
static hacal::CalendarView ha_calendar;
