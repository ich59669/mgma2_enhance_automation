#ifndef TYPES_H
#define TYPES_H

// --- ステップ定義 ---
struct Step {
  byte step;
  uint8_t hat;
  uint16_t button;
  bool press;
  uint16_t _dur;
  byte next;
  byte loop;
  byte loopNext;

  uint32_t duration() {
    return _dur * 100;
  }
};

#endif