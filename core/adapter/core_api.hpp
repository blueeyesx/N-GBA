#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace ngba::core {
struct CoreOptions { bool use_bios{false}; std::string bios_path; bool skip_bios{true}; bool audio{true}; };
struct FrameBuffer { int width{240},height{160}; std::vector<uint16_t> rgb565; };
class CoreApi { public: virtual ~CoreApi()=default; virtual bool load_rom(const std::string&)=0; virtual bool initialize(const CoreOptions&)=0; virtual void reset()=0; virtual void run_frame(uint16_t input_mask)=0; virtual const FrameBuffer& framebuffer() const=0; virtual bool save_state(const std::string&)=0; virtual bool load_state(const std::string&)=0; virtual void shutdown()=0; };
}
