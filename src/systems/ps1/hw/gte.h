#pragma once

#include <array>
#include "guest/r3000/r3000.h"
#include "guest/r3000/r3000_ir.h"

namespace zoo::ps1 {

class GTE final : public guest::r3000::Coprocessor {
private:
  using Operand = ::fox::ir::Operand;
  using Matrix = std::array<std::array<Operand, 3>, 3>;
  using Vector = std::array<Operand, 3>;

  // Retrieve a particular component of one of V0,V1,..
  Operand get_vec(u32 vector_num, u32 component);
  void put_vec(u32 vector_num, u32 component, Operand val);

  //
  Vector get_translation();
  void put_translation(Vector v);

  Vector BK();
  void push_color(Vector rgb);

  template<u32 n>
  void avsz();

  void dpcs(bool lm, bool sf);
  void intpl(bool lm, bool sf);
  void dcpl(bool lm, bool sf);
  void depth_cue_shared(bool lm, bool sf);

  void gpf(bool lm, bool sf);
  void gpl(bool lm, bool sf);

  void sqr(bool sf);

  // clang-format off
  template<u32 i> Operand MAC();
  template<u32 i> Operand set_mac(Operand);
  template<u32 i> Operand IR();
  template<u32 i> Operand set_ir(Operand, bool lm);
  // clang-format on

  void push_screen_xy(Operand, Operand);
  void push_screen_z(Operand);

  void op(bool lm, bool sf);

  // two_vec = color/color or color/depth
  template<u32 vnum, bool two_vec, bool depth_cue>
  void nccs(bool lm, bool sf);

  template<u32 vec_num>
  void rtps(bool lm, bool sf);
  void rtpt(bool lm, bool sf);
  void nclip(bool lm, bool sf);

  void mvmva(bool lm, bool sf, u32 m, u32 v, u32 c);

  Operand div_unr();

  enum class MatrixType
  {
    Rotation,
    Light,
    LightColor,
  };
  Matrix get_matrix(MatrixType);

  template<u32 vector_num>
  Vector get_vector();
  template<u32 vector_num>
  void put_vector(Vector);

  Operand load_cop2data(u32 index);
  void store_cop2data(u32 index, Operand);
  Operand load_cop2ctrl(u32 index);
  void store_cop2ctrl(u32 index, Operand);

  Vector calc_mvv(Matrix m, Vector v, Vector c, bool lm, bool sf);

  template<u32 num>
  void set_mac_ir(Operand, bool lm, bool sf);

  // Generic IR helpers
  Operand ext64(Operand);

  void set_flag(Operand condition, u32 overflow_flag_bit);
  Operand clamp16(Operand in, Operand min_val, Operand max_val, u32 overflow_flag_bit);

  Operand lower_16(Operand op);
  Operand upper_16(Operand op);

public:
  u32 handle_cop_ir(u32 cofun) override;
};

}
