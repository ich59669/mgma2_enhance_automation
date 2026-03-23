#include <switch_controller.h>
#include "data.h"

// --- 実行制御系変数 ---
bool run = false;                // 実行中
byte loopCount[totalStep];       // ステップ毎の現在のループ回数
Step currentStep;                // 現在実行中のステップ
byte currentStepNum = 0;         // 現在実行中のステップ番号
uint32_t lastStepTime = 0;       // ステップに遷移した時刻
byte initialLoopSetting;         // 初回の大ループ回数変更用
bool isFirstGlobalCycle = true;  // 無限ループの初回かどうか

void setup() {
  // ボタン入力をプルアップ入力に設定
  for (byte i = 0; i < btncnt; i++) {
    pinMode(button[i].pin, INPUT_PULLUP);
  }

  // ループカウンタ初期化
  for (byte i = 0; i < totalStep; i++) {
    loopCount[i] = 0;
  }

  // 初回のループ回数設定用
  initialLoopSetting = fruitRipenTimeSecond / colosseumLoopTimeSecond + 1;

  loadStep(0); // 初期ステップを読み込む
}

void loop() {
  button[0].update();
  button[1].update();

  // 停止中にボタン0を押すと、初回の繰り返し回数を変更できる
  if (!run && button[0].wasPressed) {
    if (initialLoopSetting * colosseumLoopTimeSecond > fruitRipenTimeSecond ) {
      initialLoopSetting = 0;
    }
    initialLoopSetting++;
  }

  if (button[1].wasPressed) {
    run = !run;               // ボタン1を押すと実行状態を切り替える
    lastStepTime = millis();  // シーケンスを開始した時間を保持
  }

  // Switch自動化のメイン処理
  executeSwitchControl();
  SwitchController().sendReport();
}

void executeSwitchControl() {
  if (run) {
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

      byte targetLoopLimit = (currentStepNum == outermostLoopStepNo && isFirstGlobalCycle) ? initialLoopSetting : currentStep.loop;

      if (currentStep.loop != 0 && loopCount[currentStep.step] > targetLoopLimit) {
        loopCount[currentStep.step] = 0;
        loadStep(currentStep.loopNext);
      } else {
        loadStep(currentStep.next);
      }
      
      // 最終シーケンスに到達したら初回フラグを下ろす
      if (currentStepNum == lastSequenceStepNo) isFirstGlobalCycle = false;
      lastStepTime = now;
    }
  }
}

void loadStep(byte num) {
  if (num >= totalStep) num = 0;
  currentStepNum = num;
  memcpy_P(&currentStep, &sequence[num], sizeof(Step));
}