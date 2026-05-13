#include <TimerOne.h>
#include <switch_controller.h>

#include "data.h"

// --- LED関連の変数 ---
volatile byte currentDigit = 0;  // 割り込み内で使う変数はvolatileを付ける
byte digits[4] = { 0, 0, 0, 0 };
byte dpState = 0;

// --- 実行制御系変数 ---
bool run = false;                // 実行中
byte loopCount[MAX_TOTAL_STEP];  // ステップ毎の現在のループ回数
Step currentStep;                // 現在実行中のステップ
uint16_t currentStepNum = 0;         // 現在実行中のステップ番号
uint32_t lastStepTime = 0;       // ステップに遷移した時刻
byte initialLoopSetting;         // 初回の大ループ回数変更用
uint16_t initialLoopTimeRemain;       // 初回の大ループの残り時間
uint16_t loopTimeRemain;              // 大ループの残り時間
bool isFirstGlobalCycle = true;  // 無限ループの初回かどうか
byte activeSeq = 0;              // 現在アクティブなシーケンス番号

unsigned long btn0PressTime = 0;      // ボタン0を押した時刻（停止中のみ記録）
bool btn0LongPressTriggered = false;  // 長押し処理済みフラグ
uint32_t seqDisplayUntil = 0;         // シーケンス番号表示の終了時刻

const unsigned long LONG_PRESS_MS  = 1000;  // 長押し判定時間 (ms)
const unsigned long SEQ_DISPLAY_MS = 2000;  // シーケンス番号表示時間 (ms)

void setup() {
  // セグメント・ドットピンを出力に設定（初期状態はHIGH = 消灯）
  for (byte i = 0; i < 7; i++) {
    pinMode(segLedPins[i], OUTPUT);
    digitalWrite(segLedPins[i], HIGH);
  }
  pinMode(dpPin, OUTPUT);
  digitalWrite(dpPin, HIGH);

  // 桁ピンを出力に設定（初期状態はLOW = 選択解除）
  for (byte i = 0; i < 4; i++) {
    pinMode(digitPins[i], OUTPUT);
    digitalWrite(digitPins[i], LOW);
  }

  // ボタン入力をプルアップ入力に設定
  for (byte i = 0; i < btncnt; i++) {
    pinMode(button[i].pin, INPUT_PULLUP);
  }

  // 割り込みの設定
  Timer1.initialize(2000);  // マイクロ秒
  Timer1.attachInterrupt(driveLED);

  // ループカウンタ初期化
  memset(loopCount, 0, sizeof(loopCount));

  // 初回のループ所要時間設定用(最大値)
  initialLoopSetting = seqFruitSec[activeSeq] / seqCLoopSec[activeSeq] + 1;
  initialLoopTimeRemain = initialLoopSetting * seqCLoopSec[activeSeq];

  loadStep(0);  // 初期ステップを読み込んでおく
}

void loop() {
  button[0].update();
  button[1].update();

  // 停止中のボタン0操作：長押しでシーケンス切り替え、短押しで初回ループ回数変更
  if (!run) {
    if (button[0].wasPressed) {
      btn0PressTime = millis();
      btn0LongPressTriggered = false;
    }
    if (button[0].state && !btn0LongPressTriggered) {
      if (millis() - btn0PressTime >= LONG_PRESS_MS) {
        btn0LongPressTriggered = true;
        activeSeq = (activeSeq + 1) % SEQUENCE_COUNT;
        resetSequenceState();
        seqDisplayUntil = millis() + SEQ_DISPLAY_MS;
      }
    }
    if (button[0].wasReleased && !btn0LongPressTriggered) {
      if ((long)initialLoopSetting * seqCLoopSec[activeSeq] > seqFruitSec[activeSeq]) {
        initialLoopSetting = 0;
      }
      initialLoopSetting++;
      initialLoopTimeRemain = initialLoopSetting * seqCLoopSec[activeSeq];
    }
  }

  if (button[1].wasPressed) {
    run = !run;               // ボタン1を押すと実行状態を切り替える
    lastStepTime = millis();  // シーケンスを開始した時間を保持する
  }

  // Switch自動化のメイン処理
  executeSwitchControl();
  SwitchController().sendReport();

  // 表示の切り替え
  if (run) {
    updateDigits(currentStepNum);  // 実行中はステップ番号を表示
  } else if (millis() < seqDisplayUntil) {
    dpState = 0;
    updateDigits(activeSeq);  // シーケンス切り替え直後はシーケンス番号を表示
  } else {
    dpState = 0b1010;  // 停止中なら設定時間の区切りとしてDPを表示
    if (isFirstGlobalCycle) {
      updateDigits(secondTo4digits(initialLoopTimeRemain));  // 最初のループでは設定時間を表示
    } else {
      updateDigits(secondTo4digits(loopTimeRemain));  // 残り時間を表示
    }
  }
}


