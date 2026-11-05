// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "belmont_map_control.h"
#include <cstring>
namespace belmont_pin_transaction {
// Re-read the complete native placement transaction immediately before the one
// permitted scalar write. Managed pin storage is allocated and changed only by
// the game's subsequent native validation action.
template<class Read,class Write>
bool Apply(Read& read,Write& write,const belmont::Identity& id,
           const belmont::Snapshot& source,const belmont_map_control::Request& request,
           const belmont_map_control::Step& step) {
  using namespace belmont_map_control;
  if(id.title!=belmont::Title||id.build!=belmont::Build||!source.supported||
     !source.coherent||!request.source_proven||request.operation!=Operation::Add||
     !step.write_position||step.position[0]!=request.x||step.position[1]!=request.y)
    return false;
  for(float value:request.bounds)if(!std::isfinite(value))return false;
  if(!std::isfinite(request.x)||!std::isfinite(request.y)||
     request.bounds[2]<=request.bounds[0]||request.bounds[3]<=request.bounds[1]||
     request.x<request.bounds[0]||request.x>request.bounds[2]||
     request.y<request.bounds[1]||request.y>request.bounds[3])return false;
  Decoder decoder(read);const auto fresh=decoder.Sample(id,source);
  if(!fresh.ready||!fresh.coherent||!fresh.menu_open||!fresh.map_tab||
     !fresh.palette||!fresh.place_palette||fresh.change_palette||!fresh.top_layer||
     fresh.entity!=step.entity||fresh.entity!=source.entity||
     fresh.markers!=step.markers||fresh.palette_object!=step.palette||
     fresh.position_address!=step.address||step.address!=fresh.markers+0x48||
     fresh.level_guid!=request.level_guid||fresh.navigation_index!=request.navigation_index||
     fresh.placement!=step.expected_position)return false;
  std::array<float,2> current{};
  if(!read(step.address,current.data(),sizeof current)||current!=step.expected_position)
    return false;
  if(!write(step.address,step.position.data(),sizeof step.position))return false;
  return read(step.address,current.data(),sizeof current)&&current==step.position;
}
} // namespace belmont_pin_transaction
