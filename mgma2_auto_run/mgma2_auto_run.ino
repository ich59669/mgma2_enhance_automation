#include <switch_controller.h>
#include "data.h"

// --- 実行制御系変数 ---
bool run = false;                // 実行中フラグ
byte loopCount[totalStep];       // ステップ毎の現在のループ回数
Step currentStep;                // 現在実行中のステップ
byte currentStepNum = 0;         // 現在実行中のステップ番号
uint32_t lastStepTime = 0;       // ステップに遷移した時刻
uint32_t startTime = 0;          // 自動開始判定用の時刻

void setup() {
  // ループカウンタ初期化
  for (byte i = 0; i < totalStep; i++) {
    loopCount[i] = 0;
  }

  loadStep(0); // 初期ステップを読み込む
  
  // 起動時の時刻を記録
  startTime = millis();
}

void loop() {
  // 設定時間経過したら自動的に実行フラグを立てる
  if (!run && (millis() - startTime >= START_DELAY_MS)) {
    run = true;
    lastStepTime = millis(); // 最初のステップの開始時刻を同期
  }

  // 実行中のみSwitch操作を送信
  if (run) {
    executeSwitchControl();
    SwitchController().sendReport();
  }
}

void executeSwitchControl() {
  uint32_t now = millis();

  // 現在のステップの操作を実行
  if (currentStep.hat != Hat::CENTER) {
    SwitchController().pressHatButton(currentStep.hat);
  } else {
    SwitchController().releaseHatButton();
  }
  
  if (currentStep.press) {
    SwitchController().pressButton(currentStep.button);
  } else {
    SwitchController().releaseButton(currentStep.button);
  }

  // 指定の時間が経過したかチェック
  if (now - lastStepTime >= currentStep.duration()) {
    if (currentStep.loop != 0) {
      loopCount[currentStep.step]++;
    }

    if (currentStep.loop != 0 && loopCount[currentStep.step] > currentStep.loop) {
      loopCount[currentStep.step] = 0;
      loadStep(currentStep.loopNext);
    } else {
      loadStep(currentStep.next);
    }
    lastStepTime = now;
  }
}

void loadStep(byte num) {
  if (num >= totalStep) num = 0;
  currentStepNum = num;
  memcpy_P(&currentStep, &sequence[num], sizeof(Step));
}