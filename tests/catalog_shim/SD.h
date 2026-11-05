#pragma once
#include "Arduino.h"
#include <filesystem>
#include <memory>
#include <vector>
#define FILE_WRITE "w"
#define FILE_APPEND "a"
inline uint64_t hostOpenCalls=0,hostReadCalls=0,hostWriteCalls=0,hostCloseCalls=0;
inline int64_t hostWriteBudget=-1;
inline int64_t hostReadBudget=-1;
inline bool hostReadOnly=false;  // write-protected card: mkdir, remove, rename and any non-read open fail
// CY-42: the device mounts SD with max_files=4, so a fifth regular file open at once fails there.
// Directory handles use opendir and do not count. The shim fails the open like the device and
// the process exits non-zero if the limit was ever hit.
inline constexpr int hostMaxOpenFiles=4;
inline int hostOpenFiles=0,hostOpenHighWater=0,hostOpenRefused=0;
struct HostOpenReport{int touched=0;~HostOpenReport(){fprintf(stderr,"open-file high water %d (limit %d)\n",hostOpenHighWater,hostMaxOpenFiles);
  if(hostOpenRefused){fprintf(stderr,"FAIL: %d open(s) refused, more than %d regular files open at once\n",hostOpenRefused,hostMaxOpenFiles);_Exit(97);}}};
inline HostOpenReport hostOpenReport;
class File {
  struct Handle {
    FILE* file=nullptr;std::filesystem::path path;std::string name;
    bool directory=false;std::vector<std::filesystem::path> entries;size_t next=0;
    ~Handle(){if(file){++hostCloseCalls;fclose(file);--hostOpenFiles;}}
  };
  std::shared_ptr<Handle> h;
 public:
  File()=default;
  File(const std::filesystem::path& p,const char* mode){
    ++hostOpenCalls;++hostOpenReport.touched;  // real use so the linker keeps the end-of-run report
    if(std::filesystem::is_directory(p)){h=std::make_shared<Handle>();h->directory=true;
      for(auto& e:std::filesystem::directory_iterator(p))h->entries.push_back(e.path());std::sort(h->entries.begin(),h->entries.end());
    }else{if(hostOpenFiles>=hostMaxOpenFiles){++hostOpenRefused;return;}FILE* f=fopen(p.string().c_str(),(std::string(mode)+"b").c_str());if(!f)return;h=std::make_shared<Handle>();h->file=f;if(++hostOpenFiles>hostOpenHighWater)hostOpenHighWater=hostOpenFiles;}
    h->path=p;h->name=p.filename().string();
  }
  explicit operator bool()const{return bool(h);}
  bool isDirectory()const{return h&&h->directory;}
  const char* name()const{return h?h->name.c_str():"";}
  File openNextFile(){return !h||h->next>=h->entries.size()?File():File(h->entries[h->next++],"r");}
  void close(){h.reset();}
  size_t size(){if(!h||!h->file)return 0;long p=ftell(h->file);fseek(h->file,0,SEEK_END);long n=ftell(h->file);fseek(h->file,p,SEEK_SET);return size_t(n);}
  size_t position(){return h&&h->file?size_t(ftell(h->file)):0;}
  bool seek(uint32_t p){return h&&h->file&&fseek(h->file,long(p),SEEK_SET)==0;}
  size_t read(uint8_t* p,size_t n){++hostReadCalls;if(hostReadBudget>=0){n=std::min<size_t>(n,size_t(hostReadBudget));hostReadBudget-=n;}return h&&h->file?fread(p,1,n,h->file):0;}
  size_t write(const uint8_t* p,size_t n){++hostWriteCalls;if(!h||!h->file)return 0;if(hostWriteBudget>=0){n=std::min<size_t>(n,size_t(hostWriteBudget));hostWriteBudget-=n;}return fwrite(p,1,n,h->file);}
  size_t print(const char* p){return write((const uint8_t*)p,strlen(p));}
  size_t println(const String& s){size_t n=print(s.c_str());return n+print("\r\n");}
  size_t printf(const char* fmt,...){char p[2048];va_list a;va_start(a,fmt);int n=vsnprintf(p,sizeof(p),fmt,a);va_end(a);return n>=0&&n<int(sizeof(p))?write((const uint8_t*)p,n):0;}
  bool available(){return position()<size();}
  String readStringUntil(char c){std::string s;int v;while(h&&h->file&&(v=fgetc(h->file))!=EOF&&v!=c)s.push_back(char(v));return s;}
  bool setBufferSize(size_t n){return h&&h->file&&setvbuf(h->file,nullptr,_IOFBF,n)==0;}
  void flush(){if(h&&h->file)fflush(h->file);}
  uint32_t getLastWrite(){return uint32_t(std::filesystem::last_write_time(h->path).time_since_epoch().count());}
};
struct HostSD {
  std::filesystem::path root;
  File open(const String& p,const char* mode="r"){if(hostReadOnly&&strcmp(mode,"r"))return File();return File(root/std::filesystem::path(p.c_str()).relative_path(),mode);}
  bool exists(const String& p){return std::filesystem::exists(root/std::filesystem::path(p.c_str()).relative_path());}
  bool mkdir(const String& p){if(hostReadOnly)return false;return std::filesystem::create_directories(root/std::filesystem::path(p.c_str()).relative_path());}
  bool remove(const String& p){if(hostReadOnly)return false;return std::filesystem::remove(root/std::filesystem::path(p.c_str()).relative_path());}
  bool rename(const String& a,const String& b){if(hostReadOnly)return false;std::error_code e;std::filesystem::rename(root/std::filesystem::path(a.c_str()).relative_path(),root/std::filesystem::path(b.c_str()).relative_path(),e);return !e;}
};
inline HostSD SD;
