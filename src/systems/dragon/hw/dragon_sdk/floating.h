#pragma once

#include <string>
#include <algorithm>
#include <limits>
#include <cassert>
#include <cstdint>
#include <cmath>

#ifndef __FLOATING_H
#define __FLOATING_H

/*!
 * @brief Breakout structure for the fields that make up a standard 32-bit
 *        float.
 */
struct FloatBits {
    static constexpr int32_t ExponentBias = 127;
    static constexpr int32_t MantissaBits = 23;

    union {
        float raw;
        struct {
            uint32_t mantissa : 23;
            uint32_t exponent : 8;
            uint32_t sign     : 1;
        };
    };

    FloatBits()
    {
        return;
    }

    FloatBits(const float x) : raw(x)
    {
        return;
    }
};

/*!
 * @brief Generic implementation of a simplified floating point format. Not
 *        IEEE-754 compatible.
 *
 * To simplify the hardware implementation of this floating point type, the
 * following restrictions are added versus standard floats:
 *
 *  - There is no representation for infinity or NaN
 *  - Sub-normals are not supported (rounded to zero)
 *  - Division is not supported
 *
 * E - Exponent bits
 * M - Mantissa bits
 * Storage - Internal storage type (must be at least E + M + 1 bits)
 *
 * TODO Unfinished
 */
template<unsigned E, unsigned M, typename Storage = uint32_t>
class Floating {
  public: /* Types */
    typedef Floating<E, M, Storage> Self;

  public: /* Constants */
    static constexpr unsigned ExponentBits = E;
    static constexpr unsigned MantissaBits = M;
    static constexpr unsigned TotalBits = E + M + 1; /* With sign bit */
    static constexpr Storage TotalMask = (1lu << TotalBits) - 1;
    static constexpr Storage SignMask = 1lu << (E + M);
    static constexpr Storage ExponentMask = ((1lu << E) - 1lu) << M;
    static constexpr Storage MantissaMask = ((1lu << M) - 1lu);
    static constexpr int32_t ExponentBias = 1lu << (E - 1);
    static constexpr Storage MantissaMax = (1lu << M) - 1lu;
    static constexpr Storage ExponentMax = (1lu << E) - 1lu;

  public: /* API */
    Floating()
    {
        /* Left uninitialized. */
        return;
    }

    /* From bit representation */
    typedef struct { } from_bits_t;
    static constexpr from_bits_t from_bits = from_bits_t();

    Floating(from_bits_t, const Storage value) : m_data(value)
    {
        return;
    }

    Floating(const float x)
    {
        const FloatBits bits(x);
        if (bits.exponent == 255) {
            if (bits.mantissa == 0) {
                /* Infinity, converted to maximum supported value. */
                m_data = (bits.sign ? SignMask : 0) | ExponentMask | MantissaMask;
            } else {
                /* NaN */
                assert("Cannot convert NaN to Floating<>" && false);
            }
            return;
        }

        const int32_t unbiased_exponent =
          int32_t(bits.exponent) - FloatBits::ExponentBias;
        if (unbiased_exponent < -ExponentBias) {
            /* Zero, denormal, or below supported range. Convert to +/- 0. */
            m_data = bits.sign ? SignMask : 0;
        } else if (unbiased_exponent >= ExponentBias) {
            /* Value larger than supported range, converted to maximum supported value. */
            m_data = (bits.sign ? SignMask : 0) | ExponentMask | MantissaMask;
        } else {
            /* Normal floating point value. */
            const Storage biased_exponent = unbiased_exponent + ExponentBias;
            Storage mantissa;
            if constexpr (MantissaBits < FloatBits::MantissaBits) {
                mantissa = bits.mantissa >> (FloatBits::MantissaBits - MantissaBits);
            } else {
                mantissa = bits.mantissa << (MantissaBits - FloatBits::MantissaBits);
            }

            m_data = (bits.sign ? SignMask : 0) | (biased_exponent << MantissaBits) |
                     (mantissa & MantissaMask);

            assert(biased_exponent < (1lu << E));
        }
    }

    /*
     * Note: Exponent must be in biased (unsigned) format.
     */
    explicit Floating(const bool negative, const Storage exponent, const Storage mantissa)
      : m_data((negative ? SignMask : 0) | ((exponent << MantissaBits) & ExponentMask) |
               (mantissa & MantissaMask))
    {
        return;
    }

    float to_float() const
    {
        return float(mantissa() * exp2f(unbiased_exponent() - int32_t(MantissaBits))) *
               (is_negative() ? -1.0f : 1.0f);
    }

    bool is_zero() const
    {
        return ((m_data & ExponentMask) >> MantissaBits) == 0;
    }

    bool is_negative() const
    {
        return (m_data & SignMask) ? true : false;
    }

    uint32_t biased_exponent() const
    {
        return int32_t((m_data & ExponentMask) >> MantissaBits);
    }

    /*!
     * @brief Returns the unbiased exponent as a signed value.
     */
    int32_t unbiased_exponent() const
    {
        return int32_t((m_data & ExponentMask) >> MantissaBits) - ExponentBias;
    }

    /*!
     * @brief Returns the significand (mantissa bits including implicit leading
     *        1 bit).
     */
    Storage mantissa() const
    {
        return is_zero() ? 0 : ((m_data & MantissaMask) | (1lu << MantissaBits));
    }

    Storage raw() const
    {
        return m_data;
    }

    Self operator-() const
    {
        return Self(!is_negative(), biased_exponent(), mantissa());
    }

