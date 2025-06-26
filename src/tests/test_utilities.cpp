#include <gtest/gtest.h>

#include "shared/bitmanip.h"

TEST(Bitmanip, bit_mask)
{
  ASSERT_EQ(bit_mask(0, 0), 0b1u);
  ASSERT_EQ(bit_mask(1, 0), 0b11u);
  ASSERT_EQ(bit_mask(31, 0), 0xFFFFFFFFu);
  ASSERT_EQ(bit_mask(7, 4), 0xF0u);
}

int
main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
