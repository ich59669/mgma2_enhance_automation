#include <TimerOne.h>
#include <switch_controller.h>

#include "data.h"

// --- LED関連の変数 ---
volatile byte currentDigit = 0;  // 割り込み内で使う変数はvolatileを付ける
byte digits[4] = { 0, 0, 0, 0 };
byte dpState = 0;

// --- 実行制御系変数 ---
bool run = false;                // 実行中
byte loopCount[totalStep];       // ステップ毎の現在のループ回数
Step currentStep;                // 現在実行中のステップ
byte currentStepNum = 0;         // 現在実行中のステップ番号
uint32_t lastStepTime = 0;       // ステップに遷移した時刻
byte initialLoopSetting = 19;    // 初回の大ループ回数変更用
bool isFirstGlobalCycle = true;  // 大外無限ループの初回かどうか

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
  for (byte i = 0; i < totalStep; i++) {
    loopCount[i] = 0;
  }

  loadStep(0);  // 初期ステップを読み込んでおく
}

void loop() {
  button[0].update();
  button[1].update();

  // 停止中にボタン0を押すと、初回の繰り返し回数を変更できる
  if (!run && button[0].wasPressed) {
    initialLoopSetting--;
    if (initialLoopSetting == 0) initialLoopSetting = 19;  // 19 ～ 0回でループ
  }

  if (button[1].wasPressed) {
    run = !run;               // ボタン1を押すと実行状態を切り替える
    lastStepTime = millis();  // シーケンスを開始した時間を保持する
  }

  if (!run) {
    dpState = 0b0100;  // 設定中なら真ん中のドットを点灯
  }

  // Switch自動化のメイン処理
  executeSwitchControl();
  SwitchController().sendReport();

  // 表示の切り替え
  if (run) {
    updateDigits(currentStepNum);  // 実行中はステップ番号を表示
  } else {
    updateDigits(initialLoopSetting);  // 停止中は設定値を表示
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
      byte targetLimit = (currentStepNum == outermostLoopStepNo && isFirstGlobalCycle) ? initialLoopSetting : currentStep.loop;

      if (currentStepNum == outermostLoopStepNo) {
        // 残り回数を計算し、0〜15の範囲に制限
        byte remaining = 0;
        if (targetLimit >= loopCount[currentStep.step]) {
          remaining = targetLimit - loopCount[currentStep.step];
        }
        if (remaining > 15) remaining = 15;

        dpState = remaining;
      }
    }

    // 指定の時間が経過したかチェック
    if (now - lastStepTime >= currentStep.duration()) {
      // ループ開始ステップならループカウンタ増やす
      if (currentStep.loop != 0) {
        loopCount[currentStep.step]++;
      }

      byte targetLoopLimit = (currentStepNum == outermostLoopStepNo && isFirstGlobalCycle) ? initialLoopSetting : currentStep.loop;

      if (currentStep.loop != 0 && loopCount[currentStep.step] > targetLoopLimit) {
        // ループ回数を満たした場合現在のステップのループカウンタをリセットする
        loopCount[currentStep.step] = 0;

        // 大外ループが終わり、最終シーケンスに到達したら初回フラグを下ろす
        if (currentStepNum == lastSequenceStepNo) isFirstGlobalCycle = false;

        // ループ先のステップを読み込む
        loadStep(currentStep.loopNext);
      } else {
        // ループでない場合またはループが足りない場合は次のステップを読み込む
        loadStep(currentStep.next);
      }
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

void loadStep(byte num) {
  if (num >= totalStep) num = 0;  // 変なところを読み込まないようにする
  currentStepNum = num;
  memcpy_P(&currentStep, &sequence[num], sizeof(Step));
}