#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include "../firmware/waste_sorter/control.h"

int main(int argc,char** argv) {
  unsigned wet=waste::WET_MAX,dry=waste::DRY_MIN;
  if (argc!=1 && argc!=3) return 2;
  if (argc==3) {
    char *a=nullptr,*b=nullptr;
    const long w=std::strtol(argv[1],&a,10),d=std::strtol(argv[2],&b,10);
    if (!a || !b || a==argv[1] || b==argv[2] || *a || *b || w<0 || w>=d || d>1023) return 2;
    wet=unsigned(w);dry=unsigned(d);
  }
  std::cout<<"{\"wet_max\":"<<wet<<",\"dry_min\":"<<dry<<"}\n";
  std::string line;
  while(std::getline(std::cin,line)) {
    std::istringstream in(line);int moisture,votes;std::string extra;
    if (!(in>>moisture>>votes) || (in>>extra) || moisture<0 || moisture>1023 || votes<0 || votes>10) return 2;
    const waste::Category c=waste::classify(uint16_t(moisture),uint8_t(votes),uint16_t(wet),uint16_t(dry));
    const char* name=c==waste::METAL?"METAL":c==waste::WET?"WET":c==waste::DRY?"DRY":"UNKNOWN";
    std::cout<<name<<'\n';
  }
  return std::cin.bad()?2:0;
}
