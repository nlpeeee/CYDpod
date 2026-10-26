#pragma once
#include <stdint.h>
#include <stdlib.h>
namespace cydclassic {
inline int clampTop(int top,int count,int rows){int end=count>rows?count-rows:0;return top<0?0:top>end?end:top;}
// Movement claims the entire press, including long drags and excursions back
// into the original target. A later return cannot turn a drag into a tap.
struct Gesture {
  int x=0,y=0,top=0,target=-1;
  bool claimed=false,cancelled=false;
  void down(int px,int py,int first,int hit){x=px;y=py;top=first;target=hit;claimed=cancelled=false;}
  bool move(int px,int py,int hit,bool scroll){
    (void)scroll;
    if(abs(px-x)>=10||abs(py-y)>=10)claimed=true;
    if(hit!=target)cancelled=true;
    return claimed||cancelled;
  }
  bool activate(int hit)const{return !claimed&&!cancelled&&target!=-1&&hit==target;}
  int scrollTop(int py,int count,int rows)const{return clampTop(top+(y-py)/44,count,rows);}
};
template<class T,unsigned Capacity=8> struct Navigation {
  T entries[Capacity]{};unsigned depth=1;
  T& current(){return entries[depth-1];}
  bool push(const T& page){if(depth>=Capacity)return false;entries[depth++]=page;return true;}
  bool back(){if(depth<=1)return false;--depth;return true;}
  void home(){depth=1;entries[0]=T{};}
};
}
