#include "serialize.h"
#include <string.h>

json_object *serialize(const void *ptr, const struct ser_meta *meta)
{
	json_object *root = json_object_new_object();

	for (int i = 0; meta[i].name; i++) {
		if (meta[i].type == SER_T_INT)
			JSON_ADD_INT(root, meta[i].name,
			             SER_GET(ptr, int, meta[i].offset));
		else if (meta[i].type == SER_T_INT64)
			JSON_ADD_INT64(root, meta[i].name,
			               SER_GET(ptr, int64_t, meta[i].offset));
		else if (meta[i].type == SER_T_FLOAT)
			JSON_ADD_DOUBLE(root, meta[i].name,
			                SER_GET(ptr, float, meta[i].offset));
		else if (meta[i].type == SER_T_DOUBLE)
			JSON_ADD_DOUBLE(root, meta[i].name,
			                SER_GET(ptr, double, meta[i].offset));
		else if (meta[i].type == SER_T_STRING)
			JSON_ADD_STRING(root, meta[i].name,
			                SER_GET(ptr, char *, meta[i].offset));
	}

	return root;
}

int deserialize(void *ptr, const struct ser_meta *meta, const json_object *root)
{
	for (int i = 0; meta[i].name; i++) {
		json_object *obj = JSON_GET_OBJECT(root, meta[i].name);
		int type = meta[i].type;

		if (type == SER_T_INT64)
			type = json_type_int;
		else if (type == SER_T_FLOAT)
			type = json_type_double;
		else if (type >= SER_T_ARRAY_OBJECT &&
		         type <= SER_T_ARRAY_STRING)
			type = json_type_array;

		if (!obj || !json_object_is_type(obj, type))
			return -1;
	}

	for (int i = 0; meta[i].name; i++) {
		if (meta[i].type == SER_T_INT)
			SER_SET(ptr, int, meta[i].offset,
			         JSON_GET_INT(root, meta[i].name));
		else if (meta[i].type == SER_T_INT64)
			SER_SET(ptr, long long, meta[i].offset,
			         JSON_GET_INT64(root, meta[i].name));
		else if (meta[i].type == SER_T_FLOAT)
			SER_SET(ptr, float, meta[i].offset,
			         JSON_GET_DOUBLE(root, meta[i].name));
		else if (meta[i].type == SER_T_DOUBLE)
			SER_SET(ptr, double, meta[i].offset,
			         JSON_GET_DOUBLE(root, meta[i].name));
		else if (meta[i].type == SER_T_STRING)
			SER_SET(ptr, char *, meta[i].offset,
			         strdup(JSON_GET_STRING(root, meta[i].name)));
	}

	return 0;
}
