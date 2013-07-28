#ifndef ISA_XML
#define ISA_XML

/* Autogenerated file, DO NOT EDIT manually!

This file was generated by the rules-ng-ng headergen tool in this git repository:
http://0x04.net/cgit/index.cgi/rules-ng-ng
git clone git://0x04.net/rules-ng-ng

The rules-ng-ng source files this header was generated from are:
- /home/orion/projects/etna_viv/rnndb/isa.xml (  18824 bytes, from 2013-07-20 13:11:45)

Copyright (C) 2013
*/


#define INST_OPCODE_NOP						0x00000000
#define INST_OPCODE_ADD						0x00000001
#define INST_OPCODE_MAD						0x00000002
#define INST_OPCODE_MUL						0x00000003
#define INST_OPCODE_DST						0x00000004
#define INST_OPCODE_DP3						0x00000005
#define INST_OPCODE_DP4						0x00000006
#define INST_OPCODE_DSX						0x00000007
#define INST_OPCODE_DSY						0x00000008
#define INST_OPCODE_MOV						0x00000009
#define INST_OPCODE_MOVAR					0x0000000a
#define INST_OPCODE_MOVAF					0x0000000b
#define INST_OPCODE_RCP						0x0000000c
#define INST_OPCODE_RSQ						0x0000000d
#define INST_OPCODE_LITP					0x0000000e
#define INST_OPCODE_SELECT					0x0000000f
#define INST_OPCODE_SET						0x00000010
#define INST_OPCODE_EXP						0x00000011
#define INST_OPCODE_LOG						0x00000012
#define INST_OPCODE_FRC						0x00000013
#define INST_OPCODE_CALL					0x00000014
#define INST_OPCODE_RET						0x00000015
#define INST_OPCODE_BRANCH					0x00000016
#define INST_OPCODE_TEXKILL					0x00000017
#define INST_OPCODE_TEXLD					0x00000018
#define INST_OPCODE_TEXLDB					0x00000019
#define INST_OPCODE_TEXLDD					0x0000001a
#define INST_OPCODE_TEXLDL					0x0000001b
#define INST_OPCODE_TEXLDPCF					0x0000001c
#define INST_OPCODE_REP						0x0000001d
#define INST_OPCODE_ENDREP					0x0000001e
#define INST_OPCODE_LOOP					0x0000001f
#define INST_OPCODE_ENDLOOP					0x00000020
#define INST_OPCODE_SQRT					0x00000021
#define INST_OPCODE_SIN						0x00000022
#define INST_OPCODE_COS						0x00000023
#define INST_OPCODE_FLOOR					0x00000025
#define INST_OPCODE_CEIL					0x00000026
#define INST_OPCODE_SIGN					0x00000027
#define INST_OPCODE_I2F						0x0000002d
#define INST_OPCODE_CMP						0x00000031
#define INST_OPCODE_LOAD					0x00000032
#define INST_OPCODE_STORE					0x00000033
#define INST_OPCODE_IMULLO0					0x0000003c
#define INST_OPCODE_IMULHI0					0x00000040
#define INST_OPCODE_LEADZERO					0x00000058
#define INST_OPCODE_LSHIFT					0x00000059
#define INST_OPCODE_RSHIFT					0x0000005a
#define INST_OPCODE_ROTATE					0x0000005b
#define INST_OPCODE_OR						0x0000005c
#define INST_OPCODE_AND						0x0000005d
#define INST_OPCODE_XOR						0x0000005e
#define INST_OPCODE_NOT						0x0000005f
#define INST_CONDITION_TRUE					0x00000000
#define INST_CONDITION_GT					0x00000001
#define INST_CONDITION_LT					0x00000002
#define INST_CONDITION_GE					0x00000003
#define INST_CONDITION_LE					0x00000004
#define INST_CONDITION_EQ					0x00000005
#define INST_CONDITION_NE					0x00000006
#define INST_CONDITION_AND					0x00000007
#define INST_CONDITION_OR					0x00000008
#define INST_CONDITION_XOR					0x00000009
#define INST_CONDITION_NOT					0x0000000a
#define INST_CONDITION_NZ					0x0000000b
#define INST_CONDITION_GEZ					0x0000000c
#define INST_CONDITION_GZ					0x0000000d
#define INST_CONDITION_LEZ					0x0000000e
#define INST_CONDITION_LZ					0x0000000f
#define INST_RGROUP_TEMP					0x00000000
#define INST_RGROUP_INTERNAL					0x00000001
#define INST_RGROUP_UNIFORM_0					0x00000002
#define INST_RGROUP_UNIFORM_1					0x00000003
#define INST_AMODE_DIRECT					0x00000000
#define INST_AMODE_ADD_A_X					0x00000001
#define INST_AMODE_ADD_A_Y					0x00000002
#define INST_AMODE_ADD_A_Z					0x00000003
#define INST_AMODE_ADD_A_W					0x00000004
#define INST_SWIZ_COMP_X					0x00000000
#define INST_SWIZ_COMP_Y					0x00000001
#define INST_SWIZ_COMP_Z					0x00000002
#define INST_SWIZ_COMP_W					0x00000003
#define INST_COMPS_X						0x00000001
#define INST_COMPS_Y						0x00000002
#define INST_COMPS_Z						0x00000004
#define INST_COMPS_W						0x00000008
#define INST_SWIZ_X__MASK					0x00000003
#define INST_SWIZ_X__SHIFT					0
#define INST_SWIZ_X(x)						(((x) << INST_SWIZ_X__SHIFT) & INST_SWIZ_X__MASK)
#define INST_SWIZ_Y__MASK					0x0000000c
#define INST_SWIZ_Y__SHIFT					2
#define INST_SWIZ_Y(x)						(((x) << INST_SWIZ_Y__SHIFT) & INST_SWIZ_Y__MASK)
#define INST_SWIZ_Z__MASK					0x00000030
#define INST_SWIZ_Z__SHIFT					4
#define INST_SWIZ_Z(x)						(((x) << INST_SWIZ_Z__SHIFT) & INST_SWIZ_Z__MASK)
#define INST_SWIZ_W__MASK					0x000000c0
#define INST_SWIZ_W__SHIFT					6
#define INST_SWIZ_W(x)						(((x) << INST_SWIZ_W__SHIFT) & INST_SWIZ_W__MASK)
#define VIV_ISA_WORD_0						0x00000000
#define VIV_ISA_WORD_0_OPCODE__MASK				0x0000003f
#define VIV_ISA_WORD_0_OPCODE__SHIFT				0
#define VIV_ISA_WORD_0_OPCODE(x)				(((x) << VIV_ISA_WORD_0_OPCODE__SHIFT) & VIV_ISA_WORD_0_OPCODE__MASK)
#define VIV_ISA_WORD_0_COND__MASK				0x000007c0
#define VIV_ISA_WORD_0_COND__SHIFT				6
#define VIV_ISA_WORD_0_COND(x)					(((x) << VIV_ISA_WORD_0_COND__SHIFT) & VIV_ISA_WORD_0_COND__MASK)
#define VIV_ISA_WORD_0_SAT					0x00000800
#define VIV_ISA_WORD_0_DST_USE					0x00001000
#define VIV_ISA_WORD_0_DST_AMODE__MASK				0x0000e000
#define VIV_ISA_WORD_0_DST_AMODE__SHIFT				13
#define VIV_ISA_WORD_0_DST_AMODE(x)				(((x) << VIV_ISA_WORD_0_DST_AMODE__SHIFT) & VIV_ISA_WORD_0_DST_AMODE__MASK)
#define VIV_ISA_WORD_0_DST_REG__MASK				0x007f0000
#define VIV_ISA_WORD_0_DST_REG__SHIFT				16
#define VIV_ISA_WORD_0_DST_REG(x)				(((x) << VIV_ISA_WORD_0_DST_REG__SHIFT) & VIV_ISA_WORD_0_DST_REG__MASK)
#define VIV_ISA_WORD_0_DST_COMPS__MASK				0x07800000
#define VIV_ISA_WORD_0_DST_COMPS__SHIFT				23
#define VIV_ISA_WORD_0_DST_COMPS(x)				(((x) << VIV_ISA_WORD_0_DST_COMPS__SHIFT) & VIV_ISA_WORD_0_DST_COMPS__MASK)
#define VIV_ISA_WORD_0_TEX_ID__MASK				0xf8000000
#define VIV_ISA_WORD_0_TEX_ID__SHIFT				27
#define VIV_ISA_WORD_0_TEX_ID(x)				(((x) << VIV_ISA_WORD_0_TEX_ID__SHIFT) & VIV_ISA_WORD_0_TEX_ID__MASK)

