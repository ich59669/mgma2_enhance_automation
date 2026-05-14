#include <EEPROM.h>
#include <TimerOne.h>
#include <switch_controller.h>

#include "data.h"

const uint8_t EEPROM_MAGIC      = 0xA5;
const int     EEPROM_ADDR_MAGIC = 0;
const int     EEPROM_ADDR_SEQ   = 1;

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
uint16_t loopTimeRemain;              // 大ループの残り時間
LoopOverride loopOverride;       // 1周目のみのループ回数上書き設定
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

  // EEPROMから選択シーケンスを復元
  if (EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC) {
    byte saved = EEPROM.read(EEPROM_ADDR_SEQ);
    if (saved < SEQUENCE_COUNT) activeSeq = saved;
  }

  loopOverride = { seqOutermostLoopNo[activeSeq], (byte)(seqFruitSec[activeSeq] / seqCLoopSec[activeSeq] + 1) };

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
        EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
        EEPROM.update(EEPROM_ADDR_SEQ, activeSeq);
        resetSequenceState();
        seqDisplayUntil = millis() + SEQ_DISPLAY_MS;
      }
    }
    if (button[0].wasReleased && !btn0LongPressTriggered) {
      if ((long)loopOverride.count * seqCLoopSec[activeSeq] > seqFruitSec[activeSeq]) {
        loopOverride.count = 0;
      }
      loopOverride.count++;
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
    uint32_t elapsed = millis() - lastStepTime;
    uint32_t dur = currentStep.duration();
    uint32_t progress = (dur > 0) ? (elapsed * 10 / dur) : 0;
    if (progress > 9) progress = 9;
    updateDigits(currentStepNum + progress * 1000);
  } else if (millis() < seqDisplayUntil) {
    dpState = 0;
    updateDigits(activeSeq);  // シーケンス切り替え直後はシーケンス番号を表示
  } else {
    dpState = 0b1010;  // 停止中なら設定時間の区切りとしてDPを表示
    if (loopOverride.count > 0) {
      updateDigits(secondTo4digits(loopOverride.count * seqCLoopSec[activeSeq]));  // オーバーライド設定時間を表示
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

byte getLoopTarget() {
  if (loopOverride.count > 0 && currentStepNum == loopOverride.stepNum) {
    return loopOverride.count;
  }
  return currentStep.loop;
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
    if (currentStep.loop != 0 && currentStepNum == seqOutermostLoopNo[activeSeq]) {
      byte targetLimit = getLoopTarget();
      byte remaining = (targetLimit >= loopCount[currentStep.step]) ? targetLimit - loopCount[currentStep.step] : 0;
      loopTimeRemain = remaining * seqCLoopSec[activeSeq];
      dpState = (remaining > 15 ? 15 : remaining);
    }

    // 指定の時間が経過したかチェック
    if (now - lastStepTime >= currentStep.duration()) {
      if (currentStep.loop != 0) {
        loopCount[currentStep.step]++;
      }
      byte limit = getLoopTarget();
      if (currentStep.loop != 0 && loopCount[currentStep.step] > limit) {
        loopCount[currentStep.step] = 0;
        if (currentStepNum == loopOverride.stepNum) {
          loopOverride.count = 0;  // 1周完了でオーバーライドを消費
        }
        loadStep(currentStep.loopNext);
      } else {
        loadStep(currentStep.next);
      }
      lastStepTime = now;
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
  loopTimeRemain = 0;
  loopOverride = { seqOutermostLoopNo[activeSeq], (byte)(seqFruitSec[activeSeq] / seqCLoopSec[activeSeq] + 1) };
  loadStep(0);
}