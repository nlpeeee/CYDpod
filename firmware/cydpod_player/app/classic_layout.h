#pragma once
#include <stdint.h>
namespace cydclassic { namespace layout {
// All Classic pages: chrome, list rows, text margins and Back hit width.
constexpr int HeaderH=44,FooterH=44,RowH=44,MaxRows=6,BackW=64,TextMargin=10;
constexpr int HeaderTextY=22,HeaderTitleInset=116,HeaderBackX=6,HeaderBackW=54;
constexpr int StatusInset=5,StatusW=36,RouteInset=20,RouteSize=24;
constexpr inline int rows(int h){return (h-HeaderH-FooterH)/RowH<MaxRows?(h-HeaderH-FooterH)/RowH:MaxRows;}
constexpr inline int footerY(int h){return h-FooterH;}
// Browse pages: row width reserves 8 px; the painted strip is only 7 px wide.
constexpr int RowTitleY=14,RowDetailY=33,RowTextInset=34,RowIconInset=12,RowIconSize=12;
constexpr int RowInset=8,StripW=7,ThumbMin=12,ThumbInset=2,ThumbW=3;
template<class R> constexpr inline R row(int w,int i){return {0,HeaderH+i*RowH,w-RowInset,RowH};}
template<class R> constexpr inline R strip(int w,int h){return {w-StripW,HeaderH,StripW,rows(h)*RowH};}
// Browse footer: Home, Up, Down, Playing; Player/Keyboard use 80 px cells.
constexpr int HomeW=64,PageW=48,ActionW=80,PlayingX=160,FooterIconSize=14;
template<class R> constexpr inline R footer(int w,int h,int cell){return {cell==0?0:cell==1?HomeW:cell==2?HomeW+PageW:PlayingX,footerY(h),cell==0?HomeW:cell<3?PageW:w-PlayingX,FooterH};}
template<class R> constexpr inline R action(int w,int h,int cell,bool fixed=false){return {cell*ActionW,footerY(h),cell<2||fixed?ActionW:w-PlayingX,FooterH};}
// Now Playing: three transport cells, progress line and larger seek hit zone.
constexpr int TransportMargin=4,TransportGap=8,TransportBottom=100,ControlH=52;
constexpr int ProgressInset=12,ProgressBottom=136,ProgressH=5,TimeGap=7,TimeH=18,TimeY=16,TimeInset=16;
constexpr int SeekInset=8,SeekBottom=148,SeekH=44;
template<class R> constexpr inline R transport(int w,int h,int cell){return {TransportMargin+cell*(w/3),h-TransportBottom,w/3-TransportGap,ControlH};}
template<class R> constexpr inline R seek(int w,int h){return {SeekInset,h-SeekBottom,w-2*SeekInset,SeekH};}
// Volume: slider and the two step buttons share their paint and hit rectangles.
constexpr int SliderInset=24,SliderY=126,StepInset=8,StepY=190,StepW=96,StepIconX=56,StepIconY=216,StepIconSize=26;
template<class R> constexpr inline R slider(int w){return {SliderInset,SliderY,w-2*SliderInset,ControlH};}
template<class R> constexpr inline R step(int w,bool plus){return {plus?w-StepW-StepInset:StepInset,StepY,StepW,ControlH};}
// Search keyboard: retain the fixed landscape action row, including its gaps.
constexpr int QueryInset=8,QueryY=60,QueryTextInset=54,QueryStatusInset=6,QueryStatusW=42;
constexpr int PortraitMinH=300,QueryH=32,KeyY=76,KeyH=40,KeyGap=2,KeyTextInset=4;
constexpr int LandscapeCols=8,DigitCols=5,LetterCols=6,KeyActionY=156,PortraitActionY=236;
constexpr int KeyActionW=106,KeyDigitsX=212,KeyDigitsW=108;
constexpr inline int columns(int h,bool digits){return h<PortraitMinH?LandscapeCols:digits?DigitCols:LetterCols;}
template<class R> constexpr inline R key(int w,int h,int i,bool digits){return {(i%columns(h,digits))*(w/columns(h,digits)),KeyY+(i/columns(h,digits))*KeyH,w/columns(h,digits),KeyH};}
template<class R> constexpr inline R keyBack(int h){return h<PortraitMinH?R{0,KeyActionY,KeyActionW,KeyH}:R{ActionW,PortraitActionY,ActionW,KeyH};}
template<class R> constexpr inline R keyDigits(int h){return h<PortraitMinH?R{KeyDigitsX,KeyActionY,KeyDigitsW,KeyH}:R{PlayingX,PortraitActionY,ActionW,KeyH};}
template<class R> constexpr inline R keyPage(){return {KeyActionW,KeyActionY,KeyActionW,KeyH};}
} } // namespace cydclassic::layout
