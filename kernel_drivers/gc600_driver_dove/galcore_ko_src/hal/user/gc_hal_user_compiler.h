/****************************************************************************
*
*    Copyright (c) 2005 - 2010 by Vivante Corp.  All rights reserved.
*
*    The material in this file is confidential and contains trade secrets
*    of Vivante Corporation. This is proprietary information owned by
*    Vivante Corporation. No part of this work may be disclosed,
*    reproduced, copied, transmitted, or used in any way for any purpose,
*    without the express written permission of Vivante Corporation.
*
*****************************************************************************
*
*
*****************************************************************************/




#ifndef __gc_hal_user_compiler_h_
#define __gc_hal_user_compiler_h_

#include "gc_hal_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
|******************************* SHADER LANGUAGE ******************************|
\******************************************************************************/

/* Special register indices. */
#define gcSL_POSITION			((gctSIZE_T) -1)
#define gcSL_POINT_SIZE			((gctSIZE_T) -2)
#define gcSL_COLOR				((gctSIZE_T) -3)
#define gcSL_FRONT_FACING		((gctSIZE_T) -4)
#define gcSL_POINT_COORD		((gctSIZE_T) -5)
#define gcSL_POSITION_W			((gctSIZE_T) -6)

/* Special code generation indices. */
#define gcSL_CG_TEMP1			112
#define gcSL_CG_TEMP1_X			113
#define gcSL_CG_TEMP1_XY		114
#define gcSL_CG_TEMP1_XYZ		115
#define gcSL_CG_TEMP1_XYZW		116
#define gcSL_CG_TEMP2			117
#define gcSL_CG_TEMP2_X			118
#define gcSL_CG_TEMP2_XY		119
#define gcSL_CG_TEMP2_XYZ		120
#define gcSL_CG_TEMP2_XYZW		121
#define gcSL_CG_TEMP3			122
#define gcSL_CG_TEMP3_X			123
#define gcSL_CG_TEMP3_XY		124
#define gcSL_CG_TEMP3_XYZ		125
#define gcSL_CG_TEMP3_XYZW		126
#define gcSL_CG_CONSTANT		127

/* 4-bit enable bits. */
#define gcdSL_TARGET_Enable					 0 : 4
/* Indexed addressing mode of type gcSL_INDEXED. */
#define gcdSL_TARGET_Indexed				 4 : 4
/* 4-bit condition of type gcSL_CONDITION. */
#define gcdSL_TARGET_Condition				 8 : 4
/* Target format of type gcSL_FORMAT. */
#define gcdSL_TARGET_Format					12 : 2
/* Flag whether source0 index has been patched. */
#define gcdSL_TARGET_Source0Patched			14 : 1
/* Flag whether source1 index has been patched. */
#define gcdSL_TARGET_Source1Patched			15 : 1

