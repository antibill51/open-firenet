// host_bridge.cpp — expose la logique RÉELLE du firmware (firenet_link.h) en REPL,
// pour l'accoupler au simulateur de poêle Python (sim/stove_sim.py).
// Horloge contrôlée par le harnais -> déterministe. Voir sim/bench.py.
#include "firenet_link.h"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace firenet;

static uint32_t    g_clk = 0;

static std::string toHex(const std::string& s){
  std::ostringstream o; o<<std::hex<<std::setfill('0');
  for(unsigned char c: s) o<<std::setw(2)<<(int)c;
  return o.str();
}
static std::string fromHex(const std::string& h){
  std::string o; for(size_t i=0;i+1<h.size();i+=2)
    o += (char)strtol(h.substr(i,2).c_str(),nullptr,16);
  return o;
}

int main(){
  DongleLink link(
    [](const uint8_t* d,size_t n){
      std::cout<<"TX "<<toHex(std::string((const char*)d,n))<<"\n"; },
    [](){ return g_clk; });

  std::string line;
  while(std::getline(std::cin,line)){
    std::istringstream is(line); std::string cmd; is>>cmd;
    if(cmd=="SETSTAGE"){ int s; is>>s; link.debugSetStage(s); }
    else if(cmd=="RX"){ std::string h; is>>h; for(char c: fromHex(h)) link.onByte((uint8_t)c); }
    else if(cmd=="TICK"){ uint32_t d; is>>d; g_clk+=d; link.poll(); }
    else if(cmd=="REQSTATUS"){ link.requestStatus(); }
    else if(cmd=="PUSHSTATUS"){ std::string s,w; is>>s>>w; link.pushStatus(s,w); }
    else if(cmd=="POLLSENS"){ std::vector<std::string> n;
      for(int i=0;i<NUM_SENSOR_LABELS;i++) n.push_back(sensName(i));
      link.pollSensors(n); }
    else if(cmd=="POLLCTRL"){ std::vector<std::string> n;
      for(int i=0;i<NUM_CONTROL_LABELS;i++) n.push_back(ctrlName(i));
      link.pollControls(n); }
    else if(cmd=="POLLSENS0"){ link.pollSensors({}); }
    else if(cmd=="SETREV"){ long r; is>>r; link.setRevision(r); }
    else if(cmd=="NETWORKS"){
      std::string kv; is>>kv; std::vector<std::pair<std::string,int>> nets;
      size_t p=0; while(p<kv.size()){ size_t c=kv.find(',',p); if(c==std::string::npos)c=kv.size();
        std::string it=kv.substr(p,c-p); size_t col=it.find(':');
        if(col!=std::string::npos) nets.push_back({it.substr(0,col),atoi(it.substr(col+1).c_str())});
        p=c+1; }
      link.sendNetworks(nets);
    }
    else if(cmd=="SETONE"){
      std::string kv; is>>kv; size_t e=kv.find('=');
      std::string name=kv.substr(0,e); long val=strtol(kv.substr(e+1).c_str(),nullptr,10);
      const auto& m=link.model();
      std::vector<std::pair<std::string,long>> full;
      for(auto& c: m.controls) full.push_back({c.first, c.first==name? val : c.second});
      if(full.empty()) std::cout<<"ERR no controls\n";
      else link.applyControls(full);
    }
    else if(cmd=="APPLY"){
      std::string kv; is>>kv; std::vector<std::pair<std::string,long>> full;
      size_t p=0; while(p<kv.size()){ size_t c=kv.find(',',p); if(c==std::string::npos)c=kv.size();
        std::string it=kv.substr(p,c-p); size_t e=it.find('=');
        if(e!=std::string::npos) full.push_back({it.substr(0,e),strtol(it.substr(e+1).c_str(),nullptr,10)});
        p=c+1; }
      link.applyControls(full);
    }
    else if(cmd=="STATE"){
      const auto& m=link.model();
      std::cout<<"STATE ack="<<m.version_ack<<" gen="<<m.generation
               <<" in="<<m.frames_in<<" out="<<m.frames_out
               <<" nsens="<<m.sensors.size()<<" nctrl="<<m.controls.size();
      auto it=m.status.find("ssid"); if(it!=m.status.end()) std::cout<<" ssid="<<it->second;
      auto t=m.sensors.find("roomTemp"); if(t!=m.sensors.end()) std::cout<<" roomTemp="<<t->second;
      auto ms=m.sensors.find("mainState"); if(ms!=m.sensors.end()) std::cout<<" mainState="<<ms->second;
      auto ss=m.sensors.find("subState"); if(ss!=m.sensors.end()) std::cout<<" subState="<<ss->second;
      std::cout<<" spn="<<m.sensors_pos.size();
      for(size_t i=0;i<m.sensors_pos.size();i++) std::cout<<" sp"<<i<<"="<<m.sensors_pos[i];
      for(size_t i=0;i<m.controls_pos.size();i++) std::cout<<" cp"<<i<<"="<<m.controls_pos[i];
      std::cout<<"\n";
    }
    while(link.txPending()>0){ g_clk += DongleLink::TX_GAP_MS; link.poll(); }
    std::cout<<"OK\n"; std::cout.flush();
  }
  return 0;
}
