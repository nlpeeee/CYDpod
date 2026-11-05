#pragma once
// Host drawing recorder for firmware UI regression checks. The SVG keeps host Arial text
// as layout evidence only. The RGB565 framebuffer behind savePng() rasterises text from the
// REAL TFT_eSPI glyph tables (Font16.c font 2, glcdfont font 1) and the production helper's
// GFX font bitmaps, so a PNG shows the pixels the panel receives. Shapes (round rects,
// circles, lines, triangles) are host approximations, not TFT_eSPI's exact rasteriser.
#include "Arduino.h"
#define PROGMEM
#define LOAD_GFXFF
#include <Fonts/GFXFF/gfxfont.h>
#include <Fonts/Font16.h>
#include <sstream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <Fonts/glcdfont.c>
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
  uint16_t fg=0,bg=0xffff;bool viewport=false,viewportDatum=false;int clip=0;unsigned rotation=0;
  std::vector<uint16_t> fb=std::vector<uint16_t>(320*320,0xffff);int ox=0,oy=0,cx0=0,cy0=0,cx1=320,cy1=320;
  static std::string color(uint16_t c){char b[16];snprintf(b,sizeof(b),"#%02x%02x%02x",((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31);return b;}
  static std::string escape(const char* p){std::string s;for(;*p;++p){if(*p=='&')s+="&amp;";else if(*p=='<')s+="&lt;";else if(*p=='>')s+="&gt;";else s+=*p;}return s;}
 public:
  struct Ink{std::string text;int x0,y0,x1,y1;bool cell;}; // x1/y1 exclusive; absolute panel pixels
  std::vector<std::string> labels;std::vector<Ink> inks;int inkAt=-1;long largestFill=0;
  int fillCalls=0,rowFillCalls=0;size_t labelsAtReset=0;
  void resetDrawCounts(){fillCalls=rowFillCalls=0;largestFill=0;labelsAtReset=labels.size();}
  int width()const{return viewportDatum?viewW:w;}int height()const{return viewportDatum?viewH:h;}
  int viewW=0,viewH=0;
  struct Window {int x,y,w,h;unsigned pixels=0,bursts=0;};
  std::vector<Window> windows;unsigned streamStarts=0,streamEnds=0,pixelCalls=0;
  void resetStreamCounts(){windows.clear();streamStarts=streamEnds=pixelCalls=0;}
  int getOriginX()const{return ox;}int getOriginY()const{return oy;}
  bool checkViewport(int x,int y,int width,int height)const{
    x+=ox;y+=oy;return width>0&&height>0&&std::max(x,cx0)<std::min(x+width,cx1)&&std::max(y,cy0)<std::min(y+height,cy1);
  }
  // Match installed driver's raw API: absolute panel coordinates, NO origin or
  // viewport clipping. A faulty caller cannot be rescued by the host recorder.
  void setAddrWindow(int x,int y,int width,int height){
    if(x<0||y<0||width<=0||height<=0||x+width>w||y+height>h)throw std::runtime_error("unclipped raw address window");
    windows.push_back({x,y,width,height});
  }
  void pushColors(uint16_t* colors,uint32_t n,bool swap=true){
    if(windows.empty()||!swap)throw std::runtime_error("invalid raw burst");
    Window& v=windows.back();if(v.pixels+n>unsigned(v.w*v.h))throw std::runtime_error("window overflow");
    ++v.bursts;
    for(unsigned i=0;i<n;++i){unsigned at=v.pixels++;int x=v.x+at%v.w,y=v.y+at/v.w;fb[size_t(y)*320+x]=colors[i];}
    // SVG is secondary geometry evidence; PNG above is the exact RGB565 stream.
    int start=0;while(start<int(n)){int end=start+1;while(end<int(n)&&colors[end]==colors[start])++end;
      unsigned at=v.pixels-n+start;svg<<"<rect x='"<<v.x+int(at%v.w)-ox<<"' y='"<<v.y+int(at/v.w)-oy<<"' width='"<<end-start<<"' height='1' fill='"<<color(colors[start])<<"'/>";start=end;}
  }
  void invertDisplay(bool){}
  unsigned getRotation()const{return rotation;}
  void setRotation(unsigned r){rotation=r;w=(r&1)?320:240;h=(r&1)?240:320;resetViewport();}
  void save(const char* file){std::ofstream f(file);f<<"<svg xmlns='http://www.w3.org/2000/svg' width='"<<w<<"' height='"<<h<<"' viewBox='0 0 "<<w<<" "<<h<<"'>"<<svg.str()<<"</svg>";}
  void fillScreen(uint16_t c){inks.clear();inkAt=-1;ox=oy=0;cx0=cy0=0;cx1=w;cy1=h;svg.str("");svg.clear();labels.clear();viewport=viewportDatum=false;fillRect(0,0,w,h,c);}
  void fbRect(int x,int y,int width,int height,uint16_t c){x+=ox;y+=oy;int x1=std::min({x+width,cx1,w}),y1=std::min({y+height,cy1,h});for(int j=std::max({y,cy0,0});j<y1;++j)for(int i=std::max({x,cx0,0});i<x1;++i)fb[size_t(j)*320+i]=c;}
  void drawPixel(int x,int y,uint16_t c){++pixelCalls;fbRect(x,y,1,1,c);}
  void fillRect(int x,int y,int width,int height,uint16_t c){inkAt=-1;largestFill=std::max(largestFill,long(width)*height);++fillCalls;fbRect(x,y,width,height,c);if(y>=36&&height==40)++rowFillCalls;if(width>0&&height>0)svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<width<<"' height='"<<height<<"' fill='"<<color(c)<<"'/>";}
  void drawRect(int x,int y,int width,int height,uint16_t c){fillRect(x,y,width,1,c);fillRect(x,y+height-1,width,1,c);fillRect(x,y,1,height,c);fillRect(x+width-1,y,1,height,c);}
  void fbRound(int x,int y,int width,int height,int r,uint16_t c,bool ring){for(int j=0;j<height;++j)for(int i=0;i<width;++i){auto in=[&](int a,int b,int rr,int wd,int ht){int dx=a<rr?rr-a:a>=wd-rr?a-(wd-rr-1):0,dy=b<rr?rr-b:b>=ht-rr?b-(ht-rr-1):0;return dx*dx+dy*dy<=rr*rr+rr/2;};bool inside=in(i,j,r,width,height);if(ring&&inside&&width>2&&height>2&&i>0&&j>0&&i<width-1&&j<height-1&&in(i-1,j-1,std::max(0,r-1),width-2,height-2))inside=false;if(inside)fbRect(x+i,y+j,1,1,c);}}
  void fillRoundRect(int x,int y,int width,int height,int radius,uint16_t c){fbRound(x,y,width,height,radius,c,false);svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<width<<"' height='"<<height<<"' rx='"<<radius<<"' fill='"<<color(c)<<"'/>";}
  void drawRoundRect(int x,int y,int width,int height,int radius,uint16_t c){fbRound(x,y,width,height,radius,c,true);svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<width-1<<"' height='"<<height-1<<"' rx='"<<radius<<"' fill='none' stroke='"<<color(c)<<"'/>";}
  // Glyph runs of the production text helper (between noteLabel and endWrite) are ink: they grow that
  // label's bounding box and are not counted as fills.
  void drawFastHLine(int x,int y,int n,uint16_t c){if(inkAt>=0&&n>0){Ink& k=inks[size_t(inkAt)];const int ax=x+ox,ay=y+oy;k.x0=std::min(k.x0,ax);k.y0=std::min(k.y0,ay);k.x1=std::max(k.x1,ax+n);k.y1=std::max(k.y1,ay+1);fbRect(x,y,n,1,c);svg<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<n<<"' height='1' fill='"<<color(c)<<"'/>";return;}fillRect(x,y,n,1,c);}
  void drawFastVLine(int x,int y,int n,uint16_t c){fillRect(x,y,1,n,c);}
  void drawLine(int x,int y,int a,int b,uint16_t c){{int dx=std::abs(a-x),dy=-std::abs(b-y),sx=x<a?1:-1,sy=y<b?1:-1,e=dx+dy;for(;;){fbRect(x,y,1,1,c);if(x==a&&y==b)break;int e2=2*e;if(e2>=dy){e+=dy;x+=sx;}if(e2<=dx){e+=dx;y+=sy;}}}svg<<"<path d='M"<<x<<","<<y<<" L"<<a<<","<<b<<"' stroke='"<<color(c)<<"'/>";}
  void fillCircle(int x,int y,int r,uint16_t c){for(int j=-r;j<=r;++j)for(int i=-r;i<=r;++i)if(i*i+j*j<=r*r+r/2)fbRect(x+i,y+j,1,1,c);svg<<"<circle cx='"<<x<<"' cy='"<<y<<"' r='"<<r<<"' fill='"<<color(c)<<"'/>";}
  void fillTriangle(int x,int y,int a,int b,int c,int d,uint16_t colorValue){{int lo=std::min({y,b,d}),hi=std::max({y,b,d}),l=std::min({x,a,c}),r=std::max({x,a,c});auto e=[](int px,int py,int qx,int qy,int tx,int ty){return (qx-px)*(ty-py)-(qy-py)*(tx-px);};for(int j=lo;j<=hi;++j)for(int i=l;i<=r;++i){int e0=e(x,y,a,b,i,j),e1=e(a,b,c,d,i,j),e2=e(c,d,x,y,i,j);if((e0>=0&&e1>=0&&e2>=0)||(e0<=0&&e1<=0&&e2<=0))fbRect(i,j,1,1,colorValue);}}svg<<"<path d='M"<<x<<","<<y<<" L"<<a<<","<<b<<" L"<<c<<","<<d<<" Z' fill='"<<color(colorValue)<<"'/>";}
  uint16_t alphaBlend(uint8_t alpha,uint16_t a,uint16_t b){unsigned r=(((a>>11)&31)*alpha+((b>>11)&31)*(255-alpha))/255;unsigned g=(((a>>5)&63)*alpha+((b>>5)&63)*(255-alpha))/255;unsigned blue=((a&31)*alpha+(b&31)*(255-alpha))/255;return uint16_t((r<<11)|(g<<5)|blue);}
  int fontHeight(unsigned f)const{return f==1?8:16;}
  // Real TFT_eSPI metrics: font 1 is 6 px per character; font 2 sums the widtbl_f16 table.
  int textWidth(const String& s,unsigned f=2)const{if(f==1)return int(s.length())*6;int n=0;for(size_t i=0;i<s.length();++i){unsigned char c=(unsigned char)s[i];if(c>=32&&c<127)n+=widtbl_f16[c-32];}return n;}
  void setTextColor(uint16_t colorValue,uint16_t background){fg=colorValue;bg=background;}
  void setTextDatum(unsigned d){datum=d;}
  void setTextFont(unsigned f){font=f;}
  void setTextWrap(bool){}
  void noteLabel(const char* text){labels.push_back(text);inks.push_back({text,9999,9999,-1,-1,false});inkAt=int(inks.size())-1;}
  void startWrite(){++streamStarts;}
  void endWrite(){++streamEnds;inkAt=-1;}
  void setCursor(int x,int y){cursorX=x;cursorY=y;}
  void setViewport(int x,int y,int width,int height,bool vpDatum=true){
    resetViewport();ox=vpDatum?x:0;oy=vpDatum?y:0;viewW=width;viewH=height;viewportDatum=vpDatum;
    cx0=std::max(0,x);cy0=std::max(0,y);cx1=std::min(w,x+width);cy1=std::min(h,y+height);
    ++clip;svg<<"<defs><clipPath id='clip"<<clip<<"'><rect width='"<<width<<"' height='"<<height<<"'/></clipPath></defs><g transform='translate("<<ox<<","<<oy<<")' clip-path='url(#clip"<<clip<<")'>";viewport=true;
  }
  void resetViewport(){ox=oy=0;cx0=cy0=0;cx1=w;cy1=h;viewportDatum=false;if(viewport){svg<<"</g>";viewport=false;}}
  void drawString(const String& s,int x,int y,unsigned f=2){int width=textWidth(s,f),height=fontHeight(f);int left=x-(datum%3==1?width/2:datum%3==2?width:0),top=y-(datum/3==1?height/2:datum/3==2?height:0);
    fillRect(left,top,width,height,bg);glyphs(s,left,top,f);labels.push_back(s.c_str());inks.push_back({s.c_str(),left+ox,top+oy,left+ox+width,top+oy+height,true});svg<<"<text x='"<<left<<"' y='"<<top+height-3<<"' font-family='Arial,sans-serif' font-size='"<<(f==1?9:16)<<"' fill='"<<color(fg)<<"'>"<<escape(s.c_str())<<"</text>";
  }
  // Pixel-exact built-in font output, the same table walk as TFT_eSPI::drawChar for fonts 1 and 2.
  void glyphs(const String& s,int x,int y,unsigned f){for(size_t n=0;n<s.length();++n){unsigned char c=(unsigned char)s[n];
    if(f==1){if(c>=127)c='?';for(int i=0;i<5;++i){unsigned char col=::font[c*5+i];for(int j=0;j<7;++j)if(col>>j&1)fbRect(x+i,y+j,1,1,fg);}x+=6;}
    else{if(c<32||c>126)c='?';const unsigned char* g=chrtbl_f16[c-32];const int gw=widtbl_f16[c-32],stride=(gw+6)/8;for(int j=0;j<16;++j)for(int i=0;i<gw;++i)if(g[j*stride+i/8]>>(7-i%8)&1)fbRect(x+i,y+j,1,1,fg);x+=gw;}}}
  // PNG at 'scale' x (nearest-neighbour, exact pixel doubling, no filtering); stored-deflate, no dependencies.
  bool savePng(const char* file,int scale=1)const{const int W=w*scale,H=h*scale;std::vector<unsigned char> raw;raw.reserve(size_t(H)*(W*3+1));
    for(int j=0;j<H;++j){raw.push_back(0);for(int i=0;i<W;++i){uint16_t c=fb[size_t(j/scale)*320+i/scale];raw.push_back(uint8_t(((c>>11)&31)*255/31));raw.push_back(uint8_t(((c>>5)&63)*255/63));raw.push_back(uint8_t((c&31)*255/31));}}
    static uint32_t table[256];if(!table[1])for(uint32_t n=0;n<256;++n){uint32_t c=n;for(int k=0;k<8;++k)c=c&1?0xedb88320u^(c>>1):c>>1;table[n]=c;}
    auto crc=[&](const std::vector<unsigned char>& d){uint32_t c=0xffffffffu;for(unsigned char b:d)c=table[(c^b)&255]^(c>>8);return c^0xffffffffu;};
    auto be=[](std::vector<unsigned char>& v,uint32_t n){for(int k=3;k>=0;--k)v.push_back(uint8_t(n>>(8*k)));};
    std::vector<unsigned char> z{0x78,0x01};uint32_t a=1,b=0;for(unsigned char v:raw){a=(a+v)%65521;b=(b+a)%65521;}
    for(size_t at=0;at<raw.size();at+=65535){size_t n=std::min<size_t>(65535,raw.size()-at);z.push_back(at+n>=raw.size());z.push_back(uint8_t(n));z.push_back(uint8_t(n>>8));z.push_back(uint8_t(~n));z.push_back(uint8_t((~n)>>8));z.insert(z.end(),raw.begin()+at,raw.begin()+at+n);}
    be(z,(b<<16)|a);
    std::ofstream f(file,std::ios::binary);if(!f)return false;const unsigned char sig[8]={137,'P','N','G',13,10,26,10};f.write((const char*)sig,8);
    auto chunk=[&](const char* type,const std::vector<unsigned char>& d){std::vector<unsigned char> o;be(o,uint32_t(d.size()));std::vector<unsigned char> td(type,type+4);td.insert(td.end(),d.begin(),d.end());o.insert(o.end(),td.begin(),td.end());be(o,crc(td));f.write((const char*)o.data(),std::streamsize(o.size()));};
    std::vector<unsigned char> hdr;be(hdr,uint32_t(W));be(hdr,uint32_t(H));hdr.push_back(8);hdr.push_back(2);hdr.push_back(0);hdr.push_back(0);hdr.push_back(0);chunk("IHDR",hdr);chunk("IDAT",z);chunk("IEND",{});return true;}
  uint16_t pixel(int x,int y)const{return fb[size_t(y)*320+x];}
  void classicText(const char* text,int x,int y,int height,uint16_t fg,uint16_t){labels.push_back(text);svg<<"<text x='"<<x<<"' y='"<<y+height/2-3<<"' font-family='Arial,sans-serif' font-size='"<<height<<"' fill='"<<color(fg)<<"'>"<<escape(text)<<"</text>";}
  std::string drawing()const{return svg.str();}
  int colorCount(uint16_t c)const{const std::string content=svg.str(),needle="fill='"+color(c)+"'";int n=0;size_t at=0;while((at=content.find(needle,at))!=std::string::npos){++n;at+=needle.size();}return n;}
  void println(const char* s){drawString(String(s),cursorX,cursorY,font);cursorY+=fontHeight(font);}
};
