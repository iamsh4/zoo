#include <algorithm>
#include <fmt/core.h>

#include "systems/ps1/hw/gte.h"

using namespace fox::ir;

namespace zoo::ps1 {

#define IR_PRINT(...)                                                                    \
  a->call(Type::Integer32, [](fox::Guest *) {                                            \
    printf(__VA_ARGS__);                                                                 \
    return fox::Value { .u32_value = 0 };                                                \
  });

#define IR_PRINT_U32(__name, __val)                                                      \
  a->call(                                                                               \
    Type::Integer32,                                                                     \
    [](fox::Guest *, fox::Value v) {                                                     \
      printf("%s: 0x%08x\n", __name, v.u32_value);                                       \
      return fox::Value { .u32_value = 0 };                                              \
    },                                                                                   \
    __val);

#define IR_PRINT_VECTOR(__name, __vec)                                                   \
  a->call(                                                                               \
    Type::Integer32,                                                                     \
    [](fox::Guest *, fox::Value x, fox::Value y) {                                       \
      printf("%s[0]=0x%04x (%f)\n", __name, x.u32_value, i16_to_float(x.u32_value));     \
      printf("%s[1]=0x%04x (%f)\n", __name, y.u32_value, i16_to_float(y.u32_value));     \
      return fox::Value { .u32_value = 0 };                                              \
    },                                                                                   \
    __vec[0],                                                                            \
    __vec[1]);                                                                           \
  a->call(                                                                               \
    Type::Integer32,                                                                     \
    [](fox::Guest *, fox::Value x) {                                                     \
      printf("%s[2]=0x%04x (%f)\n", __name, x.u32_value, i16_to_float(x.u32_value));     \
      return fox::Value { .u32_value = 0 };                                              \
    },                                                                                   \
    __vec[2]);

#define IR_PRINT_MATRIX(__name, __matrix)                                                \
  IR_PRINT_VECTOR(__name "[0]", __matrix[0]);                                            \
  IR_PRINT_VECTOR(__name "[1]", __matrix[1]);                                            \
  IR_PRINT_VECTOR(__name "[2]", __matrix[2]);

enum FlagBits
{
  Error_Flag             = 31,
  MAC1_OverflowPositive  = 30,
  MAC2_OverflowPositive  = 29,
  MAC3_OverflowPositive  = 28,
  MAC1_OverflowNegative  = 27,
  MAC2_OverflowNegative  = 26,
  MAC3_OverflowNegative  = 25,
  IR1_Saturated          = 24,
  IR2_Saturated          = 23,
  IR3_Saturated          = 22,
  COLOR_FIFO_R_SATURATED = 21,
  COLOR_FIFO_G_SATURATED = 20,
  COLOR_FIFO_B_SATURATED = 19,
  SZ3_Or_OTZ_Saturated   = 18,
  Divide_Overflow        = 17,
  MAC0_OverflowPositive  = 16,
  MAC0_OverflowNegative  = 15,
  SX2_Saturated          = 14,
  SY2_Saturated          = 13,
  IR0_Saturated          = 12,
};

template<u32 start_bit>
u32
clz(u32 in)
{
  u32 count = 0;
  for (i32 i = start_bit; i >= 0; --i) {
    if (in & (1 << i)) {
      break;
    } else {
      count++;
    }
  }
  return count;
}

Operand
const_u32(const u32 value)
{
  return Operand::constant<u32>(value);
}

Operand
const_u16(const u16 value)
{
  return Operand::constant<u16>(value);
}

Operand
const_bool(const bool value)
{
  return fox::ir::Operand::constant<bool>(value);
}

Operand
GTE::ext64(Operand in)
{
  return a->extend64(in);
}

Operand
GTE::lower_16(Operand op)
{
  return a->extend32(a->bitcast(Type::Integer16, op));
}

Operand
GTE::upper_16(Operand op)
{
  return lower_16(a->shiftr(op, const_u32(16)));
}

u32
GTE::handle_cop_ir(u32 cofun)
{
  assert(a);
  const u8 opcode = cofun & 0x3f;
  const bool lm   = (cofun >> 10) & 1;
  const bool sf   = (cofun >> 19) & 1;

  // mvmva parameters
  const u8 matrix_   = (cofun >> 17) & 3;
  const u8 vector_   = (cofun >> 15) & 3;
  const u8 constant_ = (cofun >> 13) & 3;

  a->call(
    Type::Integer32,
    [](fox::Guest *guest, fox::Value v) {
#if 0
      guest::r3000::R3000 *r3000 = (guest::r3000::R3000 *)guest;
      const u32 pc = r3000->PC();

      const u32 cofun = v.u32_value;
      const u8 opcode = cofun & 0x3f;
      const bool lm = (cofun >> 10) & 1;
      const bool sf = (cofun >> 19) & 1;

      // mvmva parameters
      const u8 matrix_ = (cofun >> 17) & 3;
      const u8 vector_ = (cofun >> 15) & 3;
      const u8 constant_ = (cofun >> 13) & 3;
      printf(
        "gte: opcode 0x%x (lm=%u, sf=%u, matrix=%u, vector=%u, constant=%u) PC=0x%08x\n",
        opcode, lm, sf, matrix_, vector_, constant_, pc);
#endif
      return fox::Value { .u32_value = 0 };
    },
    const_u32(cofun));

  // const u8 fake = (cofun >> 20) & 0x1f;

  switch (opcode) {
    case 0x01:
      rtps<0>(lm, sf);
      return 15;

    case 0x30:
      rtpt(lm, sf);
      return 23;

    case 0x06:
      nclip(lm, sf);
      return 8;

    case 0x0c:
      op(lm, sf);
      return 6;

    case 0x10:
      dpcs(lm, sf);
      return 8;

    case 0x11:
      intpl(lm, sf);
      return 8;

    case 0x12:
      mvmva(lm, sf, matrix_, vector_, constant_);
      return 8;

    case 0x13:
      // NCDS
      nccs<0, true, true>(lm, sf);
      return 19;

    case 0x16:
      // NCDT
      nccs<0, true, true>(lm, sf);
      nccs<1, true, true>(lm, sf);
      nccs<2, true, true>(lm, sf);
      return 44;

    case 0x1b:
      nccs<0, true, false>(lm, sf);
      return 17;

    case 0x1e:
      // NCS
      nccs<0, false, false>(lm, sf);
      return 17;

    case 0x28:
      sqr(sf);
      return 5;

    case 0x29:
      dcpl(lm, sf);
      return 5;

    case 0x2d:
      avsz<3>();
      return 5;

    case 0x2e:
      avsz<4>();
      return 6;

    case 0x3d:
      gpf(lm, sf);
      return 5;

    case 0x3e:
      gpl(lm, sf);
      return 5;

    case 0x3f:
      // NCCT
      nccs<0, true, false>(lm, sf);
      nccs<1, true, false>(lm, sf);
      nccs<2, true, false>(lm, sf);
      return 39;

    default:
      throw std::runtime_error(
        fmt::format("Assembly: Unhandled GTE opcode 0x{:02x}", opcode));
      return 1;
  }
}

//    ___                                          _
//   / __|  ___   _ __    _ __    __ _   _ _    __| |  ___
//  | (__  / _ \ | '  \  | '  \  / _` | | ' \  / _` | (_-<
//   \___| \___/ |_|_|_| |_|_|_| \__,_| |_||_| \__,_| /__/

template<u32 vec_num>
void
GTE::rtps(bool lm, bool sf)
{
  // clang-format off
  // RTPS performs final Rotate, translate and perspective transformation on 
  // vertex V0. Before writing to the FIFOs, the older entries are moved one
  // stage down. RTPT is same as RTPS, but repeats for V1 and V2. The "sf"
  // bit should be usually set.
  
  // IR1 = MAC1 = (TRX*1000h + RT11*VX0 + RT12*VY0 + RT13*VZ0) SAR (sf*12)
  // IR2 = MAC2 = (TRY*1000h + RT21*VX0 + RT22*VY0 + RT23*VZ0) SAR (sf*12)
  // IR3 = MAC3 = (TRZ*1000h + RT31*VX0 + RT32*VY0 + RT33*VZ0) SAR (sf*12)
  // SZ3 = MAC3 SAR ((1-sf)*12)                           ;ScreenZ FIFO 0..+FFFFh
  // MAC0=(((H*20000h/SZ3)+1)/2)*IR1+OFX, SX2=MAC0/10000h ;ScrX FIFO -400h..+3FFh
  // MAC0=(((H*20000h/SZ3)+1)/2)*IR2+OFY, SY2=MAC0/10000h ;ScrY FIFO -400h..+3FFh
  // MAC0=(((H*20000h/SZ3)+1)/2)*DQA+DQB, IR0=MAC0/1000h  ;Depth cueing 0..+1000h

  // If the result of the "(((H*20000h/SZ3)+1)/2)" division is greater than 1FFFFh,
  // then the division result is saturated to +1FFFFh, and the divide overflow bit
  // in the FLAG register gets set; that happens if the vertex is exceeding the
  // "near clip plane", ie. if it is very close to the camera (SZ3<=H/2), exactly
  // at the camara position (SZ3=0), or behind the camera (negative Z coordinates
  // are saturated to SZ3=0). For details on the division, see: GTE Division Inaccuracy

  // For "far plane clipping", one can use the SZ3 saturation flag (MaxZ=FFFFh),
  // or the IR3 saturation flag (MaxZ=7FFFh) (eg. used by Wipeout 2097), or one
  // can compare the SZ3 value with any desired MaxZ value by software.
  // Note: The command does saturate IR1,IR2,IR3 to -8000h..+7FFFh (regardless of
  // lm bit). When using RTP with sf=0, then the IR3 saturation flag (FLAG.22)
  // gets set <only> if "MAC3 SAR 12" exceeds -8000h..+7FFFh (although IR3 is
  // saturated when "MAC3" exceeds -8000h..+7FFFh).
  // clang-format on

  //////////////////////////////////////////////////////////////////////

  // const auto R  = get_matrix(MatrixType::Rotation);
  // const auto Tr = get_translation(); // XXX
  // const auto V  = get_vector<vec_num>();

  // result = M * V + Tr
  // Vector result = calc_mvv(R, V, Tr, lm, sf);

  // SZ3 = MAC3 SAR ((1-sf)*12)
  {
    Operand screen_z = MAC<3>();
    if (!sf) {
      screen_z = a->ashiftr(screen_z, const_u32(12));
    }
    // IR_PRINT_U32("rtps:screen_z", screen_z);
    push_screen_z(screen_z);
  }

  {
    // cop2r56 (cnt24) - OFX - Screen offset X
    // cop2r57 (cnt25) - OFY - Screen offset Y
    // cop2r58 (cnt26) - H   - Projection plane distance
    // cop2r59 (cnt27) - DQA - Depth queing parameter A.(coeff.)
    // cop2r60 (cnt28) - DQB - Depth queing parameter B.(offset.)

    Operand div_result = div_unr();

    // XXX ??? . Depending on
    // div_result = a->ashiftr(div_result, const_u32(3));

    const Operand ofx = load_cop2data(56);
    const Operand fx  = a->add(a->mul(div_result, IR<1>()), ofx);
    // const Operand x   = set_mac<0>(fx);

    const Operand ofy = load_cop2data(57);
    const Operand fy  = a->add(a->mul(div_result, IR<2>()), ofy);
    // const Operand y   = set_mac<0>(fy);

    push_screen_xy(fx, fy);

    ////////
    // Depth (IR0)
    const Operand DQA = load_cop2data(59);
    const Operand DQB = load_cop2data(60);
    Operand depth_cue = a->add(a->mul(div_result, DQA), DQB);

    // XXX
    depth_cue = set_mac<0>(depth_cue);
    depth_cue = a->ashiftr(depth_cue, const_u32(12));
    depth_cue = clamp16(depth_cue, const_u32(0), const_u32(0x1000), 0);
    set_ir<0>(depth_cue, lm);
  }
}

void
GTE::nclip(bool lm, bool sf)
{
  // Register indices for the Screen X fifo SX0..SX2
  const u32 SXY[3]   = { 12, 13, 14 };
  const Operand SXY0 = load_cop2data(SXY[0]);
  const Operand SXY1 = load_cop2data(SXY[1]);
  const Operand SXY2 = load_cop2data(SXY[2]);

  const Operand S0X = lower_16(SXY0);
  const Operand S0Y = upper_16(SXY0);
  const Operand S1X = lower_16(SXY1);
  const Operand S1Y = upper_16(SXY1);
  const Operand S2X = lower_16(SXY2);
  const Operand S2Y = upper_16(SXY2);

  const Operand p0 = a->mul(S0X, S1Y);
  const Operand p1 = a->mul(S1X, S2Y);
  const Operand p2 = a->mul(S2X, S0Y);

  const Operand p3 = a->mul(S0X, S2Y);
  const Operand p4 = a->mul(S1X, S0Y);
  const Operand p5 = a->mul(S2X, S1Y);

  Operand result = a->add(p0, p1);
  result         = a->add(result, p2);
  result         = a->sub(result, p3);
  result         = a->sub(result, p4);
  result         = a->sub(result, p5);
  set_mac<0>(result);
}

void
GTE::op(bool lm, bool sf)
{
  // "Outer Product"
  // [MAC1,MAC2,MAC3] = [IR3*D2-IR2*D3, IR1*D3-IR3*D1, IR2*D1-IR1*D2] SAR (sf*12)
  // [IR1,IR2,IR3]    = [MAC1,MAC2,MAC3]                        ;copy result

  const Operand r32 = load_cop2data(32);
  const Operand r34 = load_cop2data(34);
  const Operand r36 = load_cop2data(36);
  const Operand D1  = lower_16(r32);
  const Operand D2  = lower_16(r34);
  const Operand D3  = lower_16(r36);

  const Operand IR1 = IR<1>();
  const Operand IR2 = IR<2>();
  const Operand IR3 = IR<3>();

  Vector result {
    a->sub(a->mul(IR3, D2), a->mul(IR2, D3)),
    a->sub(a->mul(IR1, D3), a->mul(IR3, D1)),
    a->sub(a->mul(IR2, D1), a->mul(IR1, D2)),
  };

  if (sf) {
    result[0] = a->ashiftr(result[0], const_u32(12));
    result[1] = a->ashiftr(result[1], const_u32(12));
    result[2] = a->ashiftr(result[2], const_u32(12));
  }

  set_mac_ir<1>(result[0], lm, sf);
  set_mac_ir<2>(result[1], lm, sf);
  set_mac_ir<3>(result[2], lm, sf);
}

template<u32 vnum, bool cc_or_cd, bool depth_cue>
void
GTE::nccs(bool lm, bool sf)
{
  // In: V0=Normal vector (for triple variants repeated with V1 and V2), BK=Background
  // color, RGBC=Primary color/code, LLM=Light matrix, LCM=Color matrix, IR0=Interpolation
  // value.

  // [IR1,IR2,IR3] = [MAC1,MAC2,MAC3] = (LLM*V0) SAR (sf*12)
  {
    Vector V   = get_vector<vnum>();
    Matrix LLM = get_matrix(MatrixType::Light);
    Vector result;
    for (u32 i = 0; i < 3; ++i) {
      result[i] = a->mul(LLM[i][0], V[0]);
      result[i] = a->add(result[i], a->mul(LLM[i][1], V[1]));
      result[i] = a->add(result[i], a->mul(LLM[i][2], V[2]));
    }
    if (sf) {
      result[0] = a->ashiftr(result[0], const_u32(12));
      result[1] = a->ashiftr(result[1], const_u32(12));
      result[2] = a->ashiftr(result[2], const_u32(12));
    }
    set_mac_ir<1>(result[0], lm, sf);
    set_mac_ir<2>(result[1], lm, sf);
    set_mac_ir<3>(result[2], lm, sf);
  }

  // [IR1,IR2,IR3] = [MAC1,MAC2,MAC3] = (BK*1000h + LCM*IR) SAR (sf*12)
  {
    Vector V { IR<1>(), IR<2>(), IR<3>() };
    calc_mvv(get_matrix(MatrixType::LightColor), V, BK(), lm, sf);
  }

  // [MAC1,MAC2,MAC3] = [R*IR1,G*IR2,B*IR3] SHL 4          ;<--- for NCDx/NCCx
  if constexpr (cc_or_cd) {
    const Operand RGBC = load_cop2data(6);
    const Operand R    = a->_and(RGBC, const_u32(0xff));
    const Operand G    = a->_and(a->shiftr(RGBC, const_u32(8)), const_u32(0xff));
    const Operand B    = a->_and(a->shiftr(RGBC, const_u32(16)), const_u32(0xff));
    set_mac<1>(a->shiftl(a->mul(R, IR<1>()), const_u32(4)));
    set_mac<2>(a->shiftl(a->mul(G, IR<2>()), const_u32(4)));
    set_mac<3>(a->shiftl(a->mul(B, IR<3>()), const_u32(4)));

    // [MAC1,MAC2,MAC3] = MAC+(FC-MAC)*IR0                   ;<--- for NCDx only
    if constexpr (depth_cue) {

      // more explicitly...
      // [IR1,IR2,IR3] = (([RFC,GFC,BFC] SHL 12) - [MAC1,MAC2,MAC3]) SAR (sf*12)
      // [MAC1,MAC2,MAC3] = (([IR1,IR2,IR3] * IR0) + [MAC1,MAC2,MAC3])

      // cop2r53 (cnt21) - RFC - Far color red component
      // cop2r54 (cnt22) - GFC - Far color green component
      // cop2r55 (cnt23) - BFC - Far color blue component
      const Operand RFC = a->shiftl(load_cop2data(53), const_u32(12));
      const Operand GFC = a->shiftl(load_cop2data(54), const_u32(12));
      const Operand BFC = a->shiftl(load_cop2data(55), const_u32(12));

      Vector sub_result { a->sub(RFC, MAC<1>()),
                          a->sub(GFC, MAC<2>()),
                          a->sub(BFC, MAC<3>()) };
      if (sf) {
        sub_result[0] = a->ashiftr(sub_result[0], const_u32(12));
        sub_result[1] = a->ashiftr(sub_result[1], const_u32(12));
        sub_result[2] = a->ashiftr(sub_result[2], const_u32(12));
      }
      sub_result[0] = set_ir<1>(sub_result[0], lm);
      sub_result[1] = set_ir<2>(sub_result[1], lm);
      sub_result[2] = set_ir<3>(sub_result[2], lm);

      set_mac<1>(a->add(a->mul(IR<1>(), IR<0>()), MAC<1>()));
      set_mac<2>(a->add(a->mul(IR<2>(), IR<0>()), MAC<2>()));
      set_mac<3>(a->add(a->mul(IR<3>(), IR<0>()), MAC<3>()));
    }

    // [MAC1,MAC2,MAC3] = [MAC1,MAC2,MAC3] SAR (sf*12)       ;<--- for NCDx/NCCx
    if (sf) {
      set_mac<1>(a->ashiftr(MAC<1>(), const_u32(12)));
      set_mac<2>(a->ashiftr(MAC<2>(), const_u32(12)));
      set_mac<3>(a->ashiftr(MAC<3>(), const_u32(12)));
    }
  }

  // Color FIFO = [MAC1/16,MAC2/16,MAC3/16,CODE], [IR1,IR2,IR3] = [MAC1,MAC2,MAC3]
  {
    Vector result;
    result[0] = a->ashiftr(MAC<1>(), const_u32(4));
    result[1] = a->ashiftr(MAC<2>(), const_u32(4));
    result[2] = a->ashiftr(MAC<3>(), const_u32(4));
    push_color(result);

    set_ir<1>(MAC<1>(), lm);
    set_ir<2>(MAC<2>(), lm);
    set_ir<3>(MAC<3>(), lm);
  }
}

GTE::Vector
GTE::BK()
{
  // Background Color (BK) (Input?, R/W?)
  // cop2r45 (cnt13) - RBK - Background color red component
  // cop2r46 (cnt14) - GBK - Background color green component
  // cop2r47 (cnt15) - BBK - Background color blue component
  // Each element is 32bit (1bit sign, 19bit integer, 12bit fraction).
  return Vector { load_cop2data(45), load_cop2data(46), load_cop2data(47) };
}

template<u32 n>
void
GTE::avsz()
{
  static_assert(n == 3 || n == 4);
  // MAC0 =  ZSF3*(SZ1+SZ2+SZ3)       ;for AVSZ3
  // MAC0 =  ZSF4*(SZ0+SZ1+SZ2+SZ3)   ;for AVSZ4
  // OTZ  =  MAC0/1000h               ;for both (saturated to 0..FFFFh)
  const u32 SZ[4] = { 16, 17, 18, 19 };

  const Operand sz1   = load_cop2data(SZ[1]);
  const Operand sz2   = load_cop2data(SZ[2]);
  const Operand sz3   = load_cop2data(SZ[3]);
  const Operand sz123 = a->add(sz1, a->add(sz2, sz3));

  Operand result;
  if constexpr (n == 3) {
    // const Operand sz0 = load_cop2data(SZ[0]);
    // sum = a->add(sum, sz0)
    const Operand ZSF3 = load_cop2data(61);
    result             = set_mac<0>(a->mul(sz123, ZSF3));
  } else {
    const Operand sz0    = load_cop2data(SZ[0]);
    const Operand sz0123 = a->add(sz0, sz123);
    const Operand ZSF4   = load_cop2data(62);
    result               = set_mac<0>(a->mul(sz0123, ZSF4));
  }

  // Store to OTZ
  store_cop2data(7, a->ashiftr(result, const_u32(12)));
}

void
GTE::push_color(Vector rgb)
{
  // Color Register and Color FIFO
  // cop2r6  - RGBC  rw|CODE |B    |G    |R    | Color/code
  // cop2r20 - RGB0  rw|CD0  |B0   |G0   |R0   | Characteristic color fifo.
  // cop2r21 - RGB1  rw|CD1  |B1   |G1   |R1   |
  // cop2r22 - RGB2  rw|CD2  |B2   |G2   |R2   |

  store_cop2data(20, load_cop2data(21));
  store_cop2data(21, load_cop2data(22));

  const Operand r =
    clamp16(rgb[0], const_u32(0), const_u32(0xff), FlagBits::COLOR_FIFO_R_SATURATED);
  const Operand g =
    clamp16(rgb[1], const_u32(0), const_u32(0xff), FlagBits::COLOR_FIFO_G_SATURATED);
  const Operand b =
    clamp16(rgb[2], const_u32(0), const_u32(0xff), FlagBits::COLOR_FIFO_B_SATURATED);

  const Operand rgbc = load_cop2data(6);
  const Operand c    = a->_and(rgbc, const_u32(0xff00'0000));

  Operand new_rgbc = r;
  new_rgbc         = a->_or(new_rgbc, a->shiftl(g, const_u32(8)));
  new_rgbc         = a->_or(new_rgbc, a->shiftl(b, const_u32(16)));
  new_rgbc         = a->_or(new_rgbc, c);
  store_cop2data(22, new_rgbc);
}

void
GTE::push_screen_xy(Operand x, Operand y)
{
  // Register indices for the Screen X/Y fifo SX0..SX3
  const u32 SXY[3] = { 12, 13, 14 };
  store_cop2data(SXY[0], load_cop2data(SXY[1]));
  store_cop2data(SXY[1], load_cop2data(SXY[2]));

  // IR_PRINT_U32("push_screen_xy before clip[0]", x);
  // IR_PRINT_U32("push_screen_xy before clip[1]", y);

  x = a->ashiftr(x, const_u32(16));
  y = a->ashiftr(y, const_u32(16));

  const Operand s2x = clamp16(x, const_u32(-0x400), const_u32(0x3ff), SX2_Saturated);
  const Operand s2y = clamp16(y, const_u32(-0x400), const_u32(0x3ff), SY2_Saturated);

  // IR_PRINT_U32("push_screen_xy after clip[0]", s2x);
  // IR_PRINT_U32("push_screen_xy after clip[1]", s2y);

  // 16msb=Y 16lsb=X
  Operand store_val =
    a->_or(a->shiftl(s2y, const_u32(16)), a->_and(s2x, const_u32(0xffff)));

  store_cop2data(SXY[2], store_val);
}

void
GTE::push_screen_z(Operand op)
{
  // Register indices for the Screen Z fifo SZ0..SZ3
  const u32 SZ[4] = { 16, 17, 18, 19 };
  store_cop2data(SZ[0], load_cop2data(SZ[1]));
  store_cop2data(SZ[1], load_cop2data(SZ[2]));
  store_cop2data(SZ[2], load_cop2data(SZ[3]));

  store_cop2data(
    SZ[3], clamp16(op, const_u32(0), const_u32(0xffff), FlagBits::SZ3_Or_OTZ_Saturated));
}

Operand
GTE::div_unr()
{
  // if (H < SZ3*2) then                            ;check if overflow
  //   z = count_leading_zeroes(SZ3)                ;z=0..0Fh (for 16bit SZ3)
  //   n = (H SHL z)                                ;n=0..7FFF8000h
  //   d = (SZ3 SHL z)                              ;d=8000h..FFFFh
  //   u = unr_table[(d-7FC0h) SHR 7] + 101h        ;u=200h..101h
  //   d = ((2000080h - (d * u)) SHR 8)             ;d=10000h..0FF01h
  //   d = ((0000080h + (d * u)) SHR 8)             ;d=20000h..10000h
  //   n = min(1FFFFh, (((n*d) + 8000h) SHR 16))    ;n=0..1FFFFh
  // else n = 1FFFFh, FLAG.Bit17=1, FLAG.Bit31=1    ;n=1FFFFh plus overflow flag

  // TODO : This would be not bad to code in IR, but requires support for lookup tables.
  return a->call(Type::Integer32, [](fox::Guest *guest) {
    const u64 SZ3 =
      guest->guest_register_read(guest::r3000::Registers::COP2_DATA + 19, 4).u32_value;
    const u64 H =
      guest->guest_register_read(guest::r3000::Registers::COP2_DATA + 58, 4).u32_value;

    bool set_flag = false;
    u64 n         = 0xff;

    if (SZ3 == 0) {
      set_flag = true;
    } else {
      n = ((H * 0x20000 / SZ3) + 1) / 2;
    }

    if (n > 0x1ffff || set_flag) {
      u32 flags =
        guest->guest_register_read(guest::r3000::Registers::COP2_DATA + 63, 4).u32_value;
      flags |= 1 << 17;
      flags |= 1 << 31;
      guest->guest_register_write(
        guest::r3000::Registers::COP2_DATA + 63, 4, fox::Value { .u32_value = flags });

      n = 0x1ffff;
    }

    return fox::Value { .u32_value = u32(n) };

#if 0
    // printf("div_unr: SZ3=0x%x H=0x%x\n", SZ3, H);

    static const std::array<u8, 257> unr_table = {
      0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA, 0xE8, 0xE6,
      0xE4, 0xE3, 0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5, 0xD3, 0xD1, 0xD0, 0xCE,
      0xCD, 0xCB, 0xC9, 0xC8, 0xC6, 0xC5, 0xC3, 0xC1, 0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8,
      0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xB0, 0xAE, 0xAD, 0xAB, 0xAA, 0xA9, 0xA7, 0xA6, 0xA4,
      0xA3, 0xA2, 0xA0, 0x9F, 0x9E, 0x9C, 0x9B, 0x9A, 0x99, 0x97, 0x96, 0x95, 0x94, 0x92,
      0x91, 0x90, 0x8F, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x87, 0x86, 0x85, 0x84, 0x83, 0x82,
      0x81, 0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x75, 0x74, 0x73, 0x72,
      0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
      0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B, 0x5A, 0x59, 0x58, 0x57,
      0x56, 0x55, 0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4F, 0x4E, 0x4D, 0x4D, 0x4C, 0x4B,
      0x4A, 0x49, 0x48, 0x48, 0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3F,
      0x3F, 0x3E, 0x3D, 0x3C, 0x3C, 0x3B, 0x3A, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35,
      0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2F, 0x2E, 0x2E, 0x2D, 0x2C, 0x2C, 0x2B,
      0x2A, 0x2A, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24, 0x24, 0x23, 0x22, 0x22,
      0x21, 0x20, 0x20, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1B, 0x1B, 0x1A, 0x19, 0x19,
      0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
      0x10, 0x0F, 0x0F, 0x0E, 0x0E, 0x0D, 0x0D, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x09, 0x09,
      0x08, 0x08, 0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02,
      0x01, 0x01, 0x00, 0x00, 0x00
    };

    if (H < SZ3 * 2) {
      const u32 z = clz<15>(SZ3); // XXX CLZ
      u32 n = H << z;
      u32 d = SZ3 << z;
      const u32 u = u32(unr_table[(d - 0x7Fc0) >> 7]) + 0x101;
      d = ((0x200'0080 - (d * u)) >> 8);
      d = ((0x000'0080 + (d * u)) >> 8);
      n = (((n * d) + 0x8000) >> 16);

      if (n > 0x1'ffff) {
        n = 0x1'ffff;
      }

      return fox::Value { .u32_value = n };
    } else {

      u32 flags =
        guest->guest_register_read(guest::r3000::Registers::COP2_DATA + 63, 4).u32_value;
      flags |= 1 << 17;
      flags |= 1 << 31;
      guest->guest_register_write(
        guest::r3000::Registers::COP2_DATA + 63, 4, fox::Value { .u32_value = flags });

      return fox::Value { .u32_value = 0x1'ffff };
    }
#endif
  });
}

Operand
GTE::clamp16(Operand in, Operand min_val, Operand max_val, u32 overflow_flag_bit)
{
  const Operand lt_min = a->cmp_lt(in, min_val);
  const Operand gt_max = a->cmp_gt(in, max_val);

  set_flag(lt_min, overflow_flag_bit);
  set_flag(gt_max, overflow_flag_bit);

  Operand result = in;
  result         = a->select(lt_min, result, min_val);
  result         = a->select(gt_max, result, max_val);
  return result;
}

float
i16_to_float(u32 in)
{
  const u32 bottom = in & 0xffff;
  return float(bottom) / float(0x1000);
}

GTE::Vector
GTE::calc_mvv(Matrix m, Vector V, Vector Tr, bool lm, bool sf)
{
  // IR_PRINT("calc_mvv\n");
  // IR_PRINT_VECTOR("mvv:V", V);
  // IR_PRINT_MATRIX("mvv:m", m);
  // IR_PRINT_VECTOR("mvv:Tr", Tr);

  // Tr is shifted 12 to have the same fractional bits as R and V
  Vector result;
  for (u32 i = 0; i < 3; ++i) {
    result[i] = a->shiftl(Tr[i], const_u32(12));
    result[i] = a->add(result[i], a->mul(m[i][0], V[0]));
    result[i] = a->add(result[i], a->mul(m[i][1], V[1]));
    result[i] = a->add(result[i], a->mul(m[i][2], V[2]));
  }

  if (sf) {
    result[0] = a->ashiftr(result[0], const_u32(12));
    result[1] = a->ashiftr(result[1], const_u32(12));
    result[2] = a->ashiftr(result[2], const_u32(12));
  }

  // IR_PRINT_VECTOR("mvv:result", result);

  // Set MAC and IR, and their overflow and saturated bits
  set_mac_ir<1>(result[0], lm, sf);
  set_mac_ir<2>(result[1], lm, sf);
  set_mac_ir<3>(result[2], lm, sf);

  return result;
}

void
GTE::rtpt(bool lm, bool sf)
{
  rtps<0>(lm, sf);
  rtps<1>(lm, sf);
  rtps<2>(lm, sf);
}

void
GTE::mvmva(bool lm, bool sf, u32 m_, u32 v_, u32 c_)
{
  const Vector ZERO { const_u32(0), const_u32(0), const_u32(0) };

  // Mx = matrix specified by mx  ;RT/LLM/LCM - Rotation, light or color matrix
  // Vx = vector specified by v   ;V0, V1, V2, or [IR1,IR2,IR3]
  // Tx = translation vector specified by cv  ;TR or BK or Bugged/FC, or None

  Matrix m = get_matrix(m_ == 0 ? MatrixType::Rotation
                                : (m_ == 1 ? MatrixType::Light : MatrixType::LightColor));

  Vector v;
  if (v_ == 0) {
    v = get_vector<0>();
  } else if (v_ == 1) {
    v = get_vector<1>();
  } else {
    v = get_vector<2>();
  }

  Vector c;
  if (c_ == 0) {
    c = get_translation();
  } else if (c_ == 1) {
    c = Vector {
      load_cop2data(45),
      load_cop2data(46),
      load_cop2data(47),
    };
  } else if (c_ == 2) {
    // xxx
    c = ZERO;
  } else {
    c = ZERO;
  }

  calc_mvv(m, v, c, lm, sf);
}

void
GTE::sqr(bool sf)
{
  // [MAC1,MAC2,MAC3] = [IR1*IR1,IR2*IR2,IR3*IR3] SHR (sf*12)
  // [IR1,IR2,IR3]    = [MAC1,MAC2,MAC3]    ;IR1,IR2,IR3 saturated to max 7FFFh

  const Operand IR1 = IR<1>();
  const Operand IR2 = IR<2>();
  const Operand IR3 = IR<3>();

  Vector result { a->mul(IR1, IR1), a->mul(IR2, IR2), a->mul(IR3, IR3) };
  if (sf) {
    result[0] = a->shiftr(result[0], const_u32(12));
    result[1] = a->shiftr(result[1], const_u32(12));
    result[2] = a->shiftr(result[2], const_u32(12));
  }

  set_mac_ir<1>(result[0], 1, sf);
  set_mac_ir<2>(result[1], 1, sf);
  set_mac_ir<3>(result[2], 1, sf);
}

void
GTE::dpcs(bool lm, bool sf)
{
  // [MAC1,MAC2,MAC3] = [R,G,B] SHL 16                     ;<--- for DPCS/DPCT
  const Operand RGBC = load_cop2data(6);
  const Operand R    = a->_and(RGBC, const_u32(0xff));
  const Operand G    = a->_and(a->shiftr(RGBC, const_u32(8)), const_u32(0xff));
  const Operand B    = a->_and(a->shiftr(RGBC, const_u32(16)), const_u32(0xff));
  set_mac<1>(a->shiftl(R, const_u32(16)));
  set_mac<2>(a->shiftl(G, const_u32(16)));
  set_mac<3>(a->shiftl(B, const_u32(16)));

  depth_cue_shared(lm, sf);
}

void
GTE::intpl(bool lm, bool sf)
{
  // [MAC1,MAC2,MAC3] = [IR1,IR2,IR3] SHL 12               ;<--- for INTPL only
  set_mac<1>(a->shiftl(IR<1>(), const_u32(12)));
  set_mac<2>(a->shiftl(IR<2>(), const_u32(12)));
  set_mac<3>(a->shiftl(IR<3>(), const_u32(12)));

  depth_cue_shared(lm, sf);
}

void
GTE::dcpl(bool lm, bool sf)
{
  // [MAC1,MAC2,MAC3] = [R*IR1,G*IR2,B*IR3] SHL 4          ;<--- for DCPL only
  const Operand RGBC = load_cop2data(6);
  const Operand R    = a->_and(RGBC, const_u32(0xff));
  const Operand G    = a->_and(a->shiftr(RGBC, const_u32(8)), const_u32(0xff));
  const Operand B    = a->_and(a->shiftr(RGBC, const_u32(16)), const_u32(0xff));

  set_mac<1>(a->shiftl(a->mul(R, IR<1>()), const_u32(4)));
  set_mac<2>(a->shiftl(a->mul(G, IR<2>()), const_u32(4)));
  set_mac<3>(a->shiftl(a->mul(B, IR<3>()), const_u32(4)));

  depth_cue_shared(lm, sf);
}

void
GTE::depth_cue_shared(bool lm, bool sf)
{
  // [MAC1,MAC2,MAC3] = MAC+(FC-MAC)*IR0
  {
    // more explicitly...
    // [IR1,IR2,IR3] = (([RFC,GFC,BFC] SHL 12) - [MAC1,MAC2,MAC3]) SAR (sf*12)
    // [MAC1,MAC2,MAC3] = (([IR1,IR2,IR3] * IR0) + [MAC1,MAC2,MAC3])

    // cop2r53 (cnt21) - RFC - Far color red component
    // cop2r54 (cnt22) - GFC - Far color green component
    // cop2r55 (cnt23) - BFC - Far color blue component
    const Operand RFC = a->shiftl(load_cop2data(53), const_u32(12));
    const Operand GFC = a->shiftl(load_cop2data(54), const_u32(12));
    const Operand BFC = a->shiftl(load_cop2data(55), const_u32(12));

    Vector M { MAC<1>(), MAC<2>(), MAC<3>() };
    Vector sub_result { a->sub(RFC, M[0]), a->sub(GFC, M[1]), a->sub(BFC, M[2]) };
    if (sf) {
      set_ir<1>(a->ashiftr(sub_result[0], const_u32(12)), lm);
      set_ir<2>(a->ashiftr(sub_result[1], const_u32(12)), lm);
      set_ir<3>(a->ashiftr(sub_result[2], const_u32(12)), lm);
    } else {
      set_ir<1>(sub_result[0], lm);
      set_ir<2>(sub_result[1], lm);
      set_ir<3>(sub_result[2], lm);
    }
    set_mac<1>(a->add(a->mul(IR<1>(), IR<0>()), MAC<1>()));
    set_mac<2>(a->add(a->mul(IR<2>(), IR<0>()), MAC<2>()));
    set_mac<3>(a->add(a->mul(IR<3>(), IR<0>()), MAC<3>()));
  }

  // [MAC1,MAC2,MAC3] = [MAC1,MAC2,MAC3] SAR (sf*12)
  if (sf) {
    set_mac<1>(a->ashiftr(MAC<1>(), const_u32(12)));
    set_mac<2>(a->ashiftr(MAC<2>(), const_u32(12)));
    set_mac<3>(a->ashiftr(MAC<3>(), const_u32(12)));
  }

  // Color FIFO = [MAC1/16,MAC2/16,MAC3/16,CODE], [IR1,IR2,IR3] = [MAC1,MAC2,MAC3]
  {
    push_color(Vector {
      a->ashiftr(MAC<1>(), const_u32(4)),
      a->ashiftr(MAC<2>(), const_u32(4)),
      a->ashiftr(MAC<3>(), const_u32(4)),
    });

    set_ir<1>(MAC<1>(), lm);
    set_ir<2>(MAC<2>(), lm);
    set_ir<3>(MAC<3>(), lm);
  }
}

void
GTE::gpf(bool lm, bool sf)
{
  // [MAC1,MAC2,MAC3] = (([IR1,IR2,IR3] * IR0)) SAR (sf*12)
  set_mac<1>(a->mul(IR<1>(), IR<0>()));
  set_mac<2>(a->mul(IR<2>(), IR<0>()));
  set_mac<3>(a->mul(IR<3>(), IR<0>()));

  if (sf) {
    set_mac<1>(a->ashiftr(MAC<1>(), const_u32(12)));
    set_mac<2>(a->ashiftr(MAC<2>(), const_u32(12)));
    set_mac<3>(a->ashiftr(MAC<3>(), const_u32(12)));
  }

  // Color FIFO = [MAC1/16,MAC2/16,MAC3/16,CODE], [IR1,IR2,IR3] = [MAC1,MAC2,MAC3]
  push_color(Vector {
    a->ashiftr(MAC<1>(), const_u32(4)),
    a->ashiftr(MAC<2>(), const_u32(4)),
    a->ashiftr(MAC<3>(), const_u32(4)),
  });

  set_ir<1>(MAC<1>(), lm);
  set_ir<2>(MAC<2>(), lm);
  set_ir<3>(MAC<3>(), lm);
}

void
GTE::gpl(bool lm, bool sf)
{
  // [MAC1,MAC2,MAC3] = [MAC1,MAC2,MAC3] SHL (sf*12)       ;<--- for GPL only
  // [MAC1,MAC2,MAC3] = (([IR1,IR2,IR3] * IR0)) SAR (sf*12)
  // Color FIFO = [MAC1/16,MAC2/16,MAC3/16,CODE], [IR1,IR2,IR3] = [MAC1,MAC2,MAC3]

  if (sf) {
    set_mac<1>(a->shiftl(MAC<1>(), const_u32(12)));
    set_mac<2>(a->shiftl(MAC<2>(), const_u32(12)));
    set_mac<3>(a->shiftl(MAC<3>(), const_u32(12)));
  }

  set_mac<1>(a->add(a->mul(IR<1>(), IR<0>()), MAC<1>()));
  set_mac<2>(a->add(a->mul(IR<2>(), IR<0>()), MAC<2>()));
  set_mac<3>(a->add(a->mul(IR<3>(), IR<0>()), MAC<3>()));

  if (sf) {
    set_mac<1>(a->ashiftr(MAC<1>(), const_u32(12)));
    set_mac<2>(a->ashiftr(MAC<2>(), const_u32(12)));
    set_mac<3>(a->ashiftr(MAC<3>(), const_u32(12)));
  }

  // Color FIFO = [MAC1/16,MAC2/16,MAC3/16,CODE], [IR1,IR2,IR3] = [MAC1,MAC2,MAC3]
  push_color(Vector {
    a->ashiftr(MAC<1>(), const_u32(4)),
    a->ashiftr(MAC<2>(), const_u32(4)),
    a->ashiftr(MAC<3>(), const_u32(4)),
  });

  set_ir<1>(MAC<1>(), lm);
  set_ir<2>(MAC<2>(), lm);
  set_ir<3>(MAC<3>(), lm);
}

//   _  _         _
//  | || |  ___  | |  _ __   ___   _ _   ___
//  | __ | / -_) | | | '_ \ / -_) | '_| (_-<
//  |_||_| \___| |_| | .__/ \___| |_|   /__/
//                   |_|

// Vector 0 (V0)         Vector 1 (V1)       Vector 2 (V2)       Vector 3 (IR)
// cop2r0.lsbs - VX0     cop2r2.lsbs - VX1   cop2r4.lsbs - VX2   cop2r9  - IR1
// cop2r0.msbs - VY0     cop2r2.msbs - VY1   cop2r4.msbs - VY2   cop2r10 - IR2
// cop2r1      - VZ0     cop2r3      - VZ1   cop2r5      - VZ2   cop2r11 - IR3

template<u32 num>
void
GTE::set_mac_ir(Operand in32, bool lm, bool sf)
{
  set_mac<num>(in32);
  set_ir<num>(lower_16(in32), lm);
}

template<u32 i>
Operand
GTE::set_mac(Operand in64)
{
  // XXX : set overflow etc. flags
  const Operand in32 = a->bitcast(Type::Integer32, in64);
  store_cop2data(24 + i, in32);
  return in32;
}

template<u32 i>
Operand
GTE::set_ir(Operand op, bool lm)
{
  //   10     lm - Saturate IR1,IR2,IR3 result (0=To -8000h..+7FFFh, 1=To 0..+7FFFh)

  const Operand min_value = lm ? const_u32(0) : const_u32(-0x8000);
  const Operand max_value = const_u32(0x7fff);

  const u32 saturation_flags[4] = { FlagBits::IR0_Saturated,
                                    FlagBits::IR1_Saturated,
                                    FlagBits::IR2_Saturated,
                                    FlagBits::IR3_Saturated };

  const Operand clamped = clamp16(op, min_value, max_value, saturation_flags[i]);
  store_cop2data(8 + i, clamped);
  return clamped;
}

void
GTE::set_flag(Operand condition, u32 overflow_flag_bit)
{
  u32 new_flag_bits = 1 << overflow_flag_bit;

  // Set the general error bit if these are added
  if (overflow_flag_bit >= 20 && overflow_flag_bit <= 30)
    new_flag_bits |= 1 << 31;
  if (overflow_flag_bit >= 13 && overflow_flag_bit <= 18)
    new_flag_bits |= 1 << 31;

  Operand flags = load_cop2data(63);
  flags         = a->select(condition, flags, a->_or(flags, const_u32(new_flag_bits)));
  store_cop2data(63, flags);
}

template<u32 i>
Operand
GTE::MAC()
{
  assert(i < 4);
  // cop2r24    1xS32 MAC0                  32bit Maths Accumulators (Value)
  // cop2r25-27 3xS32 MAC1,MAC2,MAC3        32bit Maths Accumulators (Vector)
  return load_cop2data(24 + i);
}

template<u32 i>
Operand
GTE::IR()
{
  assert(i < 4);
  // cop2r8     1xS16 IR0                   16bit Accumulator (Interpolate)
  // cop2r9-11  3xS16 IR1,IR2,IR3           16bit Accumulator (Vector)
  Operand raw = load_cop2data(8 + i);
  return lower_16(raw);
}

GTE::Matrix
GTE::get_matrix(MatrixType matrix_type)
{
  if (matrix_type == MatrixType::Rotation) {
    const Operand r32 = load_cop2data(32);
    const Operand r33 = load_cop2data(33);
    const Operand r34 = load_cop2data(34);
    const Operand r35 = load_cop2data(35);
    const Operand r36 = load_cop2data(36);

    Matrix m;
    m[0][0] = lower_16(r32);
    m[0][1] = upper_16(r32);
    m[0][2] = lower_16(r33);

    m[1][0] = upper_16(r33);
    m[1][1] = lower_16(r34);
    m[1][2] = upper_16(r34);

    m[2][0] = lower_16(r35);
    m[2][1] = upper_16(r35);
    m[2][2] = lower_16(r36);
    return m;
  } else if (matrix_type == MatrixType::Light) {
    const Operand r40 = load_cop2data(40);
    const Operand r41 = load_cop2data(41);
    const Operand r42 = load_cop2data(42);
    const Operand r43 = load_cop2data(43);
    const Operand r44 = load_cop2data(44);

    Matrix m;
    m[0][0] = lower_16(r40);
    m[0][1] = upper_16(r40);
    m[0][2] = lower_16(r41);

    m[1][0] = upper_16(r41);
    m[1][1] = lower_16(r42);
    m[1][2] = upper_16(r42);

    m[2][0] = lower_16(r43);
    m[2][1] = upper_16(r43);
    m[2][2] = lower_16(r44);
    return m;
  } else if (matrix_type == MatrixType::LightColor) {
    const Operand r48 = load_cop2data(48);
    const Operand r49 = load_cop2data(49);
    const Operand r50 = load_cop2data(50);
    const Operand r51 = load_cop2data(51);
    const Operand r52 = load_cop2data(52);

    Matrix m;
    m[0][0] = lower_16(r48);
    m[0][1] = upper_16(r48);
    m[0][2] = lower_16(r49);

    m[1][0] = upper_16(r49);
    m[1][1] = lower_16(r50);
    m[1][2] = upper_16(r50);

    m[2][0] = lower_16(r51);
    m[2][1] = upper_16(r51);
    m[2][2] = lower_16(r52);
    return m;
  }

  else {
    assert(false);
    throw std::runtime_error("GTE: Unhandled matrix type");
  }
}

template<u32 vector_num>
GTE::Vector
GTE::get_vector()
{
  assert(vector_num < 3);
  const u32 index = (2 * vector_num);

  const Operand reg_xy = load_cop2data(index);
  const Operand reg_z  = load_cop2data(index + 1);

  Vector v;
  v[0] = lower_16(reg_xy);
  v[1] = upper_16(reg_xy);
  v[2] = lower_16(reg_z);
  return v;
}

template<u32 vector_num>
void
GTE::put_vector(Vector v)
{
  assert(false);
}

/////////////////////////////////

Operand
GTE::load_cop2data(u32 index)
{
  const Operand reg_index = const_u16(guest::r3000::Registers::COP2_DATA + index);
  return a->readgr(Type::Integer32, reg_index);
}

void
GTE::store_cop2data(u32 index, Operand val)
{
  const Operand reg_index = const_u16(guest::r3000::Registers::COP2_DATA + index);
  a->writegr(reg_index, val);
}

Operand
GTE::load_cop2ctrl(u32 index)
{
  const Operand reg_index = const_u16(guest::r3000::Registers::COP2_CTRL + index);
  return a->readgr(Type::Integer32, reg_index);
}

void
GTE::store_cop2ctrl(u32 index, Operand val)
{
  const Operand reg_index = const_u16(guest::r3000::Registers::COP2_CTRL + index);
  a->writegr(reg_index, val);
}

GTE::Vector
GTE::get_translation()
{
  Vector v;
  v[0] = load_cop2data(37);
  v[1] = load_cop2data(38);
  v[2] = load_cop2data(39);
  return v;
}

// void
// GTE::put_translation(u32 component, Operand val)
// {
//   store_cop2data(37 + component, val);
// }

}
