#pragma once
#include <stdint.h>
namespace cydclassic { namespace layout {
// All Classic pages: chrome, list rows, text margins and Back hit width.
constexpr int HeaderH=36,FooterH=44,RowH=40,MaxRows=6,BackW=100,TextMargin=10;
constexpr int HeaderTextY=18,HeaderTitleRight=36,HeaderTitleGap=8,HeaderBackX=16,HeaderParentX=28,HeaderIconSize=16;
constexpr int RouteInset=20,RouteSize=24;
constexpr inline int rows(int h){return (h-HeaderH-FooterH)/RowH<MaxRows?(h-HeaderH-FooterH)/RowH:MaxRows;}
constexpr inline int footerY(int h){return h-FooterH;}
// Browse pages reserve a 30 px scrollbar touch rail; no paging controls.
constexpr int RowTitleY=12,RowDetailY=30,RowTextInset=34,RowIconInset=12,RowIconSize=12;
constexpr int RowInset=30,StripW=30,ThumbMin=12,ThumbInset=25,ThumbW=3;
template<class R> constexpr inline R row(int w,int i){return {0,HeaderH+i*RowH,w-RowInset,RowH};}
template<class R> constexpr inline R strip(int w,int h){return {w-StripW,HeaderH,StripW,rows(h)*RowH};}
constexpr int FooterIconSize=14;
// Browse mini-player; the Player bottom row remains independent.
constexpr int MiniButtonW=52,MiniProgressH=2,MiniTitleY=12,MiniArtistY=31;
constexpr int MiniTextInset=8,MiniIconSize=18,MiniPressMs=80,DeepHomeW=40,HomeGlyphHalf=7,HomeRoofH=6,HomeWallH=7;
constexpr inline int miniY(int h){return footerY(h);}
template<class R> constexpr inline R mini(int w,int h,int cell){return {cell==0?0:cell==1?MiniButtonW:w-MiniButtonW,miniY(h),cell==1?w-2*MiniButtonW:MiniButtonW,FooterH};}
template<class R> constexpr inline R miniProgress(int w,int h){return {0,miniY(h),w,MiniProgressH};}
template<class R> constexpr inline R deepHome(){return {BackW,0,DeepHomeW,HeaderH};}
constexpr int ThumbHoldMs=550;
template<class R> inline R thumb(int w,int h,int count,int32_t offset){
  const int height=rows(h)*RowH,size=count>rows(h)?(height*rows(h)/count>ThumbMin?height*rows(h)/count:ThumbMin):height;
  const int32_t end=int32_t(count>rows(h)?count-rows(h):0)*RowH;
  const int32_t clamped=offset<0?0:offset>end?end:offset;
  return {w-StripW+ThumbInset,HeaderH+(end?int(int64_t(height-size)*clamped/end):0),ThumbW,size};
}
inline int32_t thumbOffset(int y,int h,int count,int grab){
  struct Box{int x,y,w,h;};const Box r=thumb<Box>(0,h,count,0);
  const int travel=rows(h)*RowH-r.h,position=y-HeaderH-grab;
  const int32_t end=int32_t(count>rows(h)?count-rows(h):0)*RowH;
  return travel<=0||position<=0?0:position>=travel?end:int32_t(int64_t(position)*end/travel);
}
// Proposal A: Player-only chrome and controls remain independent of browse rows.
constexpr int PlayerHeaderH=36,PlayerHeaderY=18,PlayerHeaderHitH=40;
constexpr int PlayerBackW=100,PlayerCountX=150,PlayerPortraitCountX=125,PlayerCountW=96,PlayerPortraitCountW=48,PlayerBackX=16,PlayerParentX=28;
constexpr int PlayerFavouriteInset=90,PlayerOptionsInset=48,PlayerHeaderIcon=16;
// Text rows are the vertical centre of capitals (classic_text.h); 12 pt bold title, 9 pt artist and album.
constexpr int PlayerTitleY=52,PlayerArtistY=76,PlayerAlbumY=97;
constexpr int PlayerTextH=70,PlayerTextTop=40,PlayerTextInset=14;
// Now Playing marquee: an overflowing label holds, scrolls ~33 px/s through one full lap, then rests.
constexpr int MarqueeHoldMs=1500,MarqueeStepMs=60,MarqueeStepPx=2,MarqueeGap=32;
constexpr int PlayerTimeBottom=105,PlayerTimeClearBottom=112,PlayerTimeClearH=18;
constexpr int ProgressInset=14,ProgressBottom=124,ProgressH=8,KnobR=8,KnobBorder=2;
constexpr int SeekInset=6,SeekBottom=140,SeekH=40,TransportBottom=92,ControlH=52;
// Output chip (landscape only): no icon, so the 9 pt text gets the width: "Headphones" is 102 px and
// "AirPods Pro 2" 111 px. Portrait has no chip, so the volume track runs the full width; Options and
// Settings still open Audio Output. The volume track takes the rest.
constexpr int PlayerBottomH=40,PlayerChipWideW=124,PlayerChipNarrowW=114,PlayerChipInset=6,PlayerChipGap=4,PlayerChipH=30,PlayerChipRadius=6;
constexpr int PlayerVolumeWideX=44,PlayerVolumeNarrowX=28,PlayerVolumeBottom=24,PlayerSpeakerWideX=20,PlayerSpeakerNarrowX=14;
constexpr int WideMinW=300; // the 320 px landscapes
constexpr inline int chipW(int w){return w>=WideMinW?PlayerChipWideW:PlayerChipNarrowW;}
constexpr inline bool playerHasOutput(int w){return w>=WideMinW;}
constexpr inline int outputHitW(int w){return playerHasOutput(w)?chipW(w)+2*PlayerChipInset:0;}
// Right edge of the volume number: the chip's left edge, or the screen inset without one.
constexpr inline int volumeRight(int w){return playerHasOutput(w)?w-PlayerChipInset-chipW(w)-PlayerChipGap:w-PlayerChipInset;}
constexpr inline int playerVolumeX(int w){return w>=WideMinW?PlayerVolumeWideX:PlayerVolumeNarrowX;}
constexpr inline int playerSpeakerX(int w){return w>=WideMinW?PlayerSpeakerWideX:PlayerSpeakerNarrowX;}
constexpr int PlayerSpeakerSize=16,PlayerNumberGap=10,PlayerNumberW=30,PlayerNumberClearH=20;
constexpr int PlayerDotsGap=6,PlayerDotR=2,PlayerChipTextInset=6,PlayerChipTextPad=3;
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
template<class R> constexpr inline R playerVolume(int w,int h){return {0,h-PlayerBottomH,w-outputHitW(w),PlayerBottomH};}
template<class R> constexpr inline R playerVolumeTrack(int w,int h){return {playerVolumeX(w),h-PlayerVolumeBottom,volumeRight(w)-PlayerNumberW-PlayerNumberGap-playerVolumeX(w),ProgressH};}
template<class R> constexpr inline R output(int w,int h){return {w-outputHitW(w),h-PlayerBottomH,outputHitW(w),PlayerBottomH};}
template<class R> constexpr inline R outputFace(int w,int h){return playerHasOutput(w)?R{w-PlayerChipInset-chipW(w),h-PlayerBottomH+(PlayerBottomH-PlayerChipH)/2,chipW(w),PlayerChipH}:R{w,h-PlayerBottomH,0,0};}
// Shared endpoint math: paint and input use the same inclusive endpoints.
constexpr inline int sliderValue(int x,int left,int width,int maximum){return x<=left?0:x>=left+width?maximum:int(int64_t(x-left)*maximum/width);}
// Search: full-width ten-key rows in every orientation. Hit cells are half-open,
// contiguous and own their inset face gaps; drawing and input share these rectangles.
constexpr int QueryInset=8,QueryY=60,QueryTextInset=54,QueryStatusInset=6,QueryStatusW=42;
constexpr int PortraitMinH=300,QueryH=32,KeyY=HeaderH+QueryH,KeyGap=2,KeyTextInset=4;
constexpr int KeyRows=3,KeyCols=10,KeyRowTwo=10,KeyRowThree=19,DigitRowThree=20;
constexpr inline int keyRow(int i,bool digits){return i<(digits?KeyCols:KeyRowTwo)?0:i<(digits?DigitRowThree:KeyRowThree)?1:2;}
template<class R> constexpr inline R key(int w,int h,int i,bool digits){
  const int row=keyRow(i,digits),start=row==0?0:row==1?KeyCols:digits?DigitRowThree:KeyRowThree;
  const int col=i-start,cols=!digits&&row==1?KeyCols-1:KeyCols;
  const int top=KeyY+(footerY(h)-KeyY)*row/KeyRows,bottom=KeyY+(footerY(h)-KeyY)*(row+1)/KeyRows;
  return {col*w/cols,top,(col+1)*w/cols-col*w/cols,bottom-top};
}
template<class R> constexpr inline R keyBack(int w,int h){const R r=key<R>(w,h,DigitRowThree+7,true);return {r.x,r.y,w-r.x,r.h};}
// Bottom row: 123/ABC, Space (double width), Clear, Done.
template<class R> constexpr inline R keyAction(int w,int h,int cell){
  const int start=cell==0?0:cell==1?1:cell+1,end=cell==0?1:cell==1?3:cell+2;
  return {start*w/5,footerY(h),end*w/5-start*w/5,FooterH};
}
template<class R> constexpr inline R keyDigits(int w,int h){return keyAction<R>(w,h,0);}
// CY-80 Home: six tiles (3 x 2 landscape, 2 x 3 portrait) between header and mini-player.
// The left/top margins match proposal C; the right margin is one gap. Swipe-back needs this much rightward travel.
constexpr int TileCount=6,TileMargin=8,TileGap=4,TileTop=8,TileBottom=4,TileRadius=8,SwipeBackPx=40;
constexpr int TileIconY=26,TileIconSize=28,TileLabelBottom=15,TileTextInset=6,TileStroke=2;
constexpr inline int tileCols(int h){return h<PortraitMinH?3:2;}
constexpr inline int tileW(int w,int h){return (w-TileMargin-TileGap*tileCols(h))/tileCols(h);}
constexpr inline int tileH(int h){return (h-HeaderH-FooterH-TileTop-TileBottom-(TileCount/tileCols(h)-1)*TileGap)/(TileCount/tileCols(h));}
template<class R> constexpr inline R tile(int w,int h,int i){return {TileMargin+(i%tileCols(h))*(tileW(w,h)+TileGap),HeaderH+TileTop+(i/tileCols(h))*(tileH(h)+TileGap),tileW(w,h),tileH(h)};}
// EQ-5BAND Equalizer page: a strip of three buttons (On/Off, Preset, Reset) under the header,
// then one full-height column per band down to the mini-player. The whole column is the
// band's hit area; inside it, the gain label, a vertical track and the band label.
constexpr int EqMargin=8,EqControlTop=4,EqControlH=28,EqControlGap=6,EqControlRadius=6,EqControls=3;
constexpr int EqColumnsGap=4,EqLabelH=14,EqTrackW=8,EqZeroHalf=10;
constexpr inline int eqTop(){return HeaderH+EqControlTop+EqControlH+EqColumnsGap;}
template<class R> constexpr inline R eqControl(int w,int i){
  return {EqMargin+i*((w-2*EqMargin-(EqControls-1)*EqControlGap)/EqControls+EqControlGap),HeaderH+EqControlTop,
          (w-2*EqMargin-(EqControls-1)*EqControlGap)/EqControls,EqControlH};
}
template<class R> constexpr inline R eqColumn(int w,int h,int band,int bands){return {band*w/bands,eqTop(),(band+1)*w/bands-band*w/bands,miniY(h)-eqTop()};}
template<class R> constexpr inline R eqTrack(int w,int h,int band,int bands){
  return {band*w/bands+((band+1)*w/bands-band*w/bands-EqTrackW)/2,eqTop()+EqLabelH+KnobR,EqTrackW,miniY(h)-eqTop()-2*(EqLabelH+KnobR)};
}
// Step index 0..steps from a y inside the track (top = steps), rounded to the nearest step;
// eqStepY is its inverse, so paint and input share the same positions.
constexpr inline int eqStepAt(int y,int top,int height,int steps){
  return y<=top?steps:y>=top+height?0:((top+height-y)*steps+height/2)/height;
}
constexpr inline int eqStepY(int step,int top,int height,int steps){return top+height-step*height/steps;}
} } // namespace cydclassic::layout