#define VIV_ISA_WORD_1						0x00000004
#define VIV_ISA_WORD_1_TEX_AMODE__MASK				0x00000007
#define VIV_ISA_WORD_1_TEX_AMODE__SHIFT				0
#define VIV_ISA_WORD_1_TEX_AMODE(x)				(((x) << VIV_ISA_WORD_1_TEX_AMODE__SHIFT) & VIV_ISA_WORD_1_TEX_AMODE__MASK)
#define VIV_ISA_WORD_1_TEX_SWIZ__MASK				0x000007f8
#define VIV_ISA_WORD_1_TEX_SWIZ__SHIFT				3
#define VIV_ISA_WORD_1_TEX_SWIZ(x)				(((x) << VIV_ISA_WORD_1_TEX_SWIZ__SHIFT) & VIV_ISA_WORD_1_TEX_SWIZ__MASK)
#define VIV_ISA_WORD_1_SRC0_USE					0x00000800
#define VIV_ISA_WORD_1_SRC0_REG__MASK				0x001ff000
#define VIV_ISA_WORD_1_SRC0_REG__SHIFT				12
#define VIV_ISA_WORD_1_SRC0_REG(x)				(((x) << VIV_ISA_WORD_1_SRC0_REG__SHIFT) & VIV_ISA_WORD_1_SRC0_REG__MASK)
#define VIV_ISA_WORD_1_UNK1_21					0x00200000
#define VIV_ISA_WORD_1_SRC0_SWIZ__MASK				0x3fc00000
#define VIV_ISA_WORD_1_SRC0_SWIZ__SHIFT				22
#define VIV_ISA_WORD_1_SRC0_SWIZ(x)				(((x) << VIV_ISA_WORD_1_SRC0_SWIZ__SHIFT) & VIV_ISA_WORD_1_SRC0_SWIZ__MASK)
#define VIV_ISA_WORD_1_SRC0_NEG					0x40000000
#define VIV_ISA_WORD_1_SRC0_ABS					0x80000000

