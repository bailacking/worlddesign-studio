#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>

namespace wds::voxel {

struct Vec3 {
  double x, y, z;
};

enum class Block { air, grass, dirt, stone };

struct Hit {
  Block block = Block::air;
  double distance = 0;
  int side = 0;
  int x = 0;
  int y = 0;
  int z = 0;
  int normal = 0;
};

class World {
 public:
  static constexpr int width = 24;
  static constexpr int height = 12;
  static constexpr int depth = 24;
  World();

  [[nodiscard]] Block block(int x, int y, int z) const;
  [[nodiscard]] bool collides(Vec3 feet) const;
  [[nodiscard]] Hit cast(Vec3 origin, Vec3 direction, double limit) const;
  bool edit(Vec3 feet, Vec3 eye, Vec3 direction, bool place);
  bool read(std::istream& input);
  void write(std::ostream& output) const;

 private:
  std::array<Block, width * height * depth> blocks_{};
};

struct Player {
  Vec3 feet{12.5, 2.0, 17.5};
  double yaw = 0;
  double pitch = 0;
  double fall_speed = 0;
  bool grounded = false;

  void step(const World& world, double forward, double strafe, bool jump, double seconds);
  [[nodiscard]] Vec3 eye() const;
  [[nodiscard]] Vec3 view() const;
};

[[nodiscard]] std::uint32_t pixel(const World& world, Vec3 origin, Vec3 direction);

}  // namespace wds::voxel
