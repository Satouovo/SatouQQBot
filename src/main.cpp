#include "config.hpp"
#include "duration.hpp"
#include "runner.hpp"
#include "store.hpp"
#include "util.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr UINT kLogMessage = WM_APP + 1;
constexpr UINT kStatusMessage = WM_APP + 2;
constexpr int kUrl = 101;
constexpr int kToken = 102;
constexpr int kSuper = 103;
constexpr int kStart = 104;
constexpr int kSave = 105;
constexpr int kLog = 106;
constexpr int kStatus = 107;
constexpr int kTitle = 108;
constexpr int kHint = 109;
constexpr int kIcon = 110;
constexpr int kTabs = 120;
constexpr int kGroupList = 201;
constexpr int kGroupId = 202;
constexpr int kAddGroup = 203;
constexpr int kRemoveGroup = 204;
constexpr int kSaveGroup = 205;
constexpr int kWelcomeCheck = 206;
constexpr int kWelcomeText = 207;
constexpr int kLeaveCheck = 208;
constexpr int kWords = 209;
constexpr int kWordMute = 210;
constexpr int kSpamCheck = 211;
constexpr int kSpamLimit = 212;
constexpr int kSpamWindow = 213;
constexpr int kSpamMute = 214;
constexpr int kDefaultMute = 215;
constexpr int kGroupCaption = 216;
constexpr int kApproveCheck = 217;
constexpr int kRejectUnmatched = 218;
constexpr int kApproveWords = 219;
constexpr int kRejectWords = 220;
constexpr int kBlacklist = 221;
constexpr int kRejectReason = 222;
constexpr int kSectionWelcome = 223;
constexpr int kSectionOrder = 224;
constexpr int kSectionApprove = 225;

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_url = nullptr;
HWND g_token = nullptr;
HWND g_super = nullptr;
HWND g_start = nullptr;
HWND g_save = nullptr;
HWND g_log = nullptr;
HWND g_status = nullptr;
HWND g_title = nullptr;
HWND g_hint = nullptr;
HWND g_icon = nullptr;
HWND g_label_url = nullptr;
HWND g_label_token = nullptr;
HWND g_label_super = nullptr;
HWND g_tabs = nullptr;
HWND g_group_list = nullptr;
HWND g_group_id = nullptr;
HWND g_add_group = nullptr;
HWND g_remove_group = nullptr;
HWND g_save_group = nullptr;
HWND g_welcome_check = nullptr;
HWND g_welcome_text = nullptr;
HWND g_leave_check = nullptr;
HWND g_words = nullptr;
HWND g_word_mute = nullptr;
HWND g_spam_check = nullptr;
HWND g_spam_limit = nullptr;
HWND g_spam_window = nullptr;
HWND g_spam_mute = nullptr;
HWND g_default_mute = nullptr;
HWND g_group_caption = nullptr;
HWND g_label_welcome = nullptr;
HWND g_label_words = nullptr;
HWND g_label_word_mute = nullptr;
HWND g_label_default_mute = nullptr;
HWND g_label_spam_count = nullptr;
HWND g_label_spam_window = nullptr;
HWND g_label_spam_mute = nullptr;
HWND g_form = nullptr;
HWND g_approve_check = nullptr;
HWND g_reject_unmatched = nullptr;
HWND g_approve_words = nullptr;
HWND g_reject_words = nullptr;
HWND g_blacklist = nullptr;
HWND g_reject_reason = nullptr;
HWND g_section_welcome = nullptr;
HWND g_section_order = nullptr;
HWND g_section_approve = nullptr;
HWND g_label_approve = nullptr;
HWND g_label_reject = nullptr;
HWND g_label_blacklist = nullptr;
HWND g_label_reason = nullptr;
std::map<int, bool> g_on;
std::vector<RECT> g_main_cards;
std::vector<RECT> g_form_cards;
RECT g_segment{};
int g_scroll = 0;
int g_form_content = 0;
ULONG_PTR g_gdiplus = 0;
std::vector<HWND> g_connect_controls;
std::vector<HWND> g_group_controls;
std::vector<std::string> g_group_ids;
std::string g_selected = "*";
int g_page = 0;
std::unique_ptr<satou::GroupStore> g_groups;
HFONT g_font = nullptr;
HFONT g_title_font = nullptr;
HBRUSH g_background = nullptr;
HBRUSH g_card_brush = nullptr;
HBRUSH g_field_brush = nullptr;
int g_dpi = 96;
std::atomic<bool> g_stop{false};
std::thread g_worker;
bool g_running = false;
satou::AppConfig g_config;
std::filesystem::path g_config_path;

int scale(int px) { return MulDiv(px, g_dpi, 96); }

constexpr COLORREF kBg = RGB(242, 242, 247);
constexpr COLORREF kInk = RGB(28, 28, 30);
constexpr COLORREF kSecondary = RGB(142, 142, 147);
constexpr COLORREF kBlue = RGB(0, 122, 255);
constexpr COLORREF kField = RGB(242, 242, 247);
constexpr COLORREF kGreen = RGB(52, 199, 89);

LRESULT CALLBACK wnd_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

bool is_switch(int id) {
  return id == kWelcomeCheck || id == kLeaveCheck || id == kSpamCheck || id == kApproveCheck || id == kRejectUnmatched;
}

std::wstring utf8_to_wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring wide(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
  return wide;
}

