#pragma once
#include <stdint.h>
#include "classic_layout.h"
#include <stdlib.h>
namespace cydclassic {
inline int clampTop(int top,int count,int rows){int end=count>rows?count-rows:0;return top<0?0:top>end?end:top;}
// A press is a tap while it ends near the control it landed on. Near means within
// SlopNear px of the target (the screen measures it, so a neighbouring control can
// never be reached by slop); wandering out and back is forgiven, leaving by more
// than SlopFar is a deliberate cancel. In a scrollable list a drag claims the press
// for good: mostly-vertical movement of DragVertical px, or DragAny px either way.
// Horizontal jitter below DragAny must not turn a row tap into nothing.
struct Gesture {
  static constexpr int SlopNear=8,SlopFar=24,DragVertical=10,DragAny=16;
  int x=0,y=0,top=0,target=-1;
  bool claimed=false,strayed=false;
  void down(int px,int py,int first,int hit){x=px;y=py;top=first;target=hit;claimed=strayed=false;}
  // `far`: the finger is now more than SlopFar from the captured target.
  bool move(int px,int py,bool far,bool scroll){
    if(scroll){
      const int dx=abs(px-x),dy=abs(py-y);
      if(dx>=DragAny||dy>=DragAny||(dy>=DragVertical&&dy>=dx))claimed=true;
    }
    if(far)strayed=true;
    return claimed||strayed;
  }
  // `near`: the release point is within SlopNear of the captured target.
  bool activate(bool near)const{return !claimed&&!strayed&&target!=-1&&near;}
  int scrollTop(int py,int count,int rows)const{return clampTop(top+(y-py)/layout::RowH,count,rows);}
};
template<class T,unsigned Capacity=8> struct Navigation {
  T entries[Capacity]{};unsigned depth=1;
  T& current(){return entries[depth-1];}
  bool push(const T& page){if(depth>=Capacity)return false;entries[depth++]=page;return true;}
  bool back(){if(depth<=1)return false;--depth;return true;}
  void home(){depth=1;entries[0]=T{};}
};
}
