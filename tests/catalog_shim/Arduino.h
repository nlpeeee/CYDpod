#pragma once
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <mutex>
#include <chrono>
#include <cmath>
#include <stdexcept>
constexpr double PI=3.14159265358979323846;
using std::sin;using std::cos;
using std::min;using std::max;
template<class T> T constrain(T x,T lo,T hi){return std::min(hi,std::max(x,lo));}
class String {
  std::string s;
 public:
  String()=default;String(const char* p):s(p?p:""){}String(const std::string& p):s(p){}
  String(int n):s(std::to_string(n)){}String(unsigned n):s(std::to_string(n)){}
  const char* c_str()const{return s.c_str();}size_t length()const{return s.size();}bool isEmpty()const{return s.empty();}
  char operator[](size_t i)const{return i<s.size()?s[i]:0;}
  String& operator+=(const String& v){s+=v.s;return *this;}
  friend String operator+(const String& a,const String& b){return a.s+b.s;}
  friend bool operator==(const String&a,const String&b){return a.s==b.s;}
  friend bool operator!=(const String&a,const String&b){return !(a==b);}
  friend bool operator<(const String&a,const String&b){return a.s<b.s;}
  bool startsWith(const String& p)const{return s.rfind(p.s,0)==0;}
  bool endsWith(const String& p)const{return s.size()>=p.s.size()&&s.compare(s.size()-p.s.size(),p.s.size(),p.s)==0;}
  void toLowerCase(){for(char& c:s)if((unsigned char)c<128)c=char(tolower((unsigned char)c));}
  int indexOf(char c,int start=0)const{auto n=s.find(c,start);return n==s.npos?-1:int(n);}
  int indexOf(const char* c,int start=0)const{auto n=s.find(c,start);return n==s.npos?-1:int(n);}
  int lastIndexOf(char c)const{auto n=s.rfind(c);return n==s.npos?-1:int(n);}
  String substring(size_t a,size_t b=std::string::npos)const{return a>s.size()?String():String(s.substr(a,b==s.npos?b:b-a));}
  long toInt()const{return strtol(s.c_str(),nullptr,10);}
  void replace(char a,char b){std::replace(s.begin(),s.end(),a,b);}
  void trim(){while(!s.empty()&&isspace((unsigned char)s.back()))s.pop_back();size_t n=0;while(n<s.size()&&isspace((unsigned char)s[n]))++n;s.erase(0,n);}
};
struct HostSerial {
  template<class... T>void printf(const char* fmt,T...args){std::printf(fmt,args...);}
  void println(const char* p=""){std::puts(p);}
};
inline HostSerial Serial;
inline uint32_t millis(){return uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()/1000000);}
using SemaphoreHandle_t=std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return new std::mutex;}
constexpr unsigned portMAX_DELAY=0xffffffff;
inline void xSemaphoreTake(SemaphoreHandle_t m,unsigned){m->lock();}
inline void xSemaphoreGive(SemaphoreHandle_t m){m->unlock();}
inline unsigned hostFreeHeap=64000,hostLargestBlock=64000;
struct HostESP{unsigned getFreeHeap()const{return hostFreeHeap;}unsigned getMaxAllocHeap()const{return hostLargestBlock;}void restart(){throw std::runtime_error("device restart requested");}};
inline HostESP ESP;