    Self operator*(const Self &other) const
    {
        const bool result_negative = is_negative() ^ other.is_negative();
        Storage new_mantissa =
          uint64_t(mantissa()) * uint64_t(other.mantissa()) >> MantissaBits;
        int new_exponent =
          unbiased_exponent() + other.unbiased_exponent() + Self::ExponentBias;
        if (new_mantissa & (1lu << (MantissaBits + 1u))) {
            new_mantissa = new_mantissa >> 1;
            new_exponent += 1;
        }

        if (new_exponent < 0) {
            /* Underflow */
            return Self(result_negative, 0, 0);
        } else if (new_exponent > ExponentMax) {
            /* Overflow */
            return Self(result_negative, ExponentMax, MantissaMax);
        } else {
            return Self(result_negative, new_exponent, new_mantissa);
        }
    }

    Self operator+(const Self &other) const
    {
        /* Ensure 'a' has the larger exponent if not equal. */
        Self a = *this, b = other;
        if (a.biased_exponent() < b.biased_exponent()) {
            std::swap(a, b);
        }

        const unsigned delta_e = a.biased_exponent() - b.biased_exponent();
        if (delta_e > MantissaBits) {
            /* b is less than one ULP of a, result is not affected by b. */
            return a;
        }

        /* Use shifted and sign-extended versions of input mantissas that
         * align bits of the same magnitude from 'a' and 'b' for addition. */
        const int64_t normalized_a =
            int64_t(a.mantissa() << (MantissaBits + 1)) * (a.is_negative() ? -1 : 1);
        const int64_t normalized_b =
            int64_t(b.mantissa() << (MantissaBits + 1 - delta_e)) * (b.is_negative() ? -1 : 1);
        const int64_t add_intermediate = normalized_a + normalized_b;
        const bool result_negative = add_intermediate < 0;
        if (add_intermediate == 0) {
            return Self(0.0f);
        }

        /* Check for overflow / underflow. This can only happen with inputs of
         * the same sign. */
        if (a.is_negative() && b.is_negative() && !result_negative) {
            return Self(true, ExponentMax, MantissaMax);
        } else if (!a.is_negative() && !b.is_negative() && result_negative) {
            return Self(false, ExponentMax, MantissaMax);
        }

        /* The result has the exponent of 'a' either plus 1, plus 0, or minus
         * up to MantissaBits. */
        const uint64_t add_intermediate_abs =
            add_intermediate < 0 ? -add_intermediate : add_intermediate;
        const int result_e_shift =
            __builtin_clzl(add_intermediate_abs) - (62 - MantissaBits * 2);
        assert(result_e_shift >= -1);

        if (result_e_shift == -1) {
            /* Exponent increased by 1. */
            if (a.biased_exponent() == ExponentMax) {
                return Self(false, ExponentMax, MantissaMax);
            }

            return Self(result_negative, a.biased_exponent() + 1,
                        add_intermediate_abs >> (MantissaBits + 2));
        } else if (result_e_shift == 0) {
            /* Exponent did not change. */
            return Self(result_negative, a.biased_exponent(),
                        add_intermediate_abs >> (MantissaBits + 1));
        } else {
            /* Exponent is reduced. */
            if (result_e_shift > a.biased_exponent()) {
                return Self(result_negative, 0, 0);
            }

            return Self(result_negative, a.biased_exponent() - result_e_shift,
                        add_intermediate_abs >> (MantissaBits + 1 - result_e_shift));
        }
    }

    Self operator-(const Self &other) const
    {
        return *this + -other;
    }

    bool operator==(const Self &other) const
    {
        return (is_zero() && other.is_zero()) || m_data == other.m_data;
    }

    bool operator!=(const Self &other) const
    {
        return (is_zero() ^ other.is_zero()) || m_data != other.m_data;
    }

    Self &operator+=(const Self &other)
    {
        *this = (*this) + other;
        return *this;
    }

    Self &operator-=(const Self &other)
    {
        *this = (*this) - other;
        return *this;
    }

    Self &operator*=(const Self &other)
    {
        *this = (*this) * other;
        return *this;
    }

    bool operator<(const Self& other)
    {
        return _sign_extend_raw() < other._sign_extend_raw();
    }

    bool operator<=(const Self& other)
    {
        return !(_sign_extend_raw() > other._sign_extend_raw());
    }

    bool operator>(const Self& other)
    {
        return _sign_extend_raw() > other._sign_extend_raw();
    }

    bool operator>=(const Self& other)
    {
        return !(_sign_extend_raw() < other._sign_extend_raw());
    }

    std::string to_string() const
    {
        std::string mantissa_string = is_zero() ? "0." : "1.";
        for (unsigned i = 0; i < MantissaBits; ++i) {
            mantissa_string += (m_data & (1lu << (MantissaBits - i - 1))) ? "1" : "0";
        }

        return std::string(is_negative() ? "-" : "") +
               mantissa_string +
               "e" + std::to_string(unbiased_exponent());
    }

    static Self min_value()
    {
        return Self(true, ExponentMax, MantissaMax);
    }

    static Self max_value()
    {
        return Self(false, ExponentMax, MantissaMax);
    }

private: /* Data */
    Storage m_data;

    /*!
     * @brief Internal helper to build a signed extended version of the
     *        raw bits stored, when reinterpreted as an integer. Zero
     *        is normalized to all-0 regardless of sign.
     */
    Storage _sign_extend_raw() const
    {
        if (is_zero()) {
            return 0;
        }

        return is_negative() ? m_data | (Storage(-1) & ~TotalMask) : m_data;
    }
};

typedef Floating<5, 10, uint16_t> Float16;
typedef Floating<5, 12, uint32_t> Float18;

#endif
