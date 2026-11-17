#include "App.h"
#include <algorithm>

namespace jd {
using Stage = BatteryCalibration::Stage;

void App::startBatteryCalibration() {
  if (batteryCalibration.running() || batteryCalibration.stage == Stage::Saved) return;
  if (dev.powerBusy() || dev.serving || dev.casting) {
    msg("请先结束文件服务或无线墨屏，再校准电量");
    return;
  }
  batteryRestored = false;
  lastBatteryFrame = dev.uptime();
  batteryCalibration.start(lastBatteryFrame);
  dirty = true;
}
void App::saveBatteryCalibration() {
  if (batteryCalibration.stage != Stage::Review && batteryCalibration.stage != Stage::WriteError) return;
  batteryCalibration.saved(dev.saveBatteryFull(batteryCalibration.candidate));
  dirty = true;
}
void App::resetBatteryCalibration() {
  if (dev.resetBatteryFull()) {
    batteryCalibration.cancel();
    batteryRestored = true;
  } else {
    batteryCalibration.stage = Stage::ResetError;
  }
  dirty = true;
}
void App::tickBatteryCalibration(bool idle) {
  if (!idle || !batteryCalibration.running()) return;
  const auto now = dev.uptime();
  if (batteryCalibration.due(now)) {
    batteryCalibration.collect(now, dev.sampleBattery());
    lastBatteryFrame = now;
    dirty = true;
  } else if (batteryCalibration.stage == Stage::Waiting && uint32_t(now - lastBatteryFrame) >= 5000) {
    lastBatteryFrame = now;
    dirty = true;
  }
}
void App::batteryCalibrationPage() {
  header("电量校准");
  auto value = [this](int y, const Str& label, int number) {
    canvas.ui(16, y, label, 15, 160);
    const auto text = std::to_string(number);
    canvas.ui(224 - Canvas::textWidth(text, 15), y, text, 15, 64);
  };
  auto progress = [this](unsigned percent) {
    canvas.border(16, 158, 208, 8);
    canvas.rect(18, 160, std::min(percent, 100u) * 204 / 100, 4);
  };
  switch (batteryCalibration.stage) {
    case Stage::Ready:
      value(66, "满电参考", dev.batteryFull);
      canvas.ui(16, 94, dev.batteryCalibrated ? "本机校准值" : batteryRestored ? "已恢复默认值" : "默认参考值", 12, 208);
      canvas.rect(16, 123, 208, 1);
      canvas.ui(16, 145, "充满后再校准", 15, 208);
      canvas.ui(16, 181, "充电至指示灯熄灭。", 12, 208);
      canvas.ui(16, 205, "拔掉 USB 电源线。", 12, 208);
      canvas.ui(16, 247, "开始后等待并采集，约 70 秒。", 12, 208);
      canvas.ui(16, 271, "完成采集后再确认保存。", 12, 208);
      if (dev.batteryCalibrated) compact(1, 313, "恢复默认", [this] { resetBatteryCalibration(); });
      primary(16, 366, 208, "已拔线，开始校准", [this] { startBatteryCalibration(); });
      break;
    case Stage::Waiting: {
      const auto elapsed = std::min<uint32_t>(dev.uptime() - batteryCalibration.started, 30000);
      canvas.ui(16, 70, "等待电压稳定", 15, 208);
      canvas.ui(16, 119, "还需 " + std::to_string((30000 - elapsed + 999) / 1000) + " 秒", 15, 208);
      progress(elapsed / 300);
      canvas.ui(16, 207, "请保持 USB 断开。", 12, 208);
      canvas.ui(16, 231, "随后自动采集 5 次。", 12, 208);
      compact(1, 366, "取消", [this] { back(); });
      break;
    }
    case Stage::Sampling:
      canvas.ui(16, 70, "正在采集", 15, 208);
      canvas.ui(16, 119, "已采集 " + std::to_string(batteryCalibration.count) + " / 5 次", 15, 208);
      progress(batteryCalibration.count * 20);
      value(207, "最近读数", batteryCalibration.samples[batteryCalibration.count - 1]);
      canvas.ui(16, 250, "每 10 秒采集一次。", 12, 208);
      canvas.ui(16, 274, "请保持 USB 断开。", 12, 208);
      compact(1, 366, "取消", [this] { back(); });
      break;
    case Stage::Review:
      canvas.ui(16, 70, "采集完成", 15, 208);
      value(121, "建议满电参考", batteryCalibration.candidate);
      canvas.ui(16, 165, "采集 5 次，波动 ±" + std::to_string(batteryCalibration.deviation) + "。", 12, 208);
      canvas.ui(16, 224, "保存后将此值作为 100%。", 12, 208);
      canvas.ui(16, 248, "只校准电量显示。", 12, 208);
      compact(1, 313, "重新采集", [this] { startBatteryCalibration(); });
      primary(16, 366, 208, "保存为满电", [this] { saveBatteryCalibration(); });
      break;
    case Stage::Saved:
      canvas.ui(16, 79, "校准已保存", 15, 208);
      value(134, "满电参考", dev.batteryFull);
      canvas.ui(16, 183, "重启或换卡后仍会保留。", 12, 208);
      canvas.ui(16, 207, "需要时可再次进入本页校准。", 12, 208);
      compact(1, 366, "返回", [this] { back(); });
      break;
    case Stage::Unstable:
      canvas.ui(16, 79, "读数波动较大", 15, 208);
      canvas.ui(16, 123, "本次波动 ±" + std::to_string(batteryCalibration.deviation) + "，暂不保存。", 12, 208);
      canvas.ui(16, 160, "请保持电源线断开，", 12, 208);
      canvas.ui(16, 184, "放置片刻后重新采集。", 12, 208);
      canvas.ui(16, 244, "原满电参考保持不变。", 12, 208);
      primary(16, 366, 208, "重新采集", [this] { startBatteryCalibration(); });
      break;
    case Stage::Invalid:
      canvas.ui(16, 79, "未取得有效读数", 15, 208);
      canvas.ui(16, 132, "请稍后重新采集。", 12, 208);
      canvas.ui(16, 169, "原满电参考保持不变。", 12, 208);
      primary(16, 366, 208, "重新采集", [this] { startBatteryCalibration(); });
      break;
    case Stage::WriteError:
    case Stage::ResetError: {
      const bool reset = batteryCalibration.stage == Stage::ResetError;
      canvas.ui(16, 79, reset ? "未能恢复默认值" : "未能保存校准值", 15, 208);
      canvas.ui(16, 132, "设备设置写入失败。", 12, 208);
      canvas.ui(16, 169, "原满电参考保持不变。", 12, 208);
      primary(16, 366, 208, reset ? "重试恢复" : "重试保存", [this, reset] {
        if (reset) resetBatteryCalibration();
        else saveBatteryCalibration();
      });
      break;
    }
  }
}
}  // namespace jd
