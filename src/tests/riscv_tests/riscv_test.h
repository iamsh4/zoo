
#define TESTNUM gp

#define RVTEST_CODE_BEGIN \
  li gp, 0

#define RVTEST_PASS .word 0xCAFECAFE
#define RVTEST_FAIL .word 0xBADBAD00

#define RVTEST_RV32U 
#define RVTEST_RV64U  RVTEST_RV32U
#define RVTEST_CODE_END ;

#define RVTEST_DATA_BEGIN ;
#define RVTEST_DATA_END ;
