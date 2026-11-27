// route_test2 —— 用 UI 里实际选择的设备组合验证：捕获默认播放设备（真实耳机）的回环，
// 同时外部播放器（SoundPlayer）往该设备放 test_long.wav。
#include "audio.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>
static std::string U8(const std::wstring& w){ if(w.empty())return{}; int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr); std::string s(n,0); WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),&s[0],n,nullptr,nullptr); return s; }
int main(){
  CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  std::wstring id,name;
  for(const auto&d:ListRenderDevices()){ printf(" dev: %s %s\n",U8(d.name).c_str(),d.isDefault?"(默认)":""); if(id.empty()&&d.isDefault){id=d.id;name=d.name;} }
  printf("用默认设备: %s\n",U8(name).c_str());
  Settings s; s.load(); s.testTone=TT_SINE; s.testFreq=1000.f; s.testLevel=-12.f; s.bypass=false;
  AudioEngine e; e.setRenderDevice(id,name);
  if(!e.start()){printf("start fail\n");return 2;}
  e.updateSettings(s); Sleep(500);
  printf("render: %s ok=%d\n",U8(e.deviceName()).c_str(),(int)e.deviceOk());
  if(!e.startCaptureDevice(id,name)){printf("cap start fail\n");return 3;}
  for(int i=0;i<12;++i){Sleep(300);MeterData m=e.meter();printf("t=%.1f cap=%d rms=%.4f peak=%.4f\n",(i+1)*0.3,(int)e.isCapturing(),m.rmsL,m.peakL);}
  printf("captureError: %s\n",U8(e.captureError()).c_str());
  e.stopCapture(); e.stop(); CoUninitialize(); return 0;
}
