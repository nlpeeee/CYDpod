#pragma once
// Host drawing recorder for firmware UI regression checks. SVG text uses Arial,
// not the device's bitmap font; this is layout evidence, not a hardware capture.
#include "Arduino.h"
#include <sstream>
#include <fstream>
#include <vector>
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8
class TFT_eSPI {
  std::ostringstream svg;int w=240,h=320,datum=0,font=2,cursorX=0,cursorY=0;
  uint16_t fg=0,bg=0xffff;bool viewport=false;int clip=0;unsigned rotation=0;
  static std::string color(uint16_t c){char b[16];snprintf(b,sizeof(b),"#%02x%02x%02x",((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31);return b;}
  static std::string escape(const char* p){std::string s;for(;*p;++p){if(*p=='&')s+="&amp;";else if(*p=='<')s+="&lt;";else if(*p=='>')s+="&gt;";else s+=*p;}return s;}
 public:
  std::vector<std::string> labels;
  int fillCalls=0,rowFillCalls=0;size_t labelsAtReset=0;
  void resetDrawCounts(){fillCalls=rowFillCalls=0;labelsAtReset=labels.size();}
  int width()const{return w;}int height()const{return h;}
  void invertDisplay(bool){}
  unsigned getRotation()const{return rotation;}
  void setRotation(unsigned r){rotation=r;w=(r&1)?320:240;h=(r&1)?240:320;}
  void save(const char* file){std::ofstream f(file);f<<"<svg xmlns='http://www.w3.org/2000/svg' width='"<<w<<"' height='"<<h<<"' viewBox='0 0 "<<w<<" "<<h<<"'>"<<svg.str()<<"</svg>";}
  void fillScreen(uint16_t c){svg.str("");svg.clear();labels.clear();viewport=false;fillRect(0,0,w,h,c);}
  void fillRect(int x,int y,int width,int height,uint16_t c){++fillCalls;if(y>=36&&height==40)++rowFillCalls;if(width>0&&height>0)svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<width<<"' height='"<<height<<"' fill='"<<color(c)<<"'/>";}
  void drawRect(int x,int y,int width,int height,uint16_t c){fillRect(x,y,width,1,c);fillRect(x,y+height-1,width,1,c);fillRect(x,y,1,height,c);fillRect(x+width-1,y,1,height,c);}
  void fillRoundRect(int x,int y,int width,int height,int radius,uint16_t c){svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<width<<"' height='"<<height<<"' rx='"<<radius<<"' fill='"<<color(c)<<"'/>";}
  void drawRoundRect(int x,int y,int width,int height,int radius,uint16_t c){svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<width-1<<"' height='"<<height-1<<"' rx='"<<radius<<"' fill='none' stroke='"<<color(c)<<"'/>";}
  void drawFastHLine(int x,int y,int n,uint16_t c){fillRect(x,y,n,1,c);}
  void drawFastVLine(int x,int y,int n,uint16_t c){fillRect(x,y,1,n,c);}
  void drawLine(int x,int y,int a,int b,uint16_t c){svg<<"<path d='M"<<x<<","<<y<<" L"<<a<<","<<b<<"' stroke='"<<color(c)<<"'/>";}
  void fillCircle(int x,int y,int r,uint16_t c){svg<<"<circle cx='"<<x<<"' cy='"<<y<<"' r='"<<r<<"' fill='"<<color(c)<<"'/>";}
  void fillTriangle(int x,int y,int a,int b,int c,int d,uint16_t colorValue){svg<<"<path d='M"<<x<<","<<y<<" L"<<a<<","<<b<<" L"<<c<<","<<d<<" Z' fill='"<<color(colorValue)<<"'/>";}
  uint16_t alphaBlend(uint8_t alpha,uint16_t a,uint16_t b){unsigned r=(((a>>11)&31)*alpha+((b>>11)&31)*(255-alpha))/255;unsigned g=(((a>>5)&63)*alpha+((b>>5)&63)*(255-alpha))/255;unsigned blue=((a&31)*alpha+(b&31)*(255-alpha))/255;return uint16_t((r<<11)|(g<<5)|blue);}
  int fontHeight(unsigned f)const{return f==1?8:16;}
  int textWidth(const String& s,unsigned f=2)const{return int(s.length())*(f==1?6:8);}
  void setTextColor(uint16_t colorValue,uint16_t background){fg=colorValue;bg=background;}
  void setTextDatum(unsigned d){datum=d;}
  void setTextFont(unsigned f){font=f;}
  void setTextWrap(bool){}
  void setCursor(int x,int y){cursorX=x;cursorY=y;}
  void setViewport(int x,int y,int width,int height){resetViewport();++clip;svg<<"<defs><clipPath id='clip"<<clip<<"'><rect width='"<<width<<"' height='"<<height<<"'/></clipPath></defs><g transform='translate("<<x<<","<<y<<")' clip-path='url(#clip"<<clip<<")'>";viewport=true;}
  void resetViewport(){if(viewport){svg<<"</g>";viewport=false;}}
  void drawString(const String& s,int x,int y,unsigned f=2){int width=textWidth(s,f),height=fontHeight(f);int left=x-(datum%3==1?width/2:datum%3==2?width:0),top=y-(datum/3==1?height/2:datum/3==2?height:0);
    fillRect(left,top,width,height,bg);labels.push_back(s.c_str());svg<<"<text x='"<<left<<"' y='"<<top+height-3<<"' font-family='Arial,sans-serif' font-size='"<<(f==1?9:16)<<"' fill='"<<color(fg)<<"'>"<<escape(s.c_str())<<"</text>";
  }
  void classicText(const char* text,int x,int y,int height,uint16_t fg,uint16_t){labels.push_back(text);svg<<"<text x='"<<x<<"' y='"<<y+height/2-3<<"' font-family='Arial,sans-serif' font-size='"<<height<<"' fill='"<<color(fg)<<"'>"<<escape(text)<<"</text>";}
  std::string drawing()const{return svg.str();}
  int colorCount(uint16_t c)const{const std::string content=svg.str(),needle="fill='"+color(c)+"'";int n=0;size_t at=0;while((at=content.find(needle,at))!=std::string::npos){++n;at+=needle.size();}return n;}
  void println(const char* s){drawString(String(s),cursorX,cursorY,font);cursorY+=fontHeight(font);}
};
