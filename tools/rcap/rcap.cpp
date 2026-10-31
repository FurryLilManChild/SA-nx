// Offline re-render of an F11 capture through the real Vulkan renderer (headless, lavapipe here).
//   rcap STEM OUT.ppm   reads STEM.ta, STEM.vram, STEM.vram.regs
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "dream/render/background.h"
#include "dream/render/display_list.h"
#include "dream/render/vk/context.h"
#include "dream/render/vk/offscreen.h"
#include "dream/render/vk/renderer.h"
using namespace dream::render;
static std::vector<char> rd(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
  if(argc<3){std::puts("rcap STEM OUT.ppm");return 2;}
  std::string stem=argv[1];
  auto ta=rd(stem+".ta"); auto vram=rd(stem+".vram"); auto regsb=rd(stem+".vram.regs");
  if(ta.empty()||vram.empty()||regsb.size()<8192){std::puts("missing capture files");return 2;}
  std::vector<std::uint32_t> regs(regsb.size()/4); std::memcpy(regs.data(),regsb.data(),regsb.size());
  vk::Context ctx; if(!ctx.create({},std::getenv("RCAP_VALIDATE")!=nullptr)){std::printf("context: %s\n",ctx.error().c_str());return 1;}
  vk::Offscreen off; if(!off.create(ctx,640,480)){std::printf("offscreen: %s\n",off.error().c_str());return 1;}
  vk::Renderer r; if(!r.create(ctx,off.render_pass())){std::printf("renderer: %s\n",r.error().c_str());return 1;}
  auto pf=static_cast<PaletteFormat>(regs[0x108/4]&3);
  r.set_memory(reinterpret_cast<const std::uint8_t*>(vram.data()),vram.size(),regs.data()+0x1000/4,pf);
  r.textures().set_stride_width((regs[0xE4/4]&31u)*32u);
  DisplayList dl; dl.feed_stream(reinterpret_cast<const std::uint32_t*>(ta.data()),ta.size()/4);
  add_background(dl.frame(),regs.data(),reinterpret_cast<const std::uint8_t*>(vram.data()),vram.size());
  vk::FrameGeometry g; g.alpha_ref=static_cast<float>(regs[0x11C/4]&0xFFu)/255.0f;
  off.readback = std::getenv("RCAP_NO_READBACK") == nullptr;  // pixels() then copies on demand
  off.lean_oit = std::getenv("RCAP_LEAN") != nullptr;  // the 8-layer resolve
  r.sort_triangles = std::getenv("RCAP_TRIANGLES") != nullptr;  // per-triangle sort (with DREAM_NO_OIT=1)
  if(!off.render(r,dl.frame(),g)){std::printf("render: %s\n",off.error().c_str());return 1;}
  const std::uint32_t* px0=off.pixels(); (void)px0;
  std::printf("layers per pixel (estimate): %.2f, translucent polygons %zu, fragments %u of %u, long lists %llu, unterminated %llu, longest %u\n", static_cast<double>(off.stats.depth_max), dl.frame().lists[2].size(), off.stats.fragments_max, off.stats.capacity, (unsigned long long)off.stats.long_lists, (unsigned long long)off.stats.unterminated, off.stats.longest_walk);
  const std::uint32_t* px=off.pixels(); FILE*f=std::fopen(argv[2],"wb");
  std::fprintf(f,"P6 %u %u 255\n",off.width(),off.height());
  for(std::uint32_t i=0;i<off.width()*off.height();i++){unsigned char c[3]={(unsigned char)px[i],(unsigned char)(px[i]>>8),(unsigned char)(px[i]>>16)};std::fwrite(c,1,3,f);}
  std::fclose(f); std::printf("ok %ux%u on %s\n",off.width(),off.height(),ctx.caps().device_name.c_str()); return 0;}