#define gcmSL_TARGET_GET(Value, Field) \
	gcmGETBITS(Value, gctUINT16, gcdSL_TARGET_##Field)

#define gcmSL_TARGET_SET(Value, Field, NewValue) \
	gcmSETBITS(Value, gctUINT16, gcdSL_TARGET_##Field, NewValue)

/* Register type of type gcSL_TYPE. */
#define gcdSL_SOURCE_Type					 0 : 3
/* Indexed register swizzle. */
#define gcdSL_SOURCE_Indexed				 3 : 3
/* Source format of type gcSL_FORMAT. */
#define gcdSL_SOURCE_Format					 6 : 2
/* Swizzle fields of type gcSL_SWIZZLE. */
#define gcdSL_SOURCE_Swizzle				 8 : 8
#define gcdSL_SOURCE_SwizzleX				 8 : 2
#define gcdSL_SOURCE_SwizzleY				10 : 2
#define gcdSL_SOURCE_SwizzleZ				12 : 2
#define gcdSL_SOURCE_SwizzleW				14 : 2

#define gcmSL_SOURCE_GET(Value, Field) \
	gcmGETBITS(Value, gctUINT16, gcdSL_SOURCE_##Field)

#define gcmSL_SOURCE_SET(Value, Field, NewValue) \
	gcmSETBITS(Value, gctUINT16, gcdSL_SOURCE_##Field, NewValue)

/* Index of register. */
#define gcdSL_INDEX_Index					 0 : 14
/* Constant value. */
#define gcdSL_INDEX_ConstValue				14 :  2

#define gcmSL_INDEX_GET(Value, Field) \
	gcmGETBITS(Value, gctUINT16, gcdSL_INDEX_##Field)

#define gcmSL_INDEX_SET(Value, Field, NewValue) \
	gcmSETBITS(Value, gctUINT16, gcdSL_INDEX_##Field, NewValue)

/* Structure that defines a gcSL instruction. */
typedef struct _gcSL_INSTRUCTION
{
	/* Opcode of type gcSL_OPCODE. */
	gctUINT16					opcode;

	/* Opcode condition and target write enable bits of type gcSL_TARGET. */
	gctUINT16					temp;

	/* 16-bit temporary register index. */
	gctUINT16					tempIndex;

	/* Indexed register for destination. */
	gctUINT16					tempIndexed;

	/* Type of source 0 operand of type gcSL_SOURCE. */
	gctUINT16					source0;

	/* 16-bit register index for source 0 operand of type gcSL_INDEX. */
	gctUINT16					source0Index;

	/* Indexed register for source 0 operand. */
	gctUINT16					source0Indexed;

	/* Type of source 1 operand of type gcSL_SOURCE. */
	gctUINT16					source1;

	/* 16-bit register index for source 1 operand of type gcSL_INDEX. */
	gctUINT16					source1Index;

	/* Indexed register for source 1 operand. */
	gctUINT16					source1Indexed;
}
* gcSL_INSTRUCTION;

/******************************************************************************\
|*********************************** SHADERS **********************************|
\******************************************************************************/

/* Structure the defines an attribute (input) for a shader. */
struct _gcATTRIBUTE
{
	/* The object. */
	gcsOBJECT					object;

	/* Index of the attribute. */
	gctUINT16					index;

	/* Type of the attribute. */
	gcSHADER_TYPE				type;

	/* Number of array elements for this attribute. */
	gctSIZE_T					arraySize;

	/* Flag to indicate this attribute is used as a texture coordinate. */
	gctBOOL						isTexture;

	/* Flag to indicate this attribute is used as a position. */
	gctBOOL						isPosition;

	/* Flag to indicate this attribute is enabeld or not. */
	gctBOOL						enabled;

	/* Assigned input register index. */
	gctINT						inputIndex;

	/* Length of the attribute name. */
	gctSIZE_T					nameLength;

	/* The attribute name. */
	char						name[1];
};

/* Sampel structure, but inside a binary. */
typedef struct _gcBINARY_ATTRIBUTE
{
	/* Type for this attribute of type gcATTRIBUTE_TYPE. */
	gctINT8						type;

	/* Flag to indicate this attribute is used as a texture coordinate. */
	gctINT8						isTexture;

	/* Number of array elements for this attribute. */
	gctINT16					arraySize;

	/* Length of the attribute name. */
	gctINT16					nameLength;

	/* The attribute name. */
	char						name[1];
}
* gcBINARY_ATTRIBUTE;

/* Structure that defines an uniform (constant register) for a shader. */
struct _gcUNIFORM
{
	/* The object. */
	gcsOBJECT					object;

	/* Pointer to the gcoHAL object. */
	gcoHAL						hal;

	/* Index of the uniform. */
	gctUINT16					index;

	/* Type of the uniform. */
	gcSHADER_TYPE				type;

	/* Number of array elements for this uniform. */
	gctINT						arraySize;

	/* Physically assigned values. */
	gctINT						physical;
	gctUINT8					swizzle;
	gctUINT32					address;

	/* Length of the uniform name. */
	gctSIZE_T					nameLength;

	/* The uniform name. */
	char						name[1];
};

/* Same structure, but inside a binary. */
typedef struct _gcBINARY_UNIFORM
{
	/* Uniform type of type gcUNIFORM_TYPE. */
	gctINT16					type;

	/* Number of array elements for this uniform. */
	gctINT16					arraySize;

	/* Length of the uniform name. */
	gctINT16					nameLength;

	/* The uniform name. */
	char						name[1];
}
* gcBINARY_UNIFORM;

/* Structure that defines an output for a shader. */
struct _gcOUTPUT
{
	/* The object. */
	gcsOBJECT					object;

	/* Type for this output. */
	gcSHADER_TYPE				type;

	/* Number of array elements for this output. */
	gctSIZE_T					arraySize;

	/* Temporary register index that holds the output value. */
	gctUINT16					tempIndex;

	/* Converted to physical register. */
	gctBOOL						physical;

	/* Length of the output name. */
	gctSIZE_T					nameLength;

	/* The output name. */
	char						name[1];
};

/* Same structure, but inside a binary. */
typedef struct _gcBINARY_OUTPUT
{
	/* Type for this output. */
	gctINT8						type;

	/* Number of array elements for this output. */
	gctINT8						arraySize;

	/* Temporary register index that holds the output value. */
	gctUINT16					tempIndex;

	/* Length of the output name. */
	gctINT16					nameLength;

	/* The output name. */
	char						name[1];
}
* gcBINARY_OUTPUT;

/* Structure that defines a variable for a shader. */
struct _gcVARIABLE
{
	/* The object. */
	gcsOBJECT					object;

	/* Type for this output. */
	gcSHADER_TYPE				type;

	/* Number of array elements for this output. */
	gctSIZE_T					arraySize;

	/* Temporary register index that holds the variable value. */
	gctUINT16					tempIndex;

	/* Length of the output name. */
	gctSIZE_T					nameLength;

	/* The output name. */
	char						name[1];
};

typedef struct _gcsFUNCTION_ARGUMENT
{
	gctUINT16					index;
	gctUINT8					enable;
	gctUINT8					qualifier;
}
gcsFUNCTION_ARGUMENT,
* gcsFUNCTION_ARGUMENT_PTR;

struct _gcsFUNCTION
{
	gcsOBJECT					object;
	gcoOS						os;

	gctSIZE_T					argumentCount;
	gcsFUNCTION_ARGUMENT_PTR	arguments;

	gctUINT16					label;

	/* Local variables. */
	gctSIZE_T					variableCount;
	gcVARIABLE *				variables;

	gctUINT						codeStart;
	gctUINT						codeCount;

	gctSIZE_T					nameLength;
	char						name[1];
};

/* Index into current instruction. */
typedef enum _gcSHADER_INSTRUCTION_INDEX
{
	gcSHADER_OPCODE,
	gcSHADER_SOURCE0,
	gcSHADER_SOURCE1,
}
gcSHADER_INSTRUCTION_INDEX;

typedef struct _gcSHADER_LINK * gcSHADER_LINK;

/* Structure defining a linked references for a label. */
struct _gcSHADER_LINK
{
	gcSHADER_LINK				next;
	gctUINT						referenced;
};

typedef struct _gcSHADER_LABEL * gcSHADER_LABEL;

/* Structure defining a label. */
struct _gcSHADER_LABEL
{
	gcSHADER_LABEL				next;
	gctUINT						label;
	gctUINT						defined;
	gcSHADER_LINK				referenced;
};

/* The structure that defines the gcSHADER object to the outside world. */
struct _gcSHADER
{
	/* The base object. */
	gcsOBJECT					object;

	/* Pointer to an gcoHAL object. */
	gcoHAL						hal;

	/* Type of shader. */
	gctINT						type;

	/* Attributes. */
	gctSIZE_T					attributeCount;
	gcATTRIBUTE *				attributes;

	/* Uniforms. */
	gctSIZE_T					uniformCount;
	gcUNIFORM *					uniforms;
	gctINT						samplerIndex;

	/* Outputs. */
	gctSIZE_T					outputCount;
	gcOUTPUT *					outputs;

	/* Global variables. */
	gctSIZE_T					variableCount;
	gcVARIABLE *				variables;

	/* Functions. */
	gctSIZE_T					functionCount;
	gcFUNCTION *				functions;
	gcFUNCTION					currentFunction;

	/* Code. */
	gctSIZE_T					codeCount;
	gctUINT						lastInstruction;
	gcSHADER_INSTRUCTION_INDEX	instrIndex;
	gcSHADER_LABEL				labels;
	gcSL_INSTRUCTION			code;

	/* Optimization option. */
	gctUINT						optimizationOption;

    /* has loop ? */
    gctBOOL                     hasLoop;
};

/******************************************************************************\
|************************* gcSL_BRANCH_LIST structure. ************************|
\******************************************************************************/

typedef struct _gcSL_BRANCH_LIST * gcSL_BRANCH_LIST;

struct _gcSL_BRANCH_LIST
{
	/* Pointer to next gcSL_BRANCH_LIST structure in list. */
	gcSL_BRANCH_LIST	next;

	/* Pointer to generated instruction. */
	gctUINT				ip;

	/* Target instruction for branch. */
	gctUINT				target;

	/* Flag whether this is a branch or a call. */
	gctBOOL				call;
};

/******************************************************************************\
|**************************** gcLINKTREE structure. ***************************|
\******************************************************************************/

typedef struct _gcsLINKTREE_LIST *	gcsLINKTREE_LIST_PTR;

/* Structure that defines the linked list of dependencies. */
typedef struct _gcsLINKTREE_LIST
{
	/* Pointer to next dependent register. */
	gcsLINKTREE_LIST_PTR			next;

	/* Type of dependent register. */
	gcSL_TYPE						type;

	/* Index of dependent register. */
	gctINT							index;

	/* Reference counter. */
	gctINT							counter;
}
gcsLINKTREE_LIST;

/* Structure that defines the dependencies for an attribute. */
typedef struct _gcLINKTREE_ATTRIBUTE
{
	/* In-use flag. */
	gctBOOL						inUse;

	/* Instruction location the attribute was last used. */
	gctINT						lastUse;

	/* A linked list of all temporary registers using this attribute. */
	gcsLINKTREE_LIST_PTR		users;
}
* gcLINKTREE_ATTRIBUTE;

/* Structure that defines the dependencies for a temporary register. */
typedef struct _gcLINKTREE_TEMP
{
	/* In-use flag. */
	gctBOOL						inUse;

	/* Usage flags for the temporary register. */
	gctUINT8					usage;

	/* True if the reghister is used as an index. */
	gctBOOL						isIndex;

	/* Instruction locations that defines the temporary register. */
	gcsLINKTREE_LIST_PTR		defined;

	/* Instruction location the temporary register was last used. */
	gctINT						lastUse;

	/* Dependencies for the temporary register. */
	gcsLINKTREE_LIST_PTR		dependencies;

	/* Whether the register holds a constant. */
	gctINT8						constUsage[4];
	gctFLOAT					constValue[4];

	/* A linked list of all registers using this temporary register. */
	gcsLINKTREE_LIST_PTR		users;

	/* Physical register this temporary register is assigned to. */
	gctINT						assigned;
	gctUINT8					swizzle;
	gctINT						shift;

	/* Function arguments. */
	gcFUNCTION					owner;
	gceINPUT_OUTPUT				inputOrOutput;

	/* Variable in shader's symbol table. */
	gcVARIABLE					variable;
}
* gcLINKTREE_TEMP;

/* Structure that defines the outputs. */
typedef struct _gcLINKTREE_OUTPUT
{
	/* In-use flag. */
	gctBOOL						inUse;

	/* Temporary register holding the output value. */
	gctINT						tempHolding;

	/* Fragment attribute linked to this vertex output. */
	gctINT						fragmentAttribute;
	gctINT						fragmentIndex;
}
* gcLINKTREE_OUTPUT;

typedef struct _gcsCODE_CALLER	*gcsCODE_CALLER_PTR;
typedef struct _gcsCODE_CALLER
{
	gcsCODE_CALLER_PTR			next;

	gctINT						caller;
}
gcsCODE_CALLER;

typedef struct _gcsCODE_HINT
{
	/* Pointer to function to which this code belongs. */
	gcFUNCTION					owner;

	/* Callers to this instruction. */
	gcsCODE_CALLER_PTR			callers;

	/* Nesting of call. */
	gctINT						callNest;
}
gcsCODE_HINT, *gcsCODE_HINT_PTR;

/* Structure that defines the entire life and dependency for a shader. */
typedef struct _gcLINKTREE
{
	/* Pointer to an gcoOS object. */
	gcoOS						os;

	/* Pointer to the gcSHADER object. */
	gcSHADER					shader;

	/* Number of attributes. */
	gctSIZE_T					attributeCount;

	/* Attributes. */
	gcLINKTREE_ATTRIBUTE		attributeArray;

	/* Number of temporary registers. */
	gctSIZE_T					tempCount;

	/* Temporary registers. */
	gcLINKTREE_TEMP				tempArray;

	/* Number of outputs. */
	gctSIZE_T					outputCount;

	/* Outputs. */
	gcLINKTREE_OUTPUT			outputArray;

	/* Uniform usage. */
	gctSIZE_T					uniformUsage;

	/* Resource allocation passed. */
	gctBOOL						physical;

	/* Branch list. */
	gcSL_BRANCH_LIST			branch;

	/* Code hints. */
	gcsCODE_HINT_PTR			hints;
}
* gcLINKTREE;

/* Generate hardware states. */
gceSTATUS
gcLINKTREE_GenerateStates(
	IN gcLINKTREE Tree,
	IN gceSHADER_FLAGS Flags,
	IN OUT gctSIZE_T * StateBufferSize,
	IN OUT gctPOINTER * StateBuffer,
	OUT gcsHINT_PTR * Hints
	);

typedef struct _gcsCODE_GENERATOR	gcsCODE_GENERATOR;
typedef struct _gcsCODE_GENERATOR	* gcsCODE_GENERATOR_PTR;

gctUINT
gcsCODE_GENERATOR_GetIP(
	gcsCODE_GENERATOR_PTR CodeGen
	);

typedef gctBOOL (*gctSL_FUNCTION_PTR)(
	IN gcLINKTREE Tree,
	IN gcsCODE_GENERATOR_PTR CodeGen,
	IN gcSL_INSTRUCTION Instruction,
	IN OUT gctUINT32 * States
	);

typedef struct _gcsSL_PATTERN		gcsSL_PATTERN;
typedef struct _gcsSL_PATTERN		* gcsSL_PATTERN_PTR;
struct _gcsSL_PATTERN
{
	/* Positive: search index, aproaching zero.
	   Negative: code generation index aproaching zero. */
	gctINT							count;

	/* Opcode. */
	gctUINT							opcode;

	/* Destination reference number. */
	gctINT8							dest;

	/* Source 0 reference number. */
	gctINT8							source0;

	/* Source 1 reference number. */
	gctINT8							source1;

	/* Source 2 reference number. */
	gctINT8							source2;

	/* Sampler reference number. */
	gctINT8							sampler;

	/* Code generation function. */
	gctSL_FUNCTION_PTR				function;
};

gctSIZE_T
gcSHADER_GetHintSize(
	void
	);

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_user_compiler_h_ */