#define VIV_ISA_WORD_2						0x00000008
#define VIV_ISA_WORD_2_SRC0_AMODE__MASK				0x00000007
#define VIV_ISA_WORD_2_SRC0_AMODE__SHIFT			0
#define VIV_ISA_WORD_2_SRC0_AMODE(x)				(((x) << VIV_ISA_WORD_2_SRC0_AMODE__SHIFT) & VIV_ISA_WORD_2_SRC0_AMODE__MASK)
#define VIV_ISA_WORD_2_SRC0_RGROUP__MASK			0x00000038
#define VIV_ISA_WORD_2_SRC0_RGROUP__SHIFT			3
#define VIV_ISA_WORD_2_SRC0_RGROUP(x)				(((x) << VIV_ISA_WORD_2_SRC0_RGROUP__SHIFT) & VIV_ISA_WORD_2_SRC0_RGROUP__MASK)
#define VIV_ISA_WORD_2_SRC1_USE					0x00000040
#define VIV_ISA_WORD_2_SRC1_REG__MASK				0x0000ff80
#define VIV_ISA_WORD_2_SRC1_REG__SHIFT				7
#define VIV_ISA_WORD_2_SRC1_REG(x)				(((x) << VIV_ISA_WORD_2_SRC1_REG__SHIFT) & VIV_ISA_WORD_2_SRC1_REG__MASK)
#define VIV_ISA_WORD_2_OPCODE_BIT6				0x00010000
#define VIV_ISA_WORD_2_SRC1_SWIZ__MASK				0x01fe0000
#define VIV_ISA_WORD_2_SRC1_SWIZ__SHIFT				17
#define VIV_ISA_WORD_2_SRC1_SWIZ(x)				(((x) << VIV_ISA_WORD_2_SRC1_SWIZ__SHIFT) & VIV_ISA_WORD_2_SRC1_SWIZ__MASK)
#define VIV_ISA_WORD_2_SRC1_NEG					0x02000000
#define VIV_ISA_WORD_2_SRC1_ABS					0x04000000
#define VIV_ISA_WORD_2_SRC1_AMODE__MASK				0x38000000
#define VIV_ISA_WORD_2_SRC1_AMODE__SHIFT			27
#define VIV_ISA_WORD_2_SRC1_AMODE(x)				(((x) << VIV_ISA_WORD_2_SRC1_AMODE__SHIFT) & VIV_ISA_WORD_2_SRC1_AMODE__MASK)
#define VIV_ISA_WORD_2_UNK2_30__MASK				0xc0000000
#define VIV_ISA_WORD_2_UNK2_30__SHIFT				30
#define VIV_ISA_WORD_2_UNK2_30(x)				(((x) << VIV_ISA_WORD_2_UNK2_30__SHIFT) & VIV_ISA_WORD_2_UNK2_30__MASK)

