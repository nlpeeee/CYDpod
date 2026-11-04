// Feed real CST820 decoding + rotation into the unchanged firmware classifier.
#include "cst820_report.h"
#include "touch_gesture.h"
#include <assert.h>
#include <stdio.h>

static TouchEvent feed(cydtouch::PressTracker& tracker, uint32_t ms,
                       const uint8_t* bytes, size_t length, uint8_t rotation,
                       int* ex = nullptr, int* ey = nullptr) {
  const cst820::Report r = cst820::decode(bytes, length);
  int x = 0, y = 0;
  if (r.state == cst820::State::Contact) cst820::screenPoint(r.x, r.y, rotation, x, y);
  return tracker.update(ms, r.state == cst820::State::Contact, x, y, ex, ey);
}
int main() {
  const uint8_t none[] = {0,0,0,0,0,0};
  const uint8_t releaseWrap[] = {0x0c,0,0xff,0xff,0xff,0xff};
  const uint8_t topLeft[] = {5,1,0,0,0,0}; // (0,0) is a valid contact
  const uint8_t bottomRight[] = {0,1,0,239,1,63}; // (239,319)
  const uint8_t tap[] = {5,1,0,40,0,60};
  const uint8_t move[] = {3,1,0,80,0,100};
  const uint8_t badX[] = {0,1,0,240,0,60};
  const uint8_t badY[] = {0,1,0,40,1,64};
  const uint8_t badCount[] = {0,2,0,40,0,60};
  const uint8_t wrapDown[] = {0,1,0xff,0xff,0xff,0xff};
  const uint8_t flags[] = {0xfe,1,0xa0,40,0xb0,60}; // mask non-coordinate high nibbles
  assert(cst820::decode(none,6).state == cst820::State::Released);
  assert(cst820::decode(releaseWrap,6).state == cst820::State::Released);
  assert(cst820::decode(topLeft,6).state == cst820::State::Contact);
  assert(cst820::decode(bottomRight,6).state == cst820::State::Contact);
  assert(cst820::decode(badX,6).state == cst820::State::Invalid);
  assert(cst820::decode(badY,6).state == cst820::State::Invalid);
  assert(cst820::decode(badCount,6).state == cst820::State::Invalid);
  assert(cst820::decode(wrapDown,6).state == cst820::State::Invalid);
  assert(cst820::decode(nullptr,6).state == cst820::State::Invalid);
  assert(cst820::decode(tap,5).state == cst820::State::Invalid);
  assert(cst820::decode(flags,6).x == 40 && cst820::decode(flags,6).y == 60);
  const int edges[4][4] = {{0,0,239,319},{0,239,319,0},{239,319,0,0},{319,0,0,239}};
  for (uint8_t rot = 0; rot < 4; ++rot) {
    cydtouch::PressTracker tracker;
    int x=-1,y=-1;
    assert(feed(tracker,200,topLeft,6,rot) == TOUCH_NONE);
    tracker.landing(x,y); assert(x==edges[rot][0] && y==edges[rot][1]);
    assert(feed(tracker,210,bottomRight,6,rot) == TOUCH_NONE);
    tracker.live(x,y); assert(x==(edges[rot][0]+edges[rot][2])/2 && y==(edges[rot][1]+edges[rot][3])/2);
    assert(feed(tracker,220,releaseWrap,6,rot) == TOUCH_NONE);
    assert(feed(tracker,270,releaseWrap,6,rot,&x,&y) == TOUCH_TAP);
    assert(x==edges[rot][0] && y==edges[rot][1]); // landing survives release wrap
  }
  cydtouch::PressTracker tracker;
  int x,y;
  assert(feed(tracker,200,tap,6,1)==TOUCH_NONE);
  assert(feed(tracker,210,nullptr,0,1)==TOUCH_NONE); // NACK/short read dropout
  assert(feed(tracker,220,badX,6,1)==TOUCH_NONE);    // corrupt frame dropout
  assert(feed(tracker,230,move,6,1)==TOUCH_NONE);   // one press, not split
  tracker.landing(x,y); assert(x==60 && y==199);
  tracker.live(x,y); assert(x==80 && y==179);
  assert(feed(tracker,290,releaseWrap,6,1,&x,&y)==TOUCH_TAP);
  assert(x==60 && y==199);
  assert(feed(tracker,300,none,6,1)==TOUCH_NONE); // exactly one event
  assert(feed(tracker,500,tap,6,1)==TOUCH_NONE);
  assert(feed(tracker,1100,tap,6,1)==TOUCH_NONE);
  assert(feed(tracker,1160,none,6,1)==TOUCH_LONG_PRESS);
  assert(feed(tracker,1400,tap,6,1)==TOUCH_NONE);
  assert(feed(tracker,5400,tap,6,1)==TOUCH_NONE);
  assert(feed(tracker,5460,none,6,1)==TOUCH_RECALIBRATE); // classifier unchanged
  // Driver translates RECAL to LONG; capacitive calibrate_run is a no-op too.
  puts("PASS CST820 real decode -> shared classifier: release wrap, zero/edge coordinates, all rotations, glitches/NACK, landing/live, long/recal");
}
