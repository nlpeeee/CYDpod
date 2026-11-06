#include "playlist.h"
#include "audio.h"
#include "storage.h"
#include "settings.h"
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <regex>
#include <unistd.h>
Settings g_settings;
static std::vector<String> library;
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
static bool activePlayback = true;
bool audio_isPlaying(){return activePlayback;}
bool audio_ready(){return true;}
int audio_currentTrack(){return playingTrack;}
void audio_playTrack(int i){playingTrack=i;}
void audio_setQueueSource(AudioQueueSource q,int n,int,bool){live=q;length=n;}
int at(int i){return live.at?live.at(live.context,i):live.tracks?live.tracks[i]:i;}
int storage_trackCount(){return total;}
bool storage_readOnly(){return false;}
uint32_t storage_libraryCrc(){return 1;}
String storage_trackPath(int i){return library.empty()?String("/music/A/")+String(i)+".m4a":library.at(i);}
void storage_forEach(StorageEachFn fn){++scans;for(int i=0;i<total-(brokenScan?1:0);++i)fn(i,storage_trackPath(i),String(i));}
int storage_browseCount(CatalogView){return total;}
bool storage_browseEntry(CatalogView,int i,CatalogEntry& e){e.track=total-1-i;return !brokenView || i<total-2;}
int storage_browseTracks(CatalogView,uint16_t* out,int cap){if(cap<total)return 0;for(int i=0;i<total;++i)out[i]=total-1-i;return total;}
int storage_browseTrackIds(CatalogView,int first,uint16_t* out,int cap){
  if(first<0||first>=total||(brokenView&&first+cap>total-2))return 0;
  const int n=std::min(cap,total-first);for(int i=0;i<n;++i)out[i]=total-1-first-i;return n;
}
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
// Capture the existing serial shim without changing production logging or the shim.
struct SerialCapture {
  FILE* file = std::tmpfile();
  int saved = -1;
  SerialCapture() {
    check(file != nullptr, "serial capture file");
    std::fflush(stdout); saved = dup(fileno(stdout));
    check(saved >= 0 && dup2(fileno(file), fileno(stdout)) >= 0, "serial capture redirect");
  }
  std::string finish() {
    std::fflush(stdout); dup2(saved, fileno(stdout)); close(saved); saved = -1;
    std::rewind(file); char bytes[1024]; std::string text;
    while (const size_t n = std::fread(bytes, 1, sizeof(bytes), file)) text.append(bytes, n);
    return text;
  }
  ~SerialCapture() { if (saved >= 0) { dup2(saved, fileno(stdout)); close(saved); } std::fclose(file); }
};
static void telemetryCheck(const std::string& text, const char* prefix, const char* role, int records, const char* status, bool playing) {
  const std::string pattern = std::string(prefix) + " role=" + role +
    " scan=[0-9]+ records=" + std::to_string(records) + " total=[0-9]+ status=" + status +
    " playing=" + (playing ? "1" : "0") + "\n";
  check(std::regex_match(text, std::regex(pattern)), "exact telemetry fields, visited records, role/status/playing and one line");
}
static const char* crate[] = {
  "Tracks/01 - Britpop and 90s Indie/01-01 - Pulp - Disco 2000.m4a","Tracks/01 - Britpop and 90s Indie/01-02 - Blur - Girls & Boys.m4a","Tracks/01 - Britpop and 90s Indie/01-03 - Blur - Parklife.m4a","Tracks/01 - Britpop and 90s Indie/01-04 - Oasis - Some Might Say.m4a",
  "Tracks/01 - Britpop and 90s Indie/01-05 - Oasis - She's Electric.m4a","Tracks/01 - Britpop and 90s Indie/01-06 - Suede - Beautiful Ones.m4a","Tracks/01 - Britpop and 90s Indie/01-07 - Suede - Trash.m4a","Tracks/01 - Britpop and 90s Indie/01-08 - Supergrass - Alright.m4a",
  "Tracks/01 - Britpop and 90s Indie/01-09 - Supergrass - Pumping on Your Stereo.m4a","Tracks/01 - Britpop and 90s Indie/01-10 - The Stone Roses - She Bangs the Drums.m4a","Tracks/01 - Britpop and 90s Indie/01-11 - The Stone Roses - Fools Gold.m4a","Tracks/01 - Britpop and 90s Indie/01-12 - Happy Mondays - Step On.m4a",
  "Tracks/01 - Britpop and 90s Indie/01-13 - The Charlatans - The Only One I Know.m4a","Tracks/01 - Britpop and 90s Indie/01-14 - James - Sit Down.m4a","Tracks/01 - Britpop and 90s Indie/01-15 - James - Laid.m4a","Tracks/01 - Britpop and 90s Indie/01-16 - The La's - There She Goes.m4a",
  "Tracks/01 - Britpop and 90s Indie/01-17 - The Boo Radleys - Wake Up Boo!.m4a","Tracks/01 - Britpop and 90s Indie/01-18 - Dodgy - Good Enough.m4a","Tracks/01 - Britpop and 90s Indie/01-19 - Ocean Colour Scene - The Riverboat Song.m4a","Tracks/01 - Britpop and 90s Indie/01-20 - The Bluetones - Slight Return.m4a",
  "Tracks/01 - Britpop and 90s Indie/01-21 - The Lightning Seeds - The Life of Riley.m4a","Tracks/01 - Britpop and 90s Indie/01-22 - Ash - Girl from Mars.m4a","Tracks/01 - Britpop and 90s Indie/01-23 - Republica - Ready to Go.m4a","Tracks/01 - Britpop and 90s Indie/01-24 - The Dandy Warhols - Bohemian Like You.m4a",
  "Tracks/01 - Britpop and 90s Indie/01-25 - The Cardigans - My Favourite Game.m4a","Tracks/02 - 2000s Indie Disco/02-01 - Franz Ferdinand - Take Me Out.m4a","Tracks/02 - 2000s Indie Disco/02-02 - The Strokes - Last Nite.m4a","Tracks/02 - 2000s Indie Disco/02-03 - The Killers - Somebody Told Me.m4a",
  "Tracks/02 - 2000s Indie Disco/02-04 - The Killers - When You Were Young.m4a","Tracks/02 - 2000s Indie Disco/02-05 - Kaiser Chiefs - I Predict a Riot.m4a","Tracks/02 - 2000s Indie Disco/02-06 - Kaiser Chiefs - Ruby.m4a","Tracks/02 - 2000s Indie Disco/02-07 - Bloc Party - Banquet.m4a",
  "Tracks/02 - 2000s Indie Disco/02-08 - The Wombats - Let's Dance to Joy Division.m4a","Tracks/02 - 2000s Indie Disco/02-09 - The Wombats - Moving to New York.m4a","Tracks/02 - 2000s Indie Disco/02-10 - Arctic Monkeys - Fluorescent Adolescent.m4a","Tracks/02 - 2000s Indie Disco/02-11 - The Kooks - Naive.m4a",
  "Tracks/02 - 2000s Indie Disco/02-12 - The Kooks - She Moves in Her Own Way.m4a","Tracks/02 - 2000s Indie Disco/02-13 - The Libertines - Don't Look Back into the Sun.m4a","Tracks/02 - 2000s Indie Disco/02-14 - The Futureheads - Hounds of Love.m4a","Tracks/02 - 2000s Indie Disco/02-15 - Razorlight - Golden Touch.m4a",
  "Tracks/02 - 2000s Indie Disco/02-16 - The Zutons - You Will You Won't.m4a","Tracks/02 - 2000s Indie Disco/02-17 - The Hives - Hate to Say I Told You So.m4a","Tracks/02 - 2000s Indie Disco/02-18 - The White Stripes - Seven Nation Army.m4a","Tracks/02 - 2000s Indie Disco/02-19 - Yeah Yeah Yeahs - Heads Will Roll.m4a",
  "Tracks/02 - 2000s Indie Disco/02-20 - Gossip - Standing in the Way of Control.m4a","Tracks/02 - 2000s Indie Disco/02-21 - MGMT - Kids.m4a","Tracks/02 - 2000s Indie Disco/02-22 - Vampire Weekend - A-Punk.m4a","Tracks/02 - 2000s Indie Disco/02-23 - Two Door Cinema Club - What You Know.m4a",
  "Tracks/02 - 2000s Indie Disco/02-24 - Phoenix - Lisztomania.m4a","Tracks/02 - 2000s Indie Disco/02-25 - The Temper Trap - Sweet Disposition.m4a","Tracks/03 - Alternative 80s and New Wave/03-01 - The Cure - Friday I'm in Love.m4a","Tracks/03 - Alternative 80s and New Wave/03-02 - The Cure - Just Like Heaven.m4a",
  "Tracks/03 - Alternative 80s and New Wave/03-03 - The Cure - The Lovecats.m4a","Tracks/03 - Alternative 80s and New Wave/03-04 - The Smiths - This Charming Man.m4a","Tracks/03 - Alternative 80s and New Wave/03-05 - The Smiths - Panic.m4a","Tracks/03 - Alternative 80s and New Wave/03-06 - New Order - Blue Monday.m4a",
  "Tracks/03 - Alternative 80s and New Wave/03-07 - New Order - Bizarre Love Triangle.m4a","Tracks/03 - Alternative 80s and New Wave/03-08 - Depeche Mode - Just Can't Get Enough.m4a","Tracks/03 - Alternative 80s and New Wave/03-09 - Depeche Mode - Enjoy the Silence.m4a","Tracks/03 - Alternative 80s and New Wave/03-10 - Talking Heads - Once in a Lifetime.m4a",
  "Tracks/03 - Alternative 80s and New Wave/03-11 - Talking Heads - Burning Down the House.m4a","Tracks/03 - Alternative 80s and New Wave/03-12 - David Bowie - Let's Dance.m4a","Tracks/03 - Alternative 80s and New Wave/03-13 - David Bowie - Modern Love.m4a","Tracks/03 - Alternative 80s and New Wave/03-14 - Blondie - Heart of Glass.m4a",
  "Tracks/03 - Alternative 80s and New Wave/03-15 - Blondie - Call Me.m4a","Tracks/03 - Alternative 80s and New Wave/03-16 - Soft Cell - Tainted Love.m4a","Tracks/03 - Alternative 80s and New Wave/03-17 - The Human League - Don't You Want Me.m4a","Tracks/03 - Alternative 80s and New Wave/03-18 - Pet Shop Boys - Always on My Mind.m4a",
  "Tracks/03 - Alternative 80s and New Wave/03-19 - Erasure - Sometimes.m4a","Tracks/03 - Alternative 80s and New Wave/03-20 - Dead Or Alive - You Spin Me Round (Like a Record).m4a","Tracks/03 - Alternative 80s and New Wave/03-21 - Eurythmics - Sweet Dreams (Are Made of This).m4a","Tracks/03 - Alternative 80s and New Wave/03-22 - Tears for Fears - Everybody Wants to Rule the World.m4a",
  "Tracks/03 - Alternative 80s and New Wave/03-23 - INXS - Need You Tonight.m4a","Tracks/03 - Alternative 80s and New Wave/03-24 - The B-52's - Roam.m4a","Tracks/03 - Alternative 80s and New Wave/03-25 - They Might Be Giants - Birdhouse in Your Soul.m4a","Tracks/04 - Rock and Pop Punk/04-01 - Queen - Crazy Little Thing Called Love.m4a",
  "Tracks/04 - Rock and Pop Punk/04-02 - Queen - I Want to Break Free.m4a","Tracks/04 - Rock and Pop Punk/04-03 - ACDC - You Shook Me All Night Long.m4a","Tracks/04 - Rock and Pop Punk/04-04 - Kiss - I Was Made for Lovin' You.m4a","Tracks/04 - Rock and Pop Punk/04-05 - Guns N' Roses - Sweet Child O' Mine.m4a",
  "Tracks/04 - Rock and Pop Punk/04-06 - Bon Jovi - You Give Love a Bad Name.m4a","Tracks/04 - Rock and Pop Punk/04-07 - Aerosmith - Walk This Way.m4a","Tracks/04 - Rock and Pop Punk/04-08 - The Rolling Stones - Start Me Up.m4a","Tracks/04 - Rock and Pop Punk/04-09 - The Clash - Should I Stay or Should I Go.m4a",
  "Tracks/04 - Rock and Pop Punk/04-10 - The Clash - Rock the Casbah.m4a","Tracks/04 - Rock and Pop Punk/04-11 - Ramones - Blitzkrieg Bop.m4a","Tracks/04 - Rock and Pop Punk/04-12 - The Undertones - Teenage Kicks.m4a","Tracks/04 - Rock and Pop Punk/04-13 - Buzzcocks - Ever Fallen in Love (With Someone You Shouldn't've).m4a",
  "Tracks/04 - Rock and Pop Punk/04-14 - The Jam - Town Called Malice.m4a","Tracks/04 - Rock and Pop Punk/04-15 - Billy Idol - Dancing with Myself.m4a","Tracks/04 - Rock and Pop Punk/04-16 - The Offspring - Pretty Fly (For a White Guy).m4a","Tracks/04 - Rock and Pop Punk/04-17 - Blink-182 - All the Small Things.m4a",
  "Tracks/04 - Rock and Pop Punk/04-18 - Blink-182 - What's My Age Again.m4a","Tracks/04 - Rock and Pop Punk/04-19 - Sum 41 - In Too Deep.m4a","Tracks/04 - Rock and Pop Punk/04-20 - Jimmy Eat World - The Middle.m4a","Tracks/04 - Rock and Pop Punk/04-21 - Fountains of Wayne - Stacy's Mom.m4a",
  "Tracks/04 - Rock and Pop Punk/04-22 - Paramore - Still into You.m4a","Tracks/04 - Rock and Pop Punk/04-23 - Fall Out Boy - Dance, Dance.m4a","Tracks/04 - Rock and Pop Punk/04-24 - My Chemical Romance - Teenagers.m4a","Tracks/04 - Rock and Pop Punk/04-25 - Foo Fighters - Learn to Fly.m4a",
  "Tracks/05 - Family Disco and Funk/05-01 - Chic - Good Times.m4a","Tracks/05 - Family Disco and Funk/05-02 - Chic - Everybody Dance.m4a","Tracks/05 - Family Disco and Funk/05-03 - Sister Sledge - He's the Greatest Dancer.m4a","Tracks/05 - Family Disco and Funk/05-04 - Sister Sledge - Lost in Music.m4a",
  "Tracks/05 - Family Disco and Funk/05-05 - Earth, Wind & Fire - Boogie Wonderland.m4a","Tracks/05 - Family Disco and Funk/05-06 - Earth, Wind & Fire - Let's Groove.m4a","Tracks/05 - Family Disco and Funk/05-07 - Bee Gees - You Should Be Dancing.m4a","Tracks/05 - Family Disco and Funk/05-08 - Bee Gees - Night Fever.m4a",
  "Tracks/05 - Family Disco and Funk/05-09 - Donna Summer - Hot Stuff.m4a","Tracks/05 - Family Disco and Funk/05-10 - Donna Summer - Last Dance.m4a","Tracks/05 - Family Disco and Funk/05-11 - The Trammps - Disco Inferno.m4a","Tracks/05 - Family Disco and Funk/05-12 - The Emotions - Best of My Love.m4a",
  "Tracks/05 - Family Disco and Funk/05-13 - Cheryl Lynn - Got to Be Real.m4a","Tracks/05 - Family Disco and Funk/05-14 - Evelyn Champagne King - Shame.m4a","Tracks/05 - Family Disco and Funk/05-15 - Chaka Khan - I'm Every Woman.m4a","Tracks/05 - Family Disco and Funk/05-16 - Rufus & Chaka Khan - Ain't Nobody.m4a",
  "Tracks/05 - Family Disco and Funk/05-17 - McFadden & Whitehead - Ain't No Stoppin' Us Now.m4a","Tracks/05 - Family Disco and Funk/05-18 - KC & The Sunshine Band - Give It Up.m4a","Tracks/05 - Family Disco and Funk/05-19 - Kool & The Gang - Get Down on It.m4a","Tracks/05 - Family Disco and Funk/05-20 - Shalamar - A Night to Remember.m4a",
  "Tracks/05 - Family Disco and Funk/05-21 - The Whispers - And the Beat Goes On.m4a","Tracks/05 - Family Disco and Funk/05-22 - Patrice Rushen - Forget Me Nots.m4a","Tracks/05 - Family Disco and Funk/05-23 - Stevie Wonder - Superstition.m4a","Tracks/05 - Family Disco and Funk/05-24 - Stevie Wonder - Sir Duke.m4a",
  "Tracks/05 - Family Disco and Funk/05-25 - Wild Cherry - Play That Funky Music.m4a","Tracks/06 - Ska Soul and Retro Pop/06-01 - Madness - Baggy Trousers.m4a","Tracks/06 - Ska Soul and Retro Pop/06-02 - Madness - Our House.m4a","Tracks/06 - Ska Soul and Retro Pop/06-03 - Madness - It Must Be Love.m4a",
  "Tracks/06 - Ska Soul and Retro Pop/06-04 - The Specials - A Message to You Rudy.m4a","Tracks/06 - Ska Soul and Retro Pop/06-05 - The Specials - Too Much Too Young.m4a","Tracks/06 - Ska Soul and Retro Pop/06-06 - The Beat - Mirror in the Bathroom.m4a","Tracks/06 - Ska Soul and Retro Pop/06-07 - The Beat - Save It for Later.m4a",
  "Tracks/06 - Ska Soul and Retro Pop/06-08 - The Housemartins - Happy Hour.m4a","Tracks/06 - Ska Soul and Retro Pop/06-09 - The Jam - Going Underground.m4a","Tracks/06 - Ska Soul and Retro Pop/06-10 - Dexys Midnight Runners - Geno.m4a","Tracks/06 - Ska Soul and Retro Pop/06-11 - Martha Reeves & The Vandellas - Dancing in the Street.m4a",
  "Tracks/06 - Ska Soul and Retro Pop/06-12 - Martha Reeves & The Vandellas - (Love Is Like A) Heat Wave.m4a","Tracks/06 - Ska Soul and Retro Pop/06-13 - Wilson Pickett - Land of 1000 Dances.m4a","Tracks/06 - Ska Soul and Retro Pop/06-14 - Wilson Pickett - In the Midnight Hour.m4a","Tracks/06 - Ska Soul and Retro Pop/06-15 - The Isley Brothers - This Old Heart of Mine (Is Weak for You).m4a",
  "Tracks/06 - Ska Soul and Retro Pop/06-16 - Frank Wilson - Do I Love You (Indeed I Do).m4a","Tracks/06 - Ska Soul and Retro Pop/06-17 - Gloria Jones - Tainted Love.m4a","Tracks/06 - Ska Soul and Retro Pop/06-18 - Jackie Wilson - (Your Love Keeps Lifting Me) Higher and Higher.m4a","Tracks/06 - Ska Soul and Retro Pop/06-19 - The Four Tops - I Can't Help Myself (Sugar Pie, Honey Bunch).m4a",
  "Tracks/06 - Ska Soul and Retro Pop/06-20 - Daryl Hall & John Oates - You Make My Dreams (Come True).m4a","Tracks/06 - Ska Soul and Retro Pop/06-21 - Paul Simon - You Can Call Me Al.m4a","Tracks/06 - Ska Soul and Retro Pop/06-22 - The Buggles - Video Killed the Radio Star.m4a","Tracks/06 - Ska Soul and Retro Pop/06-23 - Electric Light Orchestra - Mr. Blue Sky.m4a",
  "Tracks/06 - Ska Soul and Retro Pop/06-24 - Fleetwood Mac - Everywhere.m4a","Tracks/06 - Ska Soul and Retro Pop/06-25 - Aztec Camera - Somewhere in My Heart.m4a","Tracks/07 - Dance and Indie Crossover/07-01 - Deee-Lite - Groove Is in the Heart.m4a","Tracks/07 - Dance and Indie Crossover/07-02 - Moloko - Sing It Back.m4a",
  "Tracks/07 - Dance and Indie Crossover/07-03 - Moloko - The Time Is Now.m4a","Tracks/07 - Dance and Indie Crossover/07-04 - Spiller - Groovejet (If This Ain't Love).m4a","Tracks/07 - Dance and Indie Crossover/07-05 - Madison Avenue - Don't Call Me Baby.m4a","Tracks/07 - Dance and Indie Crossover/07-06 - Room 5 - Make Luv.m4a",
  "Tracks/07 - Dance and Indie Crossover/07-07 - Junior Senior - Move Your Feet.m4a","Tracks/07 - Dance and Indie Crossover/07-08 - Jamiroquai - Cosmic Girl.m4a","Tracks/07 - Dance and Indie Crossover/07-09 - Jamiroquai - Canned Heat.m4a","Tracks/07 - Dance and Indie Crossover/07-10 - Groove Armada - Superstylin'.m4a",
  "Tracks/07 - Dance and Indie Crossover/07-11 - The Chemical Brothers - Hey Boy Hey Girl.m4a","Tracks/07 - Dance and Indie Crossover/07-12 - The Chemical Brothers - Galvanize.m4a","Tracks/07 - Dance and Indie Crossover/07-13 - The Prodigy - Out of Space.m4a","Tracks/07 - Dance and Indie Crossover/07-14 - The Prodigy - Everybody in the Place.m4a",
  "Tracks/07 - Dance and Indie Crossover/07-15 - Primal Scream - Loaded.m4a","Tracks/07 - Dance and Indie Crossover/07-16 - Primal Scream - Movin' on Up.m4a","Tracks/07 - Dance and Indie Crossover/07-17 - Cornershop - Brimful of Asha.m4a","Tracks/07 - Dance and Indie Crossover/07-18 - Fatboy Slim - Weapon of Choice.m4a",
  "Tracks/07 - Dance and Indie Crossover/07-19 - Daft Punk - Around the World.m4a","Tracks/07 - Dance and Indie Crossover/07-20 - Daft Punk - Get Lucky.m4a","Tracks/07 - Dance and Indie Crossover/07-21 - Cassius - Cassius 1999.m4a","Tracks/07 - Dance and Indie Crossover/07-22 - Basement Jaxx - Red Alert.m4a",
  "Tracks/07 - Dance and Indie Crossover/07-23 - Basement Jaxx - Romeo.m4a","Tracks/07 - Dance and Indie Crossover/07-24 - The Supermen Lovers - Starlight.m4a","Tracks/07 - Dance and Indie Crossover/07-25 - Justice - D.A.N.C.E.m4a",
};
static void m3uChecks(){
  const String root="/music/Alternative Wedding - UK Nostalgia/";
  for(auto e:crate)library.push_back(root+e);
  total=library.size();playlist_begin();
  auto run=[&](const std::string& text,int found){
    {std::ofstream f(SD.root/"playlists/list.m3u",std::ios::binary);f<<text;}
    File catalogue=SD.open("/.sys/fav.txt"),audio=SD.open("/.sys/sources.txt");
    allocationMax=0;const int before=scans;
    SerialCapture serial;
    check(playlist_select(playlist_sourceAt(SRC_M3U,0))==(found>0),"M3U selection result");
    check((found==0||length==found)&&scans==before+1,"M3U found count/single library pass");
    check(allocationMax<=4000,"M3U key allocation remains at most 4 KB");
    telemetryCheck(serial.finish(), "cmd: playlist list n=[0-9]+ found=[0-9]+ missing=[0-9]+ ambiguous=[0-9]+", "queue cache=miss", total, "ok", true);
    return playlist_lastMatchStats();
  };
  std::string first="\xEF\xBB\xBF#EXTM3U\r\n",all=first;
  for(int i=0;i<175;++i){std::string line=std::string("#EXTINF:1,fixture\r\n")+crate[i]+"\r\n";all+=line;if(i<25)first+=line;}
  auto stats=run(first,25);check(stats.entries==25&&stats.byTail==25&&!stats.missing,"real 25/25 CRLF/BOM");
  for(int i=0;i<25;++i)check(at(i)==i,"real playlist order");
  stats=run(all,175);check(stats.entries==175&&stats.byTail==175,"real crate 175/175");
  stats=run(first+"Tracks/absent.m4a\r\n",25);check(stats.missing==1&&stats.entries==26,"missing counted");
  for(auto e:crate)library.push_back(String("/music/Second copy/")+e);
  total=library.size();playlist_begin();stats=run(first,25);
  check(stats.ambiguous==25&&at(0)==0,"ambiguous tail first library copy");
  stats=run(std::string(library[175].c_str())+"\n"+library[0].substring(7).c_str()+"\nD:\\Music\\Alternative Wedding - UK Nostalgia\\"+crate[1]+"\n",3);
  check(stats.exact==3&&!stats.ambiguous&&at(0)==175,"exact wins over ambiguous tails and old paths");
  library.resize(175);library.push_back("/music/Unicode/BjÃƒÂ¶rk - JÃƒÂ³ga.m4a");total=library.size();playlist_begin();
  stats=run(std::string("./Discard/../")+crate[0]+"\nBjÃƒÂ¶rk - JÃƒÂ³ga.m4a\nD:BjÃƒÂ¶rk - JÃƒÂ³ga.m4a\n",3);
  check(stats.byTail==1&&stats.byName==2,"normalisation and UTF-8 filename fallback");
  library.push_back("/music/Other/BjÃƒÂ¶rk - JÃƒÂ³ga.m4a");total=library.size();playlist_begin();
  stats=run("BjÃƒÂ¶rk - JÃƒÂ³ga.m4a\n",0);check(stats.ambiguous==1&&stats.missing==1,"bare names require uniqueness");
  stats=run(std::string(256,'x')+"\r\n"+crate[0]+"\n",1);check(stats.entries==2&&stats.overlong==1&&stats.missing==1,"long lines drained and counted");
  // Keep catalogue size above the playlist limit; repeated entries need no per-track allocation.
  while(library.size()<1200)library.push_back(String("/music/Filler/")+String(unsigned(library.size()))+".m4a");
  total=library.size();playlist_begin();std::string large;
  for(int i=0;i<1003;++i)large+=std::string(crate[0])+"\n";
  stats=run(large,1000);check(stats.entries==1003&&stats.truncated==3&&stats.missing==3,"MAX_M3U truncation reported");
  std::cout<<"PASS M3U real 25/25 and 175/175; exact/tail/name/order/ambiguity/missing/CRLF/BOM/UTF-8/overlong/truncation; one scan; key allocation <=4000 bytes\n";
  library.clear();
}
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
    {
      brokenScan=true; SerialCapture serial;
      check(!playlist_select(2),"partial catalogue scan refused");
      telemetryCheck(serial.finish(), "cmd: source folder 2", "queue", n-1, "fail", true);
      brokenScan=false;
    }
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
    {
      activePlayback=false; SerialCapture serial;
      check(playlist_preview(3,preview)&&preview.queueCount()==3&&preview.queueAt(0)==7&&preview.queueAt(1)==2&&preview.queueAt(2)==7,"preview M3U parity");
      telemetryCheck(serial.finish(), "cmd: playlist list n=4 found=3 missing=1 ambiguous=0", "preview cache=miss", n, "ok", false);
      activePlayback=true;
    }
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
  m3uChecks();
  PlaylistQueue empty;check(empty.queueCount()==0&&empty.queueAt(0)==-1&&empty.queueFind(0)==-1,"empty");
  PlaylistQueue one;one.count=1;check(one.queueAt(0)==0&&one.queueAt(1)==-1&&one.queueFind(0)==0,"one identity");
  total=1;playlist_begin();check(playlist_select(2)&&length==1&&at(0)==0,"one SD source");
  total=0;playlist_begin();check(!playlist_select(2),"empty source refused");
  {
    total=10; std::ofstream(SD.root/".sys/fav.txt"); playlist_begin();
    SerialCapture serial;
    check(!playlist_select(1), "empty favourites unchanged");
    telemetryCheck(serial.finish(), "cmd: source favourites 1", "queue", 0, "ok", true);
  }
  {
    SerialCapture serial; check(playlist_select(0), "All identity");
    check(serial.finish().empty(), "All has no build telemetry");
  }
  std::cout<<"PASS telemetry exact fields/roles/status/playing; playlist records equal fixture library; partial scan/empty favourites/All identity\n";
  std::cout<<"PASS queue empty/one-track sources\n";
}catch(const std::exception& e){std::cerr<<"FAIL queue: "<<e.what()<<'\n';return 1;}}