std::string wide_to_utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string utf8(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::wstring window_text(HWND control) {
  const int length = GetWindowTextLengthW(control);
  if (length <= 0) return {};
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(control, text.data(), length + 1);
  text.resize(static_cast<std::size_t>(length));
  return text;
}

void post_text(HWND window, UINT message, const std::string& text) {
  auto* copy = new std::string(text);
  if (!PostMessageW(window, message, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

void append_log(const std::wstring& line) {
  if (!IsWindow(g_log)) return;
  const int length = GetWindowTextLengthW(g_log);
  if (length > 48000) {
    SendMessageW(g_log, EM_SETSEL, 0, 8000);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
  }
  const int end = GetWindowTextLengthW(g_log);
  SendMessageW(g_log, EM_SETSEL, end, end);
  const std::wstring text = line + L"\r\n";
  SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
  SendMessageW(g_log, EM_SCROLLCARET, 0, 0);
}

void set_status(const std::wstring& text) {
  if (IsWindow(g_status)) SetWindowTextW(g_status, text.c_str());
}

std::filesystem::path exe_directory() {
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return std::filesystem::current_path();
  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path config_from_args() {
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::filesystem::path path = exe_directory() / "config.json";
  if (argv && argc > 1 && argv[1][0] != L'\0') path = argv[1];
  if (argv) LocalFree(argv);
  return path;
}

std::vector<std::string> parse_superusers(const std::string& text) {
  std::vector<std::string> ids;
  std::string current;
  auto flush = [&] {
    if (current.size() >= 5) ids.push_back(current);
    current.clear();
  };
  for (unsigned char ch : text) {
    if (std::isdigit(ch)) current.push_back(static_cast<char>(ch));
    else flush();
  }
  flush();
  return ids;
}

std::wstring join_superusers(const std::vector<std::string>& ids) {
  std::wstring text;
  for (const auto& id : ids) {
    if (!text.empty()) text += L" ";
    text += utf8_to_wide(id);
  }
  return text;
}

void apply_fields_to_config() {
  const std::string url = satou::trim_copy(wide_to_utf8(window_text(g_url)));
  g_config.napcat_url = url.empty() ? "ws://127.0.0.1:3011" : url;
  g_config.access_token = satou::trim_copy(wide_to_utf8(window_text(g_token)));
  g_config.superusers = parse_superusers(wide_to_utf8(window_text(g_super)));
}

void fill_fields() {
  SetWindowTextW(g_url, utf8_to_wide(g_config.napcat_url).c_str());
  SetWindowTextW(g_token, utf8_to_wide(g_config.access_token).c_str());
  SetWindowTextW(g_super, join_superusers(g_config.superusers).c_str());
}

bool save_fields() {
  try {
    apply_fields_to_config();
    satou::save_config(g_config_path, g_config);
    append_log(L"已保存 " + g_config_path.wstring());
    return true;
  } catch (const std::exception& ex) {
    append_log(utf8_to_wide(ex.what()));
    set_status(L"保存失败");
    return false;
  }
}

void stop_worker() {
  g_stop.store(true);
  if (g_worker.joinable()) g_worker.join();
  g_stop.store(false);
  g_running = false;
  if (IsWindow(g_start)) SetWindowTextW(g_start, L"启动");
  set_status(L"已停止");
}

void start_worker() {
  if (g_running) return;
  if (!save_fields()) return;
  g_stop.store(false);
  g_running = true;
  SetWindowTextW(g_start, L"停止");
  set_status(L"正在连接");
  if (!g_groups) {
    append_log(L"群配置无法读取，先不要启动。");
    set_status(L"群配置不可用");
    return;
  }
  const satou::AppConfig config = g_config;
  satou::GroupStore* store = g_groups.get();
  const HWND window = g_window;
  g_worker = std::thread([window, config, store] {
    satou::set_log_sink([window](const std::string& line) { post_text(window, kLogMessage, line); });
    try {
      satou::run_bot(config, *store, g_stop, [window](const std::string& text) { post_text(window, kStatusMessage, text); });
    } catch (const std::exception& ex) {
      satou::log_info(std::string("启动失败：") + ex.what());
    }
    satou::set_log_sink({});
    post_text(window, kStatusMessage, "已停止");
  });
}

HWND add_label(HWND parent, const wchar_t* text, HFONT font, bool visible) {
  const DWORD style = WS_CHILD | (visible ? WS_VISIBLE : 0);
  HWND control = CreateWindowExW(0, L"STATIC", text, style, 0, 0, 10, 10, parent, nullptr, g_instance, nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

bool box_checked(HWND box) {
  const auto found = g_on.find(GetDlgCtrlID(box));
  return found != g_on.end() && found->second;
}

void set_checked(HWND box, bool enabled) {
  g_on[GetDlgCtrlID(box)] = enabled;
  InvalidateRect(box, nullptr, FALSE);
}

std::vector<std::string> parse_word_lines(const std::string& text) {
  std::vector<std::string> words;
  std::string current;
  auto flush = [&] {
    const std::string word = satou::trim_copy(current);
    current.clear();
    if (word.empty()) return;
    if (std::find(words.begin(), words.end(), word) == words.end()) words.push_back(word);
  };
  for (char ch : text) {
    if (ch == '\n' || ch == '\r') flush();
    else current.push_back(ch);
  }
  flush();
  return words;
}

std::optional<int> parse_small_int(const std::wstring& text) {
  const std::string utf = satou::trim_copy(wide_to_utf8(text));
  if (utf.empty()) return std::nullopt;
  int value = 0;
  for (unsigned char ch : utf) {
    if (!std::isdigit(ch)) return std::nullopt;
    if (value > 100000) return std::nullopt;
    value = value * 10 + (ch - '0');
  }
  return value;
}

void load_form(const satou::GroupSettings& settings) {
  set_checked(g_welcome_check, settings.welcome_enabled);
  SetWindowTextW(g_welcome_text, utf8_to_wide(settings.welcome_text).c_str());
  set_checked(g_leave_check, settings.leave_enabled);
  set_checked(g_approve_check, settings.approve_enabled);
  set_checked(g_reject_unmatched, settings.approve_reject_unmatched);
  std::wstring words;
  for (const auto& word : settings.words) {
    if (!words.empty()) words += L"\r\n";
    words += utf8_to_wide(word);
  }
  SetWindowTextW(g_words, words.c_str());
  auto join_lines = [](const std::vector<std::string>& lines) {
    std::wstring text;
    for (const auto& line : lines) {
      if (!text.empty()) text += L"\r\n";
      text += utf8_to_wide(line);
    }
    return text;
  };
  SetWindowTextW(g_approve_words, join_lines(settings.approve_words).c_str());
  SetWindowTextW(g_reject_words, join_lines(settings.reject_words).c_str());
  SetWindowTextW(g_blacklist, join_lines(settings.approve_blacklist).c_str());
  SetWindowTextW(g_reject_reason, utf8_to_wide(settings.approve_reject_reason).c_str());
  SetWindowTextW(g_word_mute, utf8_to_wide(satou::format_seconds(settings.word_mute_seconds)).c_str());
  set_checked(g_spam_check, settings.spam_enabled);
  SetWindowTextW(g_spam_limit, std::to_wstring(settings.spam_limit).c_str());
  SetWindowTextW(g_spam_window, std::to_wstring(settings.spam_window_seconds).c_str());
  SetWindowTextW(g_spam_mute, utf8_to_wide(satou::format_seconds(settings.spam_mute_seconds)).c_str());
  SetWindowTextW(g_default_mute, utf8_to_wide(satou::format_seconds(settings.default_mute_seconds)).c_str());
  const std::wstring caption = g_selected == "*" ? L"正在编辑：默认。没有单独配置的群都用这份。"
                                                  : L"正在编辑群 " + utf8_to_wide(g_selected) + L"。这份已单独保存，不再跟随默认。";
  SetWindowTextW(g_group_caption, caption.c_str());
}

bool write_form(const std::string& group_id) {
  if (!g_groups || group_id.empty()) {
    append_log(L"群配置还不能保存");
    return false;
  }
  satou::GroupSettings settings;
  settings.welcome_enabled = box_checked(g_welcome_check);
  settings.welcome_text = satou::trim_copy(wide_to_utf8(window_text(g_welcome_text)));
  settings.leave_enabled = box_checked(g_leave_check);
  settings.approve_enabled = box_checked(g_approve_check);
  settings.approve_reject_unmatched = box_checked(g_reject_unmatched);
  settings.words = parse_word_lines(wide_to_utf8(window_text(g_words)));
  settings.approve_words = parse_word_lines(wide_to_utf8(window_text(g_approve_words)));
  settings.reject_words = parse_word_lines(wide_to_utf8(window_text(g_reject_words)));
  settings.approve_blacklist = parse_word_lines(wide_to_utf8(window_text(g_blacklist)));
  settings.approve_reject_reason = satou::trim_copy(wide_to_utf8(window_text(g_reject_reason)));
  if (settings.approve_reject_reason.empty()) settings.approve_reject_reason = "未通过入群审批";
  settings.spam_enabled = box_checked(g_spam_check);
  const auto word_mute = satou::parse_duration(wide_to_utf8(window_text(g_word_mute)));
  const auto spam_mute = satou::parse_duration(wide_to_utf8(window_text(g_spam_mute)));
  const auto default_mute = satou::parse_duration(wide_to_utf8(window_text(g_default_mute)));
  const auto spam_limit = parse_small_int(window_text(g_spam_limit));
  const auto spam_window = parse_small_int(window_text(g_spam_window));
  if (!word_mute || !spam_mute || !default_mute) {
    append_log(L"时长请写成 10分、30秒、1小时 或 1天");
    return false;
  }
  if (!spam_limit || *spam_limit < 2 || *spam_limit > 100) {
    append_log(L"刷屏条数要是 2 到 100");
    return false;
  }
  if (!spam_window || *spam_window < 1 || *spam_window > 3600) {
    append_log(L"刷屏窗口要是 1 到 3600 秒");
    return false;
  }
  settings.word_mute_seconds = *word_mute;
  settings.spam_mute_seconds = *spam_mute;
  settings.default_mute_seconds = *default_mute;
  settings.spam_limit = *spam_limit;
  settings.spam_window_seconds = *spam_window;
  if (!g_groups->set(group_id, settings)) {
    append_log(L"保存群配置失败");
    return false;
  }
  return true;
}

void select_list_item(const std::string& group_id) {
  for (int i = 0; i < static_cast<int>(g_group_ids.size()); ++i) {
    if (g_group_ids[static_cast<std::size_t>(i)] == group_id) {
      SendMessageW(g_group_list, LB_SETCURSEL, i, 0);
      return;
    }
  }
}

void reload_group_list(const std::string& select_id) {
  SendMessageW(g_group_list, LB_RESETCONTENT, 0, 0);
  g_group_ids.clear();
  g_group_ids.push_back("*");
  SendMessageW(g_group_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"默认（全部群）"));
  if (g_groups) {
    for (const auto& id : g_groups->ids()) {
      g_group_ids.push_back(id);
      const std::wstring wide = utf8_to_wide(id);
      SendMessageW(g_group_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
  }
  std::string selected = "*";
  for (const auto& id : g_group_ids) {
    if (id == select_id) selected = id;
  }
  g_selected = selected;
  select_list_item(g_selected);
  load_form(g_groups ? g_groups->get(g_selected) : satou::GroupSettings{});
}

void show_page(int page) {
  g_page = page;
  for (HWND control : g_connect_controls) ShowWindow(control, page == 0 ? SW_SHOW : SW_HIDE);
  for (HWND control : g_group_controls) ShowWindow(control, page == 1 ? SW_SHOW : SW_HIDE);
  if (g_form) ShowWindow(g_form, page == 1 ? SW_SHOW : SW_HIDE);
  if (g_window) InvalidateRect(g_window, nullptr, TRUE);
}

void round_window(HWND control, int radius) {
  if (!control) return;
  RECT rect{};
  GetWindowRect(control, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) return;
  SetWindowRgn(control, CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius), TRUE);
}

void fill_round_rect(Gdiplus::Graphics& graphics, const RECT& rect, int radius, Gdiplus::Color color) {
  if (rect.right <= rect.left || rect.bottom <= rect.top) return;
  Gdiplus::GraphicsPath path;
  const int diameter = radius * 2;
  const int left = rect.left;
  const int top = rect.top;
  const int right = rect.right;
  const int bottom = rect.bottom;
  path.AddArc(left, top, diameter, diameter, 180, 90);
  path.AddArc(right - diameter, top, diameter, diameter, 270, 90);
  path.AddArc(right - diameter, bottom - diameter, diameter, diameter, 0, 90);
  path.AddArc(left, bottom - diameter, diameter, diameter, 90, 90);
  path.CloseFigure();
  Gdiplus::SolidBrush brush(color);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.FillPath(&brush, &path);
}

void layout(HWND window) {
  if (!g_url || !g_form || !g_group_list) return;
  RECT client{};
  GetClientRect(window, &client);
  const int width = client.right;
  const int margin = scale(16);
  int y = scale(20);
  MoveWindow(g_icon, margin, y, scale(52), scale(52), TRUE);
  round_window(g_icon, scale(14));
  MoveWindow(g_title, margin + scale(66), y, width - margin * 2 - scale(66), scale(32), TRUE);
  MoveWindow(g_status, margin + scale(66), y + scale(32), width - margin * 2 - scale(66), scale(20), TRUE);
  y += scale(70);
  g_segment = RECT{margin, y, width - margin, y + scale(36)};
  y += scale(52);

  const int left = margin;
  const int right = width - margin;
  const int bottom = client.bottom - margin;
  const int inner_w = right - left;
  g_main_cards.clear();
  g_main_cards.push_back(RECT{left, y, right, y + scale(156)});
  int row = y + scale(16);
  const int row_h = scale(34);
  const int label_w = scale(84);
  auto place_row = [&](HWND label, HWND edit) {
    MoveWindow(label, left + scale(18), row + scale(5), label_w, row_h, TRUE);
    MoveWindow(edit, left + label_w + scale(12), row, inner_w - label_w - scale(30), row_h, TRUE);
    round_window(edit, scale(10));
    row += row_h + scale(10);
  };
  place_row(g_label_url, g_url);
  place_row(g_label_token, g_token);
  place_row(g_label_super, g_super);
  const int button_y = g_main_cards[0].bottom + scale(14);
  MoveWindow(g_start, left, button_y, scale(128), scale(40), TRUE);
  MoveWindow(g_save, left + scale(140), button_y, scale(128), scale(40), TRUE);
  round_window(g_start, scale(20));
  round_window(g_save, scale(20));
  const int hint_y = button_y + scale(48);
  MoveWindow(g_hint, left + scale(4), hint_y, inner_w, scale(40), TRUE);
  const int log_y = hint_y + scale(44);
  g_main_cards.push_back(RECT{left, log_y, right, bottom});
  MoveWindow(g_log, left + scale(14), log_y + scale(12), inner_w - scale(28), std::max(scale(80), bottom - log_y - scale(24)), TRUE);
  round_window(g_log, scale(14));

  const int list_w = scale(176);
  g_main_cards.push_back(RECT{left, y, left + list_w, bottom});
  MoveWindow(g_group_list, left + scale(12), y + scale(12), list_w - scale(24), bottom - y - scale(108), TRUE);
  round_window(g_group_list, scale(12));
  MoveWindow(g_group_id, left + scale(12), bottom - scale(86), list_w - scale(24), scale(34), TRUE);
  round_window(g_group_id, scale(10));
  MoveWindow(g_add_group, left + scale(12), bottom - scale(44), scale(74), scale(32), TRUE);
  MoveWindow(g_remove_group, left + scale(94), bottom - scale(44), list_w - scale(106), scale(32), TRUE);
  round_window(g_add_group, scale(16));
  round_window(g_remove_group, scale(16));
  MoveWindow(g_form, left + list_w + scale(14), y, right - left - list_w - scale(14), bottom - y, TRUE);

  RECT form{};
  GetClientRect(g_form, &form);
  const int gw = form.right;
  int gy = scale(6) - g_scroll;
  g_form_cards.clear();
  auto move_form = [&](HWND control, int x, int top, int w, int h) { MoveWindow(control, x, top, w, h, TRUE); };
  auto section = [&](HWND label) {
    move_form(label, scale(6), gy, gw - scale(12), scale(20));
    gy += scale(24);
  };
  auto finish_card = [&](int card_top) {
    gy += scale(14);
    g_form_cards.push_back(RECT{0, card_top, gw - scale(2), gy});
    gy += scale(16);
  };
  section(g_section_welcome);
  int card_top = gy;
  gy += scale(10);
  move_form(g_group_caption, scale(16), gy, gw - scale(32), scale(36));
  gy += scale(40);
  move_form(g_welcome_check, scale(6), gy, gw - scale(16), scale(36));
  gy += scale(38);
  move_form(g_leave_check, scale(6), gy, gw - scale(16), scale(36));
  gy += scale(42);
  move_form(g_label_welcome, scale(16), gy + scale(6), scale(64), scale(24));
  move_form(g_welcome_text, scale(84), gy, gw - scale(104), row_h);
  round_window(g_welcome_text, scale(10));
  gy += scale(44);
  finish_card(card_top);

  section(g_section_order);
  card_top = gy;
  gy += scale(8);
  move_form(g_spam_check, scale(6), gy, gw - scale(16), scale(36));
  gy += scale(42);
  move_form(g_label_words, scale(16), gy, gw - scale(32), scale(20));
  gy += scale(24);
  move_form(g_words, scale(12), gy, gw - scale(26), scale(84));
  round_window(g_words, scale(12));
  gy += scale(94);
  move_form(g_label_word_mute, scale(16), gy + scale(6), scale(72), scale(24));
  move_form(g_word_mute, scale(92), gy, scale(100), row_h);
  round_window(g_word_mute, scale(10));
  move_form(g_label_default_mute, scale(206), gy + scale(6), scale(72), scale(24));
  move_form(g_default_mute, scale(280), gy, scale(100), row_h);
  round_window(g_default_mute, scale(10));
  gy += scale(44);
  move_form(g_label_spam_count, scale(16), gy + scale(6), scale(40), scale(24));
  move_form(g_spam_limit, scale(58), gy, scale(56), row_h);
  round_window(g_spam_limit, scale(10));
  move_form(g_label_spam_window, scale(126), gy + scale(6), scale(58), scale(24));
  move_form(g_spam_window, scale(186), gy, scale(56), row_h);
  round_window(g_spam_window, scale(10));
  move_form(g_label_spam_mute, scale(254), gy + scale(6), scale(72), scale(24));
  move_form(g_spam_mute, scale(328), gy, scale(100), row_h);
  round_window(g_spam_mute, scale(10));
  gy += scale(48);
  finish_card(card_top);

  section(g_section_approve);
  card_top = gy;
  gy += scale(8);
  move_form(g_approve_check, scale(6), gy, gw - scale(16), scale(36));
  gy += scale(38);
  move_form(g_reject_unmatched, scale(6), gy, gw - scale(16), scale(36));
  gy += scale(44);
  move_form(g_label_approve, scale(16), gy, gw - scale(32), scale(20));
  gy += scale(22);
  move_form(g_approve_words, scale(12), gy, gw - scale(26), scale(62));
  round_window(g_approve_words, scale(12));
  gy += scale(70);
  move_form(g_label_reject, scale(16), gy, gw - scale(32), scale(20));
  gy += scale(22);
  move_form(g_reject_words, scale(12), gy, gw - scale(26), scale(62));
  round_window(g_reject_words, scale(12));
  gy += scale(70);
  move_form(g_label_blacklist, scale(16), gy, gw - scale(32), scale(20));
  gy += scale(22);
  move_form(g_blacklist, scale(12), gy, gw - scale(26), scale(62));
  round_window(g_blacklist, scale(12));
  gy += scale(70);
  move_form(g_label_reason, scale(16), gy + scale(6), scale(72), scale(24));
  move_form(g_reject_reason, scale(92), gy, gw - scale(112), row_h);
  round_window(g_reject_reason, scale(10));
  gy += scale(48);
  finish_card(card_top);
  move_form(g_save_group, 0, gy, scale(136), scale(40));
  round_window(g_save_group, scale(20));
  gy += scale(56);
  g_form_content = gy + g_scroll;
  const int max_scroll = std::max(0, g_form_content - static_cast<int>(form.bottom));
  if (g_scroll > max_scroll) g_scroll = max_scroll;
  show_page(g_page);
}

void paint_surface(HDC dc, const RECT& client, const std::vector<RECT>& cards, bool segment) {
  HBRUSH background = CreateSolidBrush(kBg);
  FillRect(dc, &client, background);
  DeleteObject(background);
  {
    Gdiplus::Graphics graphics(dc);
    for (const RECT& card : cards) fill_round_rect(graphics, card, scale(18), Gdiplus::Color(255, 255, 255, 255));
  }
  if (!segment) return;
  Gdiplus::Graphics graphics(dc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  fill_round_rect(graphics, g_segment, scale(11), Gdiplus::Color(255, 229, 229, 234));
  RECT selected = g_segment;
  const int mid = (g_segment.left + g_segment.right) / 2;
  if (g_page == 0) selected.right = mid;
  else selected.left = mid;
  InflateRect(&selected, -scale(3), -scale(3));
  fill_round_rect(graphics, selected, scale(9), Gdiplus::Color(255, 255, 255, 255));
  Gdiplus::Font font(L"Microsoft YaHei UI", static_cast<Gdiplus::REAL>(scale(14)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush ink(Gdiplus::Color(255, 28, 28, 30));
  Gdiplus::SolidBrush muted(Gdiplus::Color(255, 142, 142, 147));
  Gdiplus::StringFormat format;
  format.SetAlignment(Gdiplus::StringAlignmentCenter);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  const Gdiplus::RectF left_text(static_cast<Gdiplus::REAL>(g_segment.left), static_cast<Gdiplus::REAL>(g_segment.top),
                                 static_cast<Gdiplus::REAL>(mid - g_segment.left), static_cast<Gdiplus::REAL>(g_segment.bottom - g_segment.top));
  const Gdiplus::RectF right_text(static_cast<Gdiplus::REAL>(mid), static_cast<Gdiplus::REAL>(g_segment.top),
                                  static_cast<Gdiplus::REAL>(g_segment.right - mid), static_cast<Gdiplus::REAL>(g_segment.bottom - g_segment.top));
  graphics.DrawString(L"连接", -1, &font, left_text, &format, g_page == 0 ? &ink : &muted);
  graphics.DrawString(L"群管", -1, &font, right_text, &format, g_page == 1 ? &ink : &muted);
}

void draw_owner_item(const DRAWITEMSTRUCT* item) {
  wchar_t text[160]{};
  GetWindowTextW(item->hwndItem, text, 160);
  Gdiplus::Graphics graphics(item->hDC);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
  const RECT rect = item->rcItem;
  const int id = static_cast<int>(item->CtlID);
  if (is_switch(id)) {
    Gdiplus::SolidBrush card(Gdiplus::Color(255, 255, 255, 255));
    graphics.FillRectangle(&card, static_cast<INT>(rect.left), static_cast<INT>(rect.top), static_cast<INT>(rect.right - rect.left),
                           static_cast<INT>(rect.bottom - rect.top));
    Gdiplus::Font font(L"Microsoft YaHei UI", static_cast<Gdiplus::REAL>(scale(15)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush label(Gdiplus::Color(255, 28, 28, 30));
    Gdiplus::StringFormat format;
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const Gdiplus::RectF text_rect(static_cast<Gdiplus::REAL>(rect.left + scale(14)), static_cast<Gdiplus::REAL>(rect.top),
                                   static_cast<Gdiplus::REAL>(rect.right - rect.left - scale(78)), static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
    graphics.DrawString(text, -1, &font, text_rect, &format, &label);
    const int track_w = scale(51);
    const int track_h = scale(31);
    const int x = rect.right - scale(16) - track_w;
    const int y = rect.top + (rect.bottom - rect.top - track_h) / 2;
    const bool on = g_on[id];
    fill_round_rect(graphics, RECT{x, y, x + track_w, y + track_h}, track_h / 2,
                    on ? Gdiplus::Color(255, 52, 199, 89) : Gdiplus::Color(255, 229, 229, 234));
    const int knob = scale(27);
    const int knob_x = on ? x + track_w - knob - scale(2) : x + scale(2);
    Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
    graphics.FillEllipse(&white, knob_x, y + (track_h - knob) / 2, knob, knob);
    return;
  }
  const bool stop = id == kStart && std::wstring(text) == L"停止";
  const bool primary = (id == kStart && !stop) || id == kSaveGroup;
  Gdiplus::Color fill = primary ? Gdiplus::Color(255, 0, 122, 255) : Gdiplus::Color(255, 229, 229, 234);
  if (stop) fill = Gdiplus::Color(255, 255, 59, 48);
  fill_round_rect(graphics, rect, (rect.bottom - rect.top) / 2, fill);
  Gdiplus::Font font(L"Microsoft YaHei UI", static_cast<Gdiplus::REAL>(scale(15)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
  Gdiplus::SolidBrush brush(primary || stop ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 28, 28, 30));
  Gdiplus::StringFormat format;
  format.SetAlignment(Gdiplus::StringAlignmentCenter);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  const Gdiplus::RectF bounds(static_cast<Gdiplus::REAL>(rect.left), static_cast<Gdiplus::REAL>(rect.top),
                              static_cast<Gdiplus::REAL>(rect.right - rect.left), static_cast<Gdiplus::REAL>(rect.bottom - rect.top));
  graphics.DrawString(text, -1, &font, bounds, &format, &brush);
}

LRESULT CALLBACK form_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      const HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      HBRUSH background = CreateSolidBrush(kBg);
      FillRect(dc, &client, background);
      DeleteObject(background);
      {
        Gdiplus::Graphics graphics(dc);
        for (const RECT& card : g_form_cards) fill_round_rect(graphics, card, scale(16), Gdiplus::Color(255, 255, 255, 255));
      }
      EndPaint(window, &paint);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEWHEEL: {
      RECT client{};
      GetClientRect(window, &client);
      const int max_scroll = std::max(0, g_form_content - static_cast<int>(client.bottom));
      const int next = g_scroll - (GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA) * scale(56);
      const int clamped = std::max(0, std::min(max_scroll, next));
      if (clamped != g_scroll) {
        g_scroll = clamped;
        layout(g_window);
      }
      return 0;
    }
    case WM_COMMAND:
    case WM_DRAWITEM:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
      return wnd_proc(g_window, message, wparam, lparam);
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

LRESULT CALLBACK wnd_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      const HDC dc = BeginPaint(window, &paint);
      RECT client{};
      GetClientRect(window, &client);
      std::vector<RECT> cards;
      if (g_main_cards.size() >= 3) {
        if (g_page == 0) {
          cards.push_back(g_main_cards[0]);
          cards.push_back(g_main_cards[1]);
        } else {
          cards.push_back(g_main_cards[2]);
        }
      }
      paint_surface(dc, client, cards, true);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DRAWITEM:
      draw_owner_item(reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
      return TRUE;
    case WM_LBUTTONUP: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (PtInRect(&g_segment, point)) {
        const int mid = (g_segment.left + g_segment.right) / 2;
        show_page(point.x < mid ? 0 : 1);
      }
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (g_page == 1 && g_form) return form_proc(g_form, message, wparam, lparam);
      return 0;
    case WM_SIZE:
      layout(window);
      return 0;
    case WM_COMMAND:
      if (HIWORD(wparam) == BN_CLICKED && is_switch(LOWORD(wparam))) {
        HWND control = GetDlgItem(window, LOWORD(wparam));
        if (!control && g_form) control = GetDlgItem(g_form, LOWORD(wparam));
        if (control) set_checked(control, !box_checked(control));
        return 0;
      }
      if (LOWORD(wparam) == kStart && HIWORD(wparam) == BN_CLICKED) {
        if (g_running) stop_worker();
        else start_worker();
        return 0;
      }
      if (LOWORD(wparam) == kSave && HIWORD(wparam) == BN_CLICKED) {
        save_fields();
        return 0;
      }
      if (LOWORD(wparam) == kSaveGroup && HIWORD(wparam) == BN_CLICKED) {
        if (write_form(g_selected)) append_log(L"群管设置已保存在本机，马上生效");
        return 0;
      }
      if (LOWORD(wparam) == kAddGroup && HIWORD(wparam) == BN_CLICKED) {
        if (!g_groups) {
          append_log(L"群配置还不能保存");
          return 0;
        }
        if (!write_form(g_selected)) return 0;
        const std::string id = satou::trim_copy(wide_to_utf8(window_text(g_group_id)));
        const bool digits = !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (!digits || id.size() < 5 || id.size() > 12) {
          append_log(L"群号要是 5 到 12 位数字");
          return 0;
        }
        if (!g_groups->contains(id) && !g_groups->set(id, g_groups->get("*"))) {
          append_log(L"保存群配置失败");
          return 0;
        }
        reload_group_list(id);
        append_log(utf8_to_wide("已添加群 " + id + "，可以单独改规则"));
        return 0;
      }
      if (LOWORD(wparam) == kRemoveGroup && HIWORD(wparam) == BN_CLICKED) {
        if (g_selected == "*") {
          append_log(L"默认配置要留着，没有单独设置的群会用它");
          return 0;
        }
        if (!g_groups || !g_groups->remove(g_selected)) {
          append_log(L"没有删掉这份单独配置");
          return 0;
        }
        reload_group_list("*");
        append_log(L"已删除单独配置，这个群改回默认");
        return 0;
      }
      if (LOWORD(wparam) == kGroupList && HIWORD(wparam) == LBN_SELCHANGE) {
        const auto index = static_cast<int>(SendMessageW(g_group_list, LB_GETCURSEL, 0, 0));
        if (index < 0 || index >= static_cast<int>(g_group_ids.size())) return 0;
        const std::string next = g_group_ids[static_cast<std::size_t>(index)];
        if (next == g_selected) return 0;
        if (!write_form(g_selected)) {
          select_list_item(g_selected);
          return 0;
        }
        g_selected = next;
        load_form(g_groups->get(g_selected));
        return 0;
      }
      return 0;
    case WM_NOTIFY: {
      const auto* header = reinterpret_cast<NMHDR*>(lparam);
      if (header->hwndFrom == g_tabs && header->code == TCN_SELCHANGE) {
        show_page(static_cast<int>(SendMessageW(g_tabs, TCM_GETCURSEL, 0, 0)));
        return 0;
      }
      return 0;
    }
    case kLogMessage: {
      auto* text = reinterpret_cast<std::string*>(lparam);
      if (text) {
        append_log(utf8_to_wide(*text));
        delete text;
      }
      return 0;
    }
    case kStatusMessage: {
      auto* text = reinterpret_cast<std::string*>(lparam);
      if (text) {
        const std::wstring wide = utf8_to_wide(*text);
        set_status(wide);
        if (*text == "已停止") {
          if (g_worker.joinable()) g_worker.join();
          g_running = false;
          if (IsWindow(g_start)) SetWindowTextW(g_start, L"启动");
        }
        delete text;
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC: {
      const HDC dc = reinterpret_cast<HDC>(wparam);
      const HWND control = reinterpret_cast<HWND>(lparam);
      const bool secondary = control == g_status || control == g_hint || control == g_group_caption || control == g_section_welcome ||
                             control == g_section_order || control == g_section_approve;
      SetTextColor(dc, secondary ? kSecondary : kInk);
      SetBkMode(dc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      const HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, kInk);
      SetBkColor(dc, kField);
      return reinterpret_cast<LRESULT>(g_field_brush);
    }
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = scale(760);
      info->ptMinTrackSize.y = scale(640);
      return 0;
    }
    case WM_CLOSE:
      stop_worker();
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

void create_controls(HWND window) {
  g_icon = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON, 0, 0, 10, 10, window,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIcon)), g_instance, nullptr);
  const HICON icon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(1), IMAGE_ICON, scale(64), scale(64), 0));
  SendMessageW(g_icon, STM_SETICON, reinterpret_cast<WPARAM>(icon), 0);

  g_title = add_label(window, L"Satou QQBot", g_title_font, true);
  SetWindowLongPtrW(g_title, GWLP_ID, kTitle);
  g_status = add_label(window, L"未连接", g_font, true);
  SetWindowLongPtrW(g_status, GWLP_ID, kStatus);
  g_form = CreateWindowExW(0, L"SatouForm", L"", WS_CHILD, 0, 0, 10, 10, window, nullptr, g_instance, nullptr);
  g_label_url = add_label(window, L"NapCat", g_font, true);
  g_label_token = add_label(window, L"Token", g_font, true);
  g_label_super = add_label(window, L"超级用户", g_font, true);

  const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
  g_url = CreateWindowExW(0, L"EDIT", L"", edit_style, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUrl)),
                          g_instance, nullptr);
  g_token = CreateWindowExW(0, L"EDIT", L"", edit_style, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToken)),
                            g_instance, nullptr);
  g_super = CreateWindowExW(0, L"EDIT", L"", edit_style, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSuper)),
                            g_instance, nullptr);
  g_start = CreateWindowExW(0, L"BUTTON", L"启动", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStart)), g_instance, nullptr);
  g_save = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, window,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSave)), g_instance, nullptr);
  g_hint = CreateWindowExW(0, L"STATIC", L"群管页可以设置违禁词、欢迎、刷屏和自动入群审批，都保存在本机。",
                          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL, 0, 0, 10, 10, window,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHint)), g_instance, nullptr);
  g_log = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 0, 0, 10, 10,
                          window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLog)), g_instance, nullptr);
  SendMessageW(g_log, EM_SETLIMITTEXT, 64000, 0);

  const DWORD field = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
  const DWORD area = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
  const DWORD button = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
  HWND form = g_form;
  g_group_list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGroupList)), g_instance, nullptr);
  g_group_id = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGroupId)),
                               g_instance, nullptr);
  g_add_group = CreateWindowExW(0, L"BUTTON", L"添加群", button, 0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddGroup)),
                                g_instance, nullptr);
  g_remove_group = CreateWindowExW(0, L"BUTTON", L"删除", button, 0, 0, 10, 10, window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRemoveGroup)), g_instance, nullptr);
  g_section_welcome = add_label(form, L"欢迎", g_font, true);
  g_section_order = add_label(form, L"秩序", g_font, true);
  g_section_approve = add_label(form, L"入群审批", g_font, true);
  g_group_caption = add_label(form, L"正在编辑：默认。没有单独配置的群都用这份。", g_font, true);
  SetWindowLongPtrW(g_group_caption, GWLP_ID, kGroupCaption);
  g_welcome_check = CreateWindowExW(0, L"BUTTON", L"入群欢迎", button, 0, 0, 10, 10, form,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWelcomeCheck)), g_instance, nullptr);
  g_leave_check = CreateWindowExW(0, L"BUTTON", L"退群通知", button, 0, 0, 10, 10, form,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLeaveCheck)), g_instance, nullptr);
  g_spam_check = CreateWindowExW(0, L"BUTTON", L"刷屏检测", button, 0, 0, 10, 10, form,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSpamCheck)), g_instance, nullptr);
  g_approve_check = CreateWindowExW(0, L"BUTTON", L"自动审批", button, 0, 0, 10, 10, form,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApproveCheck)), g_instance, nullptr);
  g_reject_unmatched = CreateWindowExW(0, L"BUTTON", L"未命中白词则拒绝", button, 0, 0, 10, 10, form,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRejectUnmatched)), g_instance, nullptr);
  g_label_welcome = add_label(form, L"欢迎语", g_font, true);
  g_label_words = add_label(form, L"违禁词，每行一个", g_font, true);
  g_label_approve = add_label(form, L"白词，每行一个。申请内容包含就同意", g_font, true);
  g_label_reject = add_label(form, L"黑词，每行一个。包含就拒绝", g_font, true);
  g_label_blacklist = add_label(form, L"黑名单 QQ，每行一个", g_font, true);
  g_label_reason = add_label(form, L"拒绝理由", g_font, true);
  g_welcome_text = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWelcomeText)),
                                   g_instance, nullptr);
  g_words = CreateWindowExW(0, L"EDIT", L"", area, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWords)), g_instance,
                            nullptr);
  g_label_word_mute = add_label(form, L"违禁禁言", g_font, true);
  g_word_mute = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWordMute)),
                                g_instance, nullptr);
  g_label_default_mute = add_label(form, L"缺省禁言", g_font, true);
  g_default_mute = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultMute)),
                                   g_instance, nullptr);
  g_label_spam_count = add_label(form, L"条数", g_font, true);
  g_spam_limit = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSpamLimit)),
                                 g_instance, nullptr);
  g_label_spam_window = add_label(form, L"窗口秒", g_font, true);
  g_spam_window = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSpamWindow)),
                                  g_instance, nullptr);
  g_label_spam_mute = add_label(form, L"刷屏禁言", g_font, true);
  g_spam_mute = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSpamMute)),
                                g_instance, nullptr);
  g_approve_words = CreateWindowExW(0, L"EDIT", L"", area, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApproveWords)),
                                    g_instance, nullptr);
  g_reject_words = CreateWindowExW(0, L"EDIT", L"", area, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRejectWords)),
                                   g_instance, nullptr);
  g_blacklist = CreateWindowExW(0, L"EDIT", L"", area, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBlacklist)),
                                g_instance, nullptr);
  g_reject_reason = CreateWindowExW(0, L"EDIT", L"", field, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRejectReason)),
                                    g_instance, nullptr);
  g_save_group = CreateWindowExW(0, L"BUTTON", L"保存群管", button, 0, 0, 10, 10, form, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveGroup)),
                                 g_instance, nullptr);

  g_connect_controls = {g_label_url, g_label_token, g_label_super, g_url, g_token, g_super, g_start, g_save, g_hint, g_log};
  g_group_controls = {g_group_list, g_group_id, g_add_group, g_remove_group};
  for (HWND control : {g_url, g_token, g_super, g_start, g_save, g_hint, g_log, g_status, g_group_list, g_group_id, g_add_group,
                       g_remove_group, g_welcome_check, g_leave_check, g_spam_check, g_approve_check, g_reject_unmatched, g_welcome_text,
                       g_words, g_word_mute, g_default_mute, g_spam_limit, g_spam_window, g_spam_mute, g_approve_words, g_reject_words,
                       g_blacklist, g_reject_reason, g_save_group, g_group_caption, g_label_words, g_label_welcome, g_label_word_mute,
                       g_label_default_mute, g_label_spam_count, g_label_spam_window, g_label_spam_mute, g_section_welcome, g_section_order,
                       g_section_approve, g_label_approve, g_label_reject, g_label_blacklist, g_label_reason}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
  }
  SendMessageW(g_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
  show_page(0);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
  g_instance = instance;
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
  InitCommonControlsEx(&controls);

  Gdiplus::GdiplusStartupInput gdiplus_input;
  Gdiplus::GdiplusStartup(&g_gdiplus, &gdiplus_input, nullptr);
  g_dpi = GetDpiForSystem();
  g_background = CreateSolidBrush(kBg);
  g_field_brush = CreateSolidBrush(kField);
  g_card_brush = CreateSolidBrush(RGB(255, 255, 255));
  g_font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  g_title_font = CreateFontW(-scale(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

  WNDCLASSW window_class{};
  window_class.lpfnWndProc = wnd_proc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = g_background;
  window_class.lpszClassName = L"SatouQQBotWindow";
  window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  RegisterClassW(&window_class);
  WNDCLASSW form_class{};
  form_class.lpfnWndProc = form_proc;
  form_class.hInstance = instance;
  form_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  form_class.lpszClassName = L"SatouForm";
  RegisterClassW(&form_class);

  const int width = scale(900);
  const int height = scale(760);
  const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
  const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
  g_window = CreateWindowExW(0, L"SatouQQBotWindow", L"Satou QQBot", WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr, nullptr,
                             instance, nullptr);
  create_controls(g_window);
  g_config_path = config_from_args();
  bool loaded = false;
  try {
    std::error_code error;
    if (std::filesystem::exists(g_config_path, error)) {
      g_config = satou::load_config(g_config_path);
      loaded = true;
    }
  } catch (const std::exception& ex) {
    append_log(utf8_to_wide(ex.what()));
  }
  fill_fields();
  try {
    g_groups = std::make_unique<satou::GroupStore>(satou::resolve_data_path(g_config_path, g_config.data_file));
  } catch (const std::exception& ex) {
    append_log(utf8_to_wide(ex.what()));
  }
  reload_group_list("*");
  ShowWindow(g_window, show);
  UpdateWindow(g_window);
  layout(g_window);

  if (loaded) {
    append_log(L"已读取配置，开始连接。");
    start_worker();
  } else {
    append_log(L"还没有配置文件。填写 NapCat 地址后点启动，窗口会留下来。");
    set_status(L"等待启动");
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(g_window, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  stop_worker();
  DeleteObject(g_font);
  DeleteObject(g_title_font);
  DeleteObject(g_background);
  DeleteObject(g_field_brush);
  DeleteObject(g_card_brush);
  Gdiplus::GdiplusShutdown(g_gdiplus);
  return static_cast<int>(msg.wParam);
}
