#pragma once

#include "ship/resource/Resource.h"
#include "ship/resource/ResourceFactoryBinary.h"
#include "ship/resource/ResourceFactoryXML.h"

namespace Ship {
class BinaryReader;
}

namespace Fast {
class Vertex;

// Reads `count` vertex records into `vertex` and sets its RecordSize: the vanilla 16-byte record (s16
// positions) or the 22-byte v1 record (s32 positions, otherwise identical). Shared by every factory that
// carries vertices - the Vertex resource here and the game's generic Array resource - so the two encodings
// are defined once.
void ReadVertexRecords(Ship::BinaryReader& reader, Vertex& vertex, uint32_t count, bool s32Positions);

class ResourceFactoryBinaryVertexV0 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

// v1: s32 positions, so a room mesh can leave the +/-32767 range. Identical to v0 otherwise.
class ResourceFactoryBinaryVertexV1 final : public Ship::ResourceFactoryBinary {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};

class ResourceFactoryXMLVertexV0 final : public Ship::ResourceFactoryXML {
  public:
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File> file,
                                                  std::shared_ptr<Ship::ResourceInitData> initData) override;
};
} // namespace Fast
