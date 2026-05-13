#ifndef TYPES_H
#define TYPES_H

// --- ボタン状態定義 ---
struct ButtonState {
  const byte pin;    // 入力ピン番号
  bool state;        // 今押されているか
  bool lastState;    // 直前に押されていたか
  bool wasPressed;   // 今押された瞬間か
  bool wasReleased;  // 今離された瞬間か
  unsigned long lastDebounceTime;

  static constexpr byte debounceDelay = 50;  // チャタリング判定時間 (ms)

  // コンストラクタ
  ButtonState(byte p)
    : pin(p), state(false), lastState(HIGH), wasPressed(false), wasReleased(false), lastDebounceTime(0) {}

  void update() {
    const bool currentRead = digitalRead(pin);
    wasPressed = false;
    wasReleased = false;

    // 1. チャタリング対策（前回読み取った状態から変化があった場合のみタイマーリセット）
    if (currentRead != lastState) {
      lastDebounceTime = millis();
    }

    // 2. 最後に状態が変化してから一定時間(50ms)以上経過したかチェック
    if ((millis() - lastDebounceTime) > debounceDelay) {

      // 3. 状態が確定し、かつ「HIGHからLOWに変わった瞬間」だけを検知（エッジ検出）
      // ※INPUT_PULLUPなので、押すとLOW、離すとHIGH
      if (currentRead == LOW && state == false) {
        state = true;       // 「今押されている」ことを記録
        wasPressed = true;  // 「押された瞬間だけ」
      }
    }

    // 4. ボタンが離されたことを検知してフラグを下ろす
    if (currentRead == HIGH && state == true) {
      state = false;
      wasReleased = true;
    }

    lastState = currentRead;  // 今回の状態を保存
  }
};

// --- ループ回数オーバーライド定義 ---
struct LoopOverride {
  uint16_t stepNum;  // 上書き対象のステップ番号
  byte count;        // 上書きするループ回数（0 = 無効）
};

// --- ステップ定義 ---
struct Step {
  uint16_t step;
  uint8_t hat;
  uint16_t button;
  bool press;
  uint16_t _dur;
  uint16_t next;
  byte loop;
  uint16_t loopNext;

  uint32_t duration() {
    return (uint32_t)_dur * 100;
  }
};

#endif