// LED点灯割り込み
void driveLED() {
  // 1. 全消灯（ゴースト対策）
  for (int i = 0; i < 4; i++) digitalWrite(digitPins[i], LOW);
  for (int i = 0; i < 7; i++) digitalWrite(segLedPins[i], HIGH);
  digitalWrite(dpPin, HIGH);

  // 2. 現在の桁の数字をセット
  byte val = digits[currentDigit];
  for (int i = 0; i < 7; i++) {
    digitalWrite(segLedPins[i], !(bitRead(segBitsByNum[val], i)));
  }
  digitalWrite(dpPin, !(dpState & 1 << currentDigit));

  // 3. 現在の桁だけON
  digitalWrite(digitPins[currentDigit], HIGH);

  // 4. 次の桁へ進む 4桁2ビット
  currentDigit = (currentDigit + 1) % 4;
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

    // --- ドット表示ロジック ---
    if (currentStep.loop != 0) {
      // 大外のループの「目標回数」を取得
      byte targetLimit = (currentStepNum == seqOutermostLoopNo[activeSeq] && isFirstGlobalCycle) ? initialLoopSetting : currentStep.loop;

      if (currentStepNum == seqOutermostLoopNo[activeSeq]) {
        // 残り回数を計算
        byte remaining = 0;
        if (targetLimit >= loopCount[currentStep.step]) {
          remaining = targetLimit - loopCount[currentStep.step];
        }
        loopTimeRemain = remaining * seqCLoopSec[activeSeq];
        dpState = (remaining > 15 ? 15 : remaining);
      }
    }

    // 指定の時間が経過したかチェック
    if (now - lastStepTime >= currentStep.duration()) {
      // ループ開始ステップならループカウンタ増やす
      if (currentStep.loop != 0) {
        loopCount[currentStep.step]++;
      }

      byte targetLoopLimit = (currentStepNum == seqOutermostLoopNo[activeSeq] && isFirstGlobalCycle) ? initialLoopSetting : currentStep.loop;

      if (currentStep.loop != 0 && loopCount[currentStep.step] > targetLoopLimit) {
        // ループ回数を満たした場合現在のステップのループカウンタをリセットする
        loopCount[currentStep.step] = 0;
        // ループ先のステップを読み込む
        loadStep(currentStep.loopNext);
      } else {
        // ループでない場合またはループが足りない場合は次のステップを読み込む
        loadStep(currentStep.next);
      }
      // 最終シーケンスに到達したら初回フラグを下ろす
      if (currentStepNum == seqLastSeqNo[activeSeq]) isFirstGlobalCycle = false;
      lastStepTime = now;  // ステップに移行した時刻を記録
    }
  }
}

void updateDigits(int num) {
  int temp = (num < 0 || num > 9999) ? 9999 : num;
  for (int i = 0; i < 4; i++) {
    digits[i] = temp % 10;
    temp = temp / 10;
  }
}

// 秒数から hmms の4桁に変換する。
int secondTo4digits(int second) {
  if (second <= 0) {
    return 0;
  }
  byte h = (second / 3600) % 10;  // 時の1桁目 (0-9)
  byte m = (second % 3600) / 60;  // 分 (00-59)
  byte s10 = (second % 60) / 10;  // 秒の10の位 (0-5)

  return (h * 1000) + (m * 10) + s10;
}

void loadStep(uint16_t num) {
  if (num >= seqTotalStep[activeSeq]) num = 0;
  currentStepNum = num;
  const Step* seq = (const Step*)pgm_read_ptr(&seqPtrs[activeSeq]);
  memcpy_P(&currentStep, &seq[num], sizeof(Step));
}

void resetSequenceState() {
  memset(loopCount, 0, sizeof(loopCount));
  initialLoopSetting = seqFruitSec[activeSeq] / seqCLoopSec[activeSeq] + 1;
  initialLoopTimeRemain = initialLoopSetting * seqCLoopSec[activeSeq];
  loopTimeRemain = 0;
  isFirstGlobalCycle = true;
  loadStep(0);
}