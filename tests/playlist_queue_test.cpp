#include "playlist.h"
#include "audio.h"
#include "storage.h"
#include "settings.h"
#include <fstream>
#include <iostream>
#include <malloc.h>
Settings g_settings;
static int total=0, playingTrack=0, scans=0;
static bool brokenScan=false, brokenView=false;
static AudioQueueSource live;
static int length=0;
static size_t allocationCalls=0, allocationMax=0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n){++allocationCalls;allocationMax=std::max(allocationMax,n);return __real_malloc(n);}
void* operator new(size_t n){if(void* p=malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,size_t) noexcept {free(p);}
bool settings_save(){return true;}
bool audio_ready(){return true;}
int audio_currentTrack(){return playingTrack;}
void audio_playTrack(int i){playingTrack=i;}
void audio_setQueueSource(AudioQueueSource q,int n,int,bool){live=q;length=n;}
int at(int i){return live.at?live.at(live.context,i):live.tracks?live.tracks[i]:i;}
int storage_trackCount(){return total;}
bool storage_readOnly(){return false;}
uint32_t storage_libraryCrc(){return 1;}
String storage_trackPath(int i){return String("/music/A/")+String(i)+".m4a";}
void storage_forEach(StorageEachFn fn){++scans;for(int i=0;i<total-(brokenScan?1:0);++i)fn(i,storage_trackPath(i),String(i));}
int storage_browseCount(CatalogView){return total;}
bool storage_browseEntry(CatalogView,int i,CatalogEntry& e){e.track=total-1-i;return !brokenView || i<total-2;}
int storage_browseTracks(CatalogView,uint16_t* out,int cap){if(cap<total)return 0;for(int i=0;i<total;++i)out[i]=total-1-i;return total;}
int storage_browseTrackIds(CatalogView,int first,uint16_t* out,int cap){
  if(first<0||first>=total||(brokenView&&first+cap>total-2))return 0;
  const int n=std::min(cap,total-first);for(int i=0;i<n;++i)out[i]=total-1-first-i;return n;
}
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  SD.root=std::filesystem::current_path()/"queue-card";std::filesystem::remove_all(SD.root);
  std::filesystem::create_directories(SD.root/".sys");std::filesystem::create_directories(SD.root/"playlists");
  for(int n:{1000,2000,10000}){
    total=n;{std::ofstream f(SD.root/".sys/fav.txt");f<<storage_trackPath(7).c_str()<<'\n'<<storage_trackPath(2).c_str()<<'\n';}
    {std::ofstream f(SD.root/"playlists/list.m3u");f<<"#EXTM3U\nA/7.m4a\nA/missing.m4a\nA/2.m4a\nA/7.m4a\n";}
    playlist_begin();check(playlist_select(2)&&length==n,"folder queue build");
    auto* q=static_cast<PlaylistQueue*>(live.context);
    for(int i:{0,255,256,511,n-1,729,1})check(q->queueAt(i)==i,"first/last/crossing/arbitrary");
    check(q->queueAt(-1)==-1&&q->queueAt(n)==-1&&q->queueFind(n-1)==n-1&&q->queueFind(n)==-1&&q->queueCount()==n,"find/count/range");
    q->first=-1;q->cached=0;hostReadBudget=0;check(q->queueAt(0)==-1,"SD short read unavailable");hostReadBudget=-1;check(q->queueAt(0)==0,"retry after SD failure");
    const char* oldPath=q->path;hostWriteBudget=0;check(!playlist_select(2)&&q->path==oldPath&&q->queueAt(0)==0,"failed stage preserves live source");hostWriteBudget=-1;
    brokenScan=true;check(!playlist_select(2),"partial catalogue scan refused");brokenScan=false;
    check(playlist_select(1)&&length==2&&at(0)==2&&at(1)==7,"favourites library order");
    check(playlist_select(3)&&length==3&&at(0)==7&&at(1)==2&&at(2)==7,"M3U duplicates/order/missing");
    check(playlist_select(2),"restore folder");q=static_cast<PlaylistQueue*>(live.context);
    q->queueAt(0);size_t calls=allocationCalls;q->queueAt(1);check(allocationCalls==calls,"cache hit allocates nothing");
    calls=allocationCalls;q->queueAt(256);size_t fillCalls=allocationCalls-calls;
    check(sizeof(PlaylistQueue)<1024&&fillCalls<16,"bounded cursor and fill allocations");
    PlaylistQueue preview;
    calls=allocationCalls;allocationMax=0;
    check(playlist_preview(2,preview)&&preview.queueCount()==n,"bounded folder preview");
    const size_t previewCalls=allocationCalls-calls, previewMax=allocationMax;
    for(int row:{255,256,511,n-1,729,1})check(preview.queueAt(row)==row,"preview crossing/jump/selection");
    preview.queueAt(256);calls=allocationCalls;const auto reads=hostReadCalls;
    for(int row=256;row<270;++row)check(preview.queueAt(row)==row,"viewport cached rows");
    check(allocationCalls==calls&&hostReadCalls==reads,"viewport cache no allocation or SD reads");
    check(playlist_playPreview(preview,n-1,2)&&playingTrack==n-1&&at(n-1)==n-1,"play selected far row");
    const char* playingPath=q->path;
    check(playlist_preview(1,preview)&&preview.queueAt(0)==2&&q->path==playingPath&&at(n-1)==n-1,"preview never overwrites live files");
    check(playlist_preview(3,preview)&&preview.queueCount()==3&&preview.queueAt(0)==7&&preview.queueAt(1)==2&&preview.queueAt(2)==7,"preview M3U parity");
    const char* previewPath=preview.path;
    hostWriteBudget=0;check(!playlist_preview(2,preview)&&preview.path==previewPath,"failed preview retains descriptor");hostWriteBudget=-1;
    check(playlist_preview(2,preview),"reload preview");
    hostWriteBudget=6;check(!playlist_playPreview(preview,n-1,2)&&q->path==playingPath&&playingTrack==n-1&&at(n-1)==n-1,"short preview stage preserves live playback");hostWriteBudget=-1;
    preview.first=-1;preview.cached=0;hostReadBudget=0;
    check(!playlist_playPreview(preview,n-1,2)&&q->path==playingPath,"preview read error terminates stage");hostReadBudget=-1;
    calls=allocationCalls;allocationMax=0;
    check(playlist_playView({CatalogKind::Songs,0},n-1)&&playingTrack==0&&at(0)==n-1&&at(n-1)==0,"playView sorted order/far selection");
    const size_t viewCalls=allocationCalls-calls, viewMax=allocationMax;
    playingPath=q->path;const int sourceBefore=playlist_current();
    brokenView=true;check(!playlist_playView({CatalogKind::Songs,0},0)&&q->path==playingPath&&playingTrack==0&&playlist_current()==sourceBefore&&at(n-1)==0,"catalogue read stage preserves live playback");brokenView=false;
    hostWriteBudget=6;check(!playlist_playView({CatalogKind::Songs,0},0)&&q->path==playingPath&&playingTrack==0,"catalogue short write preserves live playback");hostWriteBudget=-1;
    check(previewMax<1024&&viewMax<1024,"no library-sized allocation");
    std::cout<<"PASS preview N="<<n<<" resident="<<sizeof(PlaylistQueue)<<" bytes; boundary/jump/selection/far-play/parity/SD failure; cache allocations=0, preview allocations="<<previewCalls<<" max="<<previewMax<<", playView allocations="<<viewCalls<<" max="<<viewMax<<"\n";
    std::cout<<"PASS queue N="<<n<<" cursor="<<sizeof(PlaylistQueue)<<" bytes, cache-hit allocations=0, fill allocations="<<fillCalls<<"; boundaries/find/SD failure/source parity\n";
  }
  PlaylistQueue empty;check(empty.queueCount()==0&&empty.queueAt(0)==-1&&empty.queueFind(0)==-1,"empty");
  PlaylistQueue one;one.count=1;check(one.queueAt(0)==0&&one.queueAt(1)==-1&&one.queueFind(0)==0,"one identity");
  total=1;playlist_begin();check(playlist_select(2)&&length==1&&at(0)==0,"one SD source");
  total=0;playlist_begin();check(!playlist_select(2),"empty source refused");
  std::cout<<"PASS queue empty/one-track sources\n";
}catch(const std::exception& e){std::cerr<<"FAIL queue: "<<e.what()<<'\n';return 1;}}

