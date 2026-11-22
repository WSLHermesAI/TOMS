// floor_mesh.h -- a floor's walls and ground as a 3D model, made from its tile map when the floor is entered
// (docs/gameplay/24_STAGE_3D.md §3). One tile is one metre: x the column, z the row (south +z), y up. Each wall cell
// ('#') is a block `wallHeight` tall: its top, and a side wherever the cell beside it is not a wall (the map's edge
// too). The ground is a square a cell. Stairs ('U' up, 'D' down) are four steps walked onto from an open side:
// up to the walls' top, or down into a pit as deep. Three materials (ground, wall, top) use the look's painted
// textures, stage3d/looks/<look>/<part>_c.jpg and <part>_n.png (tools/stage3d/paint_looks.py).
//
// The model is a glTF binary (.glb) in memory, read by the same loader as any model (gltf::f_LoadModelFromMemory with
// the base folder <assets>/stage3d), so the floor is drawn, lit and shadowed like the battle stage's props.
#pragma once
#include <string>
#include <vector>

namespace toms::next {

// walls: the map's rows, '#' = a wall, 'U' / 'D' = stairs up / down (anything else is open ground). look: the folder under stage3d/looks/.
std::string f_MakeFloorGlb(const std::vector<std::string>& l_vWalls, const std::string& l_strLook, float l_fWallHeight = 1.0f,
                           float l_fGroundRepeat = 2.0f);

}  // namespace toms::next
