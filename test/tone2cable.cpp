// tone2cable —— 把 1kHz 正弦 + 粉噪轮流送到 CABLE Input，模拟「音乐软件」输出
#include "audio.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
int main(int argc,char**argv){
  CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  std::wstring id,name;
  for(const auto&d:ListRenderDevices()) if(d.name.find(L"CABLE Input")!=std::wstring::npos){id=d.id;name=d.name;}
  if(id.empty()){printf("no cable\n");return 1;}
  Settings s; s.load(); s.testTone=TT_SINE; s.testFreq=1000.f; s.testLevel=-12.f; s.bypass=false;
  AudioEngine e; e.setRenderDevice(id,name);
  if(!e.start()){printf("start fail\n");return 2;}
  e.updateSettings(s);
  int secs = (argc>1)? atoi(argv[1]) : 30;
  for(int i=0;i<secs*2;++i){
    Sleep(500);
    if(i==10){ s.testTone=TT_PINK; e.updateSettings(s); }
  }
  e.stop(); CoUninitialize(); return 0;
}
