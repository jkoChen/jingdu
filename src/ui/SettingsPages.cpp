#include "App.h"
#include "Version.h"
#include "Lunar.h"
#include <algorithm>
#include "ViewData.h"

namespace jd {
void App::endScreen() {
  dev.cast(false);
  while (!stack.empty() && stack.back().view != View::Settings) {
    stack.pop_back();
  }
  view = View::Settings;
  offset = stack.empty() ? 0 : stack.back().offset;
  if (!stack.empty()) {
    stack.pop_back();
  }
  dirty = true;
}
void App::screenPage() {
  if (view == View::ScreenCanvas) {
    if (const Canvas* image = dev.screenImage()) {
      memcpy(canvas.bits, image->bits, sizeof canvas.bits);
    }
    hit(60, 104, 120, 208, [this] { go(View::ScreenInfo); });
    return;
  }
  if (view == View::ScreenConnect) {
    header("无线墨屏");
    canvas.ui(16, 75, dev.screenError.empty() ? "正在准备无线墨屏" : dev.screenError, 15, 208);
    entry(135, "连接 Wi-Fi", "", [this] {
      go(View::Wifi);
      if (!dev.wifi) {
        dev.radio(true);
      }
      if (dev.wifi) {
        dev.scan();
      }
    });
    return;
  }
  if (view == View::ScreenKey) {
    header("重置密钥");
    canvas.ui(16, 80, "重置后需要更新发送端密钥", 15, 208);
    primary(16, 366, 208, "确认重置", [this] {
      if (dev.renewScreenKey()) {
        back();
      } else {
        notice = "密钥保存失败，请重试";
        msg(notice);
      }
    });
    return;
  }
  header("连接信息");
  canvas.ui(16, 52, dev.screenReady ? "已连接 Wi-Fi" : "网络已断开，正在重连", 15, 208);
  canvas.ui(16, 84, "设备地址", 12);
  if (!dev.screenHost.empty()) {
    canvas.ui(16, 105, dev.screenHost + ".local:8080", 15, 208);
  }
  if (dev.connected) {
    canvas.ui(16, 130, dev.ip + ":8080", 15, 208);
  }
  canvas.rect(16, 166, 208, 1);
  canvas.ui(16, 188, "投送密钥", 12);
  canvas.ui(16, 212, dev.screenKey, 22, 208);
  entry(254, "重置密钥", "", [this] { go(View::ScreenKey); });
  canvas.ui(16, 333, "展示期间保持联网", 12);
  primary(16, 366, 208, "结束展示", [this] { endScreen(); });
}
void App::networkChanged() {
  if (dev.casting && dev.screenReady) {
    // Configuration dialogs and the connection notice belong to the same session.
    if (view == View::ScreenConnect || view == View::Wifi || view == View::Password) {
      while (!stack.empty() && stack.back().view != View::Settings) {
        stack.pop_back();
      }
      password.clear();
      joining = showPass = false;
      view = View::ScreenCanvas;
      dirty = true;
      return;
    }
    for (auto& route : stack) {
      if (route.view == View::ScreenConnect) {
        route.view = View::ScreenCanvas;
      }
    }
  }
  if (view == View::ScreenInfo || view == View::ScreenConnect) {
    dirty = true;
  }
  if (view == View::Password && joining && dev.connected && dev.network == ssid) {
    back();
    return;
  }
  if (view == View::Settings || view == View::Wifi || view == View::Service || view == View::Password ||
      view == View::Clock || view == View::Lock) {
    dirty = true;
  }
}
static int monthDays(int year, int month) {
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return days[month] + (month == 1 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
void App::startClock() {
  time_t t = dev.clock();
  localtime_r(&t, &editTime);
  if (editTime.tm_year < 120) {
    editTime = {};
    editTime.tm_year = 126;
    editTime.tm_mday = 1;
  }
  clockField = 0;
  go(View::Clock);
}
void App::clockPage() {
  const int days[] = {0, 1, 7};
  const char* choices[] = {"关闭", "每天", "每周"};
  if (view == View::AutoClock) {
    header("自动联网校时");
    for (int i = 0; i < 3; ++i) {
      int interval = days[i];
      option(48 + 48 * i, choices[i], dev.autoClockDays == interval, [this, interval] {
        dev.autoClockDays = interval;
        storeSettings();
        back();
      });
    }
    canvas.ui(16, 228, "使用已保存的 Wi-Fi", 12, 208);
    canvas.ui(16, 250, "校时后恢复原联网状态", 12, 208);
    return;
  }
  if (view == View::Clock) {
    header("日期与时间");
    time_t t = dev.clock();
    struct tm shown{};
    localtime_r(&t, &shown);
    char label[40];
    if (shown.tm_year >= 120) {
      snprintf(label, sizeof label, "%d 年 %d 月 %d 日", shown.tm_year + 1900, shown.tm_mon + 1,
               shown.tm_mday);
      canvas.ui(16, 59, label);
      canvas.ui(16, 88, lunarDate(shown), 12);
      strftime(label, sizeof label, "%H:%M", &shown);
      canvas.ui(16, 115, label);
    } else {
      canvas.ui(16, 59, "时钟待校准");
    }
    canvas.rect(16, 158, 208, 1);
    entry(172, "手动校时", "", [this, shown] {
      if (shown.tm_year >= 120) {
        editTime = shown;
      } else {
        editTime = {};
        editTime.tm_year = 126;
        editTime.tm_mday = 1;
      }
      clockField = 0;
      go(View::ClockEdit);
    });
    entry(223, "自动联网校时",
          choices[dev.autoClockDays == 7   ? 2
                  : dev.autoClockDays == 1 ? 1
                                           : 0],
          [this] { go(View::AutoClock); });
    if (dev.lastNetworkSync) {
      struct tm last{};
      localtime_r(&dev.lastNetworkSync, &last);
      strftime(label, sizeof label, "%m-%d %H:%M", &last);
      canvas.ui(16, 293, Str("上次联网校时  ") + label, 12, 208);
    }
    canvas.ui(16, 330, dev.clockInfo, 12, 208);
    primary(16, 366, 208, "联网校时", [this] {
      dev.requestTime();
      dirty = true;
    });
    return;
  }
  header("手动校时");
  canvas.ui(16, 77, "点选年月日或时分，再调整数值", 12, 208);
  const char* names[] = {"年", "月", "日", "时", "分"};
  int values[] = {editTime.tm_year + 1900, editTime.tm_mon + 1, editTime.tm_mday, editTime.tm_hour,
                  editTime.tm_min};
  int starts[] = {8, 65, 109, 153, 197};
  int widths[] = {54, 41, 41, 41, 38};
  for (int i = 0; i < 5; i++) {
    canvas.ui(starts[i] + 3, 195, names[i], 12);
    btn(starts[i], 215, widths[i], 41, std::to_string(values[i]), [this, i] {
      clockField = i;
      dirty = true;
    });
    const int edges[] = {0, 62, 106, 150, 194, 240};
    hits.back().x = edges[i];
    hits.back().w = edges[i + 1] - edges[i];
    hits.back().h = 44;
    if (i == clockField) {
      canvas.rect(starts[i], 257, widths[i], 2);
    }
  }
  auto change = [this](int delta) {
    int* field[] = {&editTime.tm_year, &editTime.tm_mon, &editTime.tm_mday, &editTime.tm_hour,
                    &editTime.tm_min};
    int lo[] = {120, 0, 1, 0, 0},
        hi[] = {199, 11, monthDays(editTime.tm_year + 1900, editTime.tm_mon), 23, 59};
    int& v = *field[clockField];
    v += delta;
    if (v < lo[clockField]) {
      v = hi[clockField];
    }
    if (v > hi[clockField]) {
      v = lo[clockField];
    }
    editTime.tm_mday = std::min(editTime.tm_mday, monthDays(editTime.tm_year + 1900, editTime.tm_mon));
    dirty = true;
  };
  btn(12, 279, 101, 45, "减一", [change] { change(-1); });
  btn(126, 279, 102, 45, "加一", [change] { change(1); });
  primary(16, 366, 208, "保存时间", [this] {
    editTime.tm_sec = 0;
    editTime.tm_isdst = -1;
    if (dev.setTime(mktime(&editTime))) {
      back();
    } else {
      msg("保存时间失败");
    }
  });
}
void App::settings() {
  if (view == View::Power) {
    header("电源");
    entry(48, "自动锁屏", idleMins ? std::to_string(idleMins) + " 分钟" : "已关闭",
          [this] { go(View::AutoLock); });
    entry(109, "长期休眠", "", [this] { go(View::ConfirmLongSleep); });
    entry(170, "重启设备", "", [this] { go(View::ConfirmRestart); });
    return;
  }
  if (view == View::ConfirmLongSleep || view == View::ConfirmRestart) {
    const bool sleeping = view == View::ConfirmLongSleep;
    header(sleeping ? "长期休眠" : "重启设备");
    canvas.ui(16, 83, sleeping ? "时钟将停止刷新" : "重启后回到首页", 15, 208);
    canvas.ui(16, 126, sleeping ? "前光和无线网络将关闭。" : "阅读进度和设置会保留。", 12, 208);
    if (sleeping) {
      canvas.ui(16, 150, "按一下侧键即可唤醒。", 12, 208);
    }
    primary(16, 366, 208, sleeping ? "进入长期休眠" : "确认重启",
            [this, sleeping] { confirmPower(sleeping ? PowerAction::LongSleep : PowerAction::Restart); });
    return;
  }
  if (view == View::Refresh) {
    header("全刷间隔");
    for (int i = 0; i < 5; ++i) {
      int value = kRefreshIntervals[i];
      option(48 + i * 48, std::to_string(value) + " 页", value == cadence, [this, value] {
        cadence = value;
        storeSettings();
        back();
      });
    }
    return;
  }
  if (view == View::AutoLock) {
    header("自动锁屏");
    const int minutes[] = {5, 10, 15, 30, 0};
    for (int i = 0; i < 5; ++i) {
      int value = minutes[i];
      option(48 + i * 48, value ? std::to_string(value) + " 分钟" : "关闭", value == idleMins, [this, value] {
        idleMins = value;
        storeSettings();
        back();
      });
    }
    return;
  }
  if (view == View::CardInfo) {
    header("存储卡");
    canvas.ui(16, 67, dev.card ? "TF 卡已挂载" : "未检测到 TF 卡");
    if (dev.card) {
      entry(122, "浏览文件", "", [this] {
        dir = "/";
        pickBook = false;
        go(View::Files);
      });
    }
    primary(16, 366, 208, "重新检测", [this] {
      dev.mount();
      msg(dev.card ? "TF 卡已就绪" : "请检查 TF 卡", dev.card ? Recovery::Back : Recovery::Card);
    });
    return;
  }
  if (view == View::Settings) {
    header("设置");
    entry(40, "Wi-Fi",
          dev.connected ? "已连接"
          : dev.wifi    ? "未连接"
                        : "已关闭",
          [this] {
            go(View::Wifi);
            if (dev.wifi) {
              dev.scan();
            }
          });
    entry(84, "文件服务", dev.serving ? "已开启" : "已关闭", [this] { go(View::Service); });
    entry(128, "无线墨屏", "", [this] {
      dev.cast(true);
      go(dev.screenReady ? View::ScreenCanvas : View::ScreenConnect);
    });
    canvas.rect(16, 176, 208, 1);
    entry(181, "快捷设置", "", [this] { go(View::Tools); });
    entry(225, "全刷间隔", std::to_string(cadence) + " 页", [this] { go(View::Refresh); });
    canvas.rect(16, 273, 208, 1);
    entry(278, "日期与时间", "", [this] { startClock(); });
    entry(322, "电源", "", [this] { go(View::Power); });
    entry(366, "关于静读", "", [this] { go(View::About); });
    return;
  }
  if (view == View::About) {
    header("关于静读");
    auto infoLine = [this](int y, const Str& label, const Str& value) {
      canvas.ui(16, y + 15, label, 12, 80);
      canvas.ui(224 - Canvas::textWidth(value, 15), y + 13, value, 15, 140);
    };
    infoLine(42, "系统版本", JINGDU_VERSION);
    canvas.rect(16, 93, 208, 1);
    infoLine(100, "设备型号", "WDD 2026V1");
    infoLine(144, "处理器", "ESP32");
    infoLine(188, "机身闪存", "8 MiB");
    infoLine(232, "屏幕", "3.7 英寸墨水屏");
    infoLine(276, "分辨率", "240 × 416");
    canvas.rect(16, 328, 208, 1);
    entry(339, "存储卡", dev.card ? "已挂载" : "未挂载", [this] { go(View::CardInfo); });
    return;
  }
  if (view == View::Wifi) {
    header("Wi-Fi");
    canvas.ui(16, 63, "无线网络");
    canvas.ui(158, 65, dev.wifi ? "开" : "关", 12);
    canvas.border(184, 59, 38, 22);
    canvas.rect(dev.wifi ? 204 : 187, 62, 15, 16);
    hit(0, 45, 240, 44, [this] {
      dev.radio(!dev.wifi);
      offset = 0;
      if (dev.wifi) {
        dev.scan();
      }
      dirty = true;
    });
    canvas.rect(16, 102, 208, 1);
    if (dev.wifi) {
      for (unsigned i = offset; i < dev.networks.size() && i < offset + 5; ++i) {
        const auto& name = dev.networks[i];
        entry(108 + (i - offset) * 44, name, dev.connected && name == dev.network ? "已连接" : "",
              [this, name] {
                ssid = name;
                password.clear();
                showPass = joining = false;
                keyboard = 0;
                go(View::Password);
              });
      }
      canvas.ui(16, dev.networks.size() > 5 ? 350 : 334, dev.error.empty() ? dev.scanInfo : dev.error, 12,
                dev.networks.size() > 5 ? 134 : 209);
      auto scan = [this] {
        dev.scan();
        offset = 0;
        dirty = true;
      };
      if (offset || offset + 5 < dev.networks.size()) {
        if (offset) {
          pageControl(false, [this] {
            offset -= 5;
            dirty = true;
          });
        }
        compact(2, 337, dev.scanning ? "扫描中" : "扫描", scan);
        pageCount(offset / 5, (dev.networks.size() + 4) / 5);
        if (offset + 5 < dev.networks.size()) {
          pageControl(true, [this] {
            offset += 5;
            dirty = true;
          });
        }
      } else {
        primary(16, 366, 208, dev.scanning ? "扫描中" : "重新扫描", scan);
      }
    }
    return;
  }
  if (view == View::Password) {
    header("输入密码");
    canvas.ui(16, 44, ssid, 12, 208);
    canvas.border(10, 70, 167, 34);
    Str shown = showPass ? password : Str(password.size(), '*');
    while (!shown.empty() && Canvas::textWidth(shown) > 154) {
      shown.erase(0, 1);
    }
    canvas.ui(16, 79, shown, 15, 155);
    canvas.ui(187, 80, showPass ? "隐藏" : "显示", 12);
    hit(177, 65, 59, 44, [this] {
      showPass = !showPass;
      dirty = true;
    });
    const char* letters[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const char* numbers[] = {"1234567890", "!@#$%&*()", "-_=+./?"};
    const char* symbols[] = {"~`^[]{}<>", ":;'\"\\|", ",!@#$%&*"};
    const char** rows = keyboard == 3 ? symbols : keyboard == 2 ? numbers : letters;
    for (int r = 0; r < 3; ++r) {
      int n = strlen(rows[r]), w = 232 / n, start = (240 - n * w) / 2;
      for (int i = 0; i < n; ++i) {
        char c = rows[r][i];
        if (keyboard == 1) {
          c = char(c - 'a' + 'A');
        }
        btn(start + i * w, 127 + r * 46, w - 2, 38, Str(1, c), [this, c] {
          if (password.size() < 64) {
            password += c;
          }
          dirty = true;
        });
      }
    }
    btn(6, 268, 68, 33, keyboard == 1 ? "小写" : "大写", [this] {
      keyboard = keyboard == 1 ? 0 : 1;
      dirty = true;
    });
    btn(86, 268, 68, 33, keyboard == 2 ? "更多符号" : "数字符号", [this] {
      keyboard = keyboard == 2 ? 3 : 2;
      dirty = true;
    });
    btn(166, 268, 68, 33, "退格", [this] {
      if (!password.empty()) {
        password.pop_back();
      }
      dirty = true;
    });
    btn(16, 309, 75, 31, "空格", [this] {
      if (password.size() < 64) {
        password += ' ';
      }
      dirty = true;
    });
    if (joining) {
      canvas.ui(100, 317, dev.error.empty() ? "正在连接" : dev.error, 12, 128);
    }
    primary(16, 366, 208, "连接", [this] {
      dev.connect(ssid, password);
      joining = true;
      showPass = false;
      networkChanged();
    });
    if (joining && dev.error.empty()) {
      hits.resize(1);  // Keep Back available during association.
    }
    return;
  }
  header("文件服务");
  if (!dev.card || !dev.connected) {
    canvas.ui(16, 66, dev.card ? "尚未连接 Wi-Fi" : "未检测到 TF 卡");
    primary(16, 366, 208, dev.card ? "连接网络" : "检查存储卡", [this] {
      go(dev.card ? View::Wifi : View::CardInfo);
      if (dev.card && dev.wifi) {
        dev.scan();
      }
    });
    return;
  }
  if (!dev.serving) {
    canvas.ui(16, 62, "服务已关闭");
    canvas.ui(16, 105, "在同一 Wi-Fi 下传书、字体", 12);
    canvas.ui(16, 126, "和英语练习，或管理 TF 卡。", 12);
    primary(16, 366, 208, "开启文件服务", [this] {
      dev.service(true);
      if (!dev.serving) {
        msg("服务启动失败，请稍后重试");
      } else {
        dirty = true;
      }
    });
    return;
  }
  if (confirmStop && dev.transferring) {
    canvas.ui(16, 68, "中断当前传输？");
    canvas.ui(16, 105, "未传完的文件需要重新上传。", 12);
    menuLine(172, "继续传输", "next", [this] {
      confirmStop = false;
      dirty = true;
    });
    primary(16, 366, 208, "停止服务", [this] {
      dev.service(false);
      confirmStop = false;
      dirty = true;
    });
    return;
  }
  confirmStop = false;
  canvas.ui(16, 56, dev.transferring ? "正在传输" : dev.browserPaired ? "浏览器已配对" : "等待浏览器连接");
  canvas.ui(16, 103, "浏览器访问地址", 12);
  canvas.ui(16, 125, "http://" + dev.ip, 16, 214);
  canvas.rect(16, 163, 208, 1);
  canvas.ui(16, 182, "配对码", 12);
  canvas.ui(16, 204, dev.pairingExpired ? "已过期" : dev.pin);
  if (!dev.transferring) {
    canvas.border(140, 198, 86, 29);
    canvas.ui(145, 209, "更新配对码", 12);
    hit(129, 188, 105, 45, [this] {
      dev.service(true);
      dirty = true;
    });
  }
  canvas.ui(16, 239, "配对码有效期 5 分钟", 12);
  if (!dev.transferName.empty()) {
    canvas.ui(16, 276, dev.transferName, 16, 208);
    canvas.ui(16, 299, dev.transferResult, 12, 208);
  }
  primary(16, 366, 208, "停止服务", [this] {
    if (dev.transferring) {
      confirmStop = true;
    } else {
      dev.service(false);
    }
    dirty = true;
  });
}
}  // namespace jd
