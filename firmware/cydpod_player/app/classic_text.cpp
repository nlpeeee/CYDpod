#ifndef CYD_UPLOADER
#include "classic_text.h"
#include "display.h"
#include <string.h>

namespace cydtext {
namespace {
const GFXfont& fontOf(Face face){return face==Face::Title?FreeSansBold12pt7b:FreeSans9pt7b;}
const GFXglyph& glyphOf(const GFXfont& font,unsigned char c){
  if(c<font.first||c>font.last)c=' ';
  return font.glyph[c-font.first];
}
}
bool ascii(const char* s){for(;*s;++s)if((unsigned char)*s>=0x80)return false;return true;}
int capHeight(Face face){const GFXfont& font=fontOf(face);return glyphOf(font,'H').height;}
int width(Face face,const char* s){
  const GFXfont& font=fontOf(face);int w=0;
  for(;*s;++s)w+=glyphOf(font,(unsigned char)*s).xAdvance;
  return w;
}
int fit(Face face,const char* s,char* out,size_t capacity,int maxW){
  if(!capacity)return 0;
  const GFXfont& font=fontOf(face);
  size_t n=0;int used=0;
  for(;s[n]&&n+4<capacity;++n){out[n]=s[n];used+=glyphOf(font,(unsigned char)s[n]).xAdvance;}
  out[n]=0;
  if(!s[n]&&used<=maxW)return used;
  // Over budget (or the source did not fit the buffer): drop whole glyphs, never a partial one.
  const int dots=3*glyphOf(font,'.').xAdvance;
  while(n&&used+dots>maxW){used-=glyphOf(font,(unsigned char)out[--n]).xAdvance;}
  while(n&&out[n-1]==' '){used-=glyphOf(font,' ').xAdvance;--n;} // "Time ..." -> "Time..."
  if(used+dots>maxW){out[0]=0;return 0;}
  memcpy(out+n,"...",4);
  return used+dots;
}
int draw(Face face,const char* s,int x,int cy,int hAlign,uint16_t fg){
  const GFXfont& font=fontOf(face);
  const int total=width(face,s);
  int left=hAlign==HAlignCentre?x-total/2:hAlign==HAlignRight?x-total:x;
  const int base=cy-capHeight(face)/2+capHeight(face);
#ifdef CYDPOD_CATALOG_HOST
  tft.noteLabel(s); // host recorder: the text drawn here, for label assertions
#endif
  tft.startWrite();
  for(;*s;++s){
    const GFXglyph& g=glyphOf(font,(unsigned char)*s);
    const uint8_t* bits=font.bitmap+g.bitmapOffset;uint8_t byte=0;unsigned count=0;
    for(int row=0;row<g.height;++row){
      int run=-1;
      for(int col=0;col<=g.width;++col){
        bool on=false;
        if(col<g.width){if(!(count++&7))byte=*bits++;on=byte&0x80;byte<<=1;}
        if(on&&run<0)run=col;
        else if(!on&&run>=0){tft.drawFastHLine(left+g.xOffset+run,base+g.yOffset+row,col-run,fg);run=-1;}
      }
    }
    left+=g.xAdvance;
  }
  tft.endWrite();
  return total;
}
}
#endif