#define VIV_ISA_WORD_3						0x0000000c
#define VIV_ISA_WORD_3_SRC1_RGROUP__MASK			0x00000007
#define VIV_ISA_WORD_3_SRC1_RGROUP__SHIFT			0
#define VIV_ISA_WORD_3_SRC1_RGROUP(x)				(((x) << VIV_ISA_WORD_3_SRC1_RGROUP__SHIFT) & VIV_ISA_WORD_3_SRC1_RGROUP__MASK)
#define VIV_ISA_WORD_3_SRC2_IMM__MASK				0x003fff80
#define VIV_ISA_WORD_3_SRC2_IMM__SHIFT				7
#define VIV_ISA_WORD_3_SRC2_IMM(x)				(((x) << VIV_ISA_WORD_3_SRC2_IMM__SHIFT) & VIV_ISA_WORD_3_SRC2_IMM__MASK)
#define VIV_ISA_WORD_3_SRC2_USE					0x00000008
#define VIV_ISA_WORD_3_SRC2_REG__MASK				0x00001ff0
#define VIV_ISA_WORD_3_SRC2_REG__SHIFT				4
#define VIV_ISA_WORD_3_SRC2_REG(x)				(((x) << VIV_ISA_WORD_3_SRC2_REG__SHIFT) & VIV_ISA_WORD_3_SRC2_REG__MASK)
#define VIV_ISA_WORD_3_UNK3_13					0x00002000
#define VIV_ISA_WORD_3_SRC2_SWIZ__MASK				0x003fc000
#define VIV_ISA_WORD_3_SRC2_SWIZ__SHIFT				14
#define VIV_ISA_WORD_3_SRC2_SWIZ(x)				(((x) << VIV_ISA_WORD_3_SRC2_SWIZ__SHIFT) & VIV_ISA_WORD_3_SRC2_SWIZ__MASK)
#define VIV_ISA_WORD_3_SRC2_NEG					0x00400000
#define VIV_ISA_WORD_3_SRC2_ABS					0x00800000
#define VIV_ISA_WORD_3_UNK3_24					0x01000000
#define VIV_ISA_WORD_3_SRC2_AMODE__MASK				0x0e000000
#define VIV_ISA_WORD_3_SRC2_AMODE__SHIFT			25
#define VIV_ISA_WORD_3_SRC2_AMODE(x)				(((x) << VIV_ISA_WORD_3_SRC2_AMODE__SHIFT) & VIV_ISA_WORD_3_SRC2_AMODE__MASK)
#define VIV_ISA_WORD_3_SRC2_RGROUP__MASK			0x70000000
#define VIV_ISA_WORD_3_SRC2_RGROUP__SHIFT			28
#define VIV_ISA_WORD_3_SRC2_RGROUP(x)				(((x) << VIV_ISA_WORD_3_SRC2_RGROUP__SHIFT) & VIV_ISA_WORD_3_SRC2_RGROUP__MASK)
#define VIV_ISA_WORD_3_UNK3_31					0x80000000


#endif /* ISA_XML */
