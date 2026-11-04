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
// Proposal A: Player-only chrome. Browse geometry remains unchanged.
constexpr int PlayerHeaderH=36,PlayerHeaderY=18,PlayerHeaderHitH=40;
constexpr int PlayerBackW=100,PlayerCountX=150,PlayerPortraitCountX=125,PlayerCountW=96,PlayerPortraitCountW=48,PlayerCountH=11,PlayerPortraitCountH=8,PlayerBackX=16,PlayerParentX=28;
constexpr int PlayerFavouriteInset=90,PlayerOptionsInset=48,PlayerHeaderIcon=16;
constexpr int PlayerTitleY=54,PlayerArtistY=78,PlayerAlbumY=96,PlayerTitleH=22,PlayerArtistH=14,PlayerAlbumH=12;
constexpr int PlayerTextH=66,PlayerTextTop=40,PlayerTextInset=14;
constexpr int PlayerTimeH=11,PlayerTimeBottom=102,PlayerTimeClearBottom=110,PlayerTimeClearH=16;
constexpr int ProgressInset=14,ProgressBottom=122,ProgressH=8,KnobR=8,KnobBorder=2;
constexpr int SeekInset=6,SeekBottom=140,SeekH=40,TransportBottom=92,ControlH=52;
constexpr int PlayerBottomH=40,PlayerChipW=76,PlayerChipInset=6,PlayerChipH=30,PlayerChipRadius=6;
constexpr int PlayerOutputHitW=88,PlayerVolumeX=44,PlayerVolumeReserve=170,PlayerVolumeBottom=24;
constexpr int PlayerSpeakerX=20,PlayerSpeakerSize=16,PlayerNumberGap=12,PlayerNumberW=28;
constexpr int PlayerDotsGap=6,PlayerDotR=2,PlayerChipTextInset=30,PlayerChipIconInset=16,PlayerChipTextH=10;
constexpr int FontSourceH=16,FontFirst=32,FontLast=126,FontByteBits=8;
constexpr inline int playerTextOffset(int h){return (h-240)/2;}
template<class R> constexpr inline R playerBack(){return {0,0,PlayerBackW,PlayerHeaderHitH};}
template<class R> constexpr inline R favourite(int w){return {w-PlayerFavouriteInset,0,40,PlayerHeaderHitH};}
template<class R> constexpr inline R options(int w){return {w-PlayerOptionsInset,0,PlayerOptionsInset,PlayerHeaderHitH};}
template<class R> constexpr inline R transport(int w,int h,int cell){
  return cell==0?R{w>=300?56:40,h-TransportBottom,w>=300?70:36,48}:
    cell==1?R{w/2-(w>=300?30:40),h-TransportBottom-2,80,ControlH}:
    R{w>=300?214:164,h-TransportBottom,w>=300?70:36,48};
}
template<class R> constexpr inline R playerToggle(int w,int h,bool repeat){return {repeat?w-36:0,h-TransportBottom,w>=300?(repeat?36:52):36,48};}
template<class R> constexpr inline R seek(int w,int h){return {SeekInset,h-SeekBottom,w-2*SeekInset,SeekH};}
template<class R> constexpr inline R progress(int w,int h){return {ProgressInset,h-ProgressBottom,w-2*ProgressInset,ProgressH};}
template<class R> constexpr inline R playerVolume(int w,int h){return {0,h-PlayerBottomH,w-PlayerOutputHitW,PlayerBottomH};}
template<class R> constexpr inline R playerVolumeTrack(int w,int h){return {PlayerVolumeX,h-PlayerVolumeBottom,w-PlayerVolumeReserve,ProgressH};}
template<class R> constexpr inline R output(int w,int h){return {w-PlayerOutputHitW,h-PlayerBottomH,PlayerOutputHitW,PlayerBottomH};}
template<class R> constexpr inline R outputFace(int w,int h){return {w-PlayerChipInset-PlayerChipW,h-PlayerBottomH+(PlayerBottomH-PlayerChipH)/2,PlayerChipW,PlayerChipH};}
// Shared endpoint math: paint and input use the same inclusive endpoints.
constexpr inline int sliderValue(int x,int left,int width,int maximum){return x<=left?0:x>=left+width?maximum:int(int64_t(x-left)*maximum/width);}
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
template<class R> constexpr inline R key(int w,int h,int i,bool digits){const int cols=columns(h,digits),cell=w/cols;return {(i%cols)*cell,KeyY+(i/cols)*KeyH,cell,KeyH};}
template<class R> constexpr inline R keyBack(int h){return h<PortraitMinH?R{0,KeyActionY,KeyActionW,KeyH}:R{ActionW,PortraitActionY,ActionW,KeyH};}
template<class R> constexpr inline R keyDigits(int h){return h<PortraitMinH?R{KeyDigitsX,KeyActionY,KeyDigitsW,KeyH}:R{PlayingX,PortraitActionY,ActionW,KeyH};}
template<class R> constexpr inline R keyPage(){return {KeyActionW,KeyActionY,KeyActionW,KeyH};}
} } // namespace cydclassic::layout
