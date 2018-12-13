#ifndef __LIB_SERIALIZE_H
#define __LIB_SERIALIZE_H

#include "json_helper.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ser_meta_type {
	SER_T_NULL = json_type_null,
	SER_T_BOOLEAN = json_type_boolean,
	SER_T_DOUBLE = json_type_double,
	SER_T_INT = json_type_int,
	SER_T_OBJECT = json_type_object,
	SER_T_STRING = json_type_string,

	SER_T_INT64 = 1000,
	SER_T_FLOAT,

	SER_T_ARRAY_OBJECT,
	SER_T_ARRAY_INT,
	SER_T_ARRAY_INT64,
	SER_T_ARRAY_DOUBLE,
	SER_T_ARRAY_FLOAT,
	SER_T_ARRAY_STRING,
};

struct ser_meta {
	const char *name;
	const int type;
	const struct ser_meta *child;
	const int offset;
};

#define INIT_SER_META(STRUCT_TYPE, NAME, TYPE, CHILD) \
	{ #NAME, TYPE, CHILD, offsetof(STRUCT_TYPE, NAME) }
#define INIT_SER_META_NONE() { NULL, 0, NULL, 0 }

#define SER_SET(ptr, type, offset, value) \
do { \
	*(type*)((char *)ptr + offset) = value; \
} while (0)

#define SER_GET(ptr, type, offset) (*(type*)((char *)ptr + offset))

json_object *serialize(const void *ptr, const struct ser_meta *meta);
int deserialize(void *ptr, const struct ser_meta *meta, const json_object *root);

#ifdef __cplusplus
}
#endif
#endif
