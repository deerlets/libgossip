#ifndef __LIB_JSON_HELPER_H
#define __LIB_JSON_HELPER_H

#include <json-c/json.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSON_PARSE(name, buf, len) \
json_object *name = ({ \
	json_tokener *tok = json_tokener_new(); \
	json_object *root = json_tokener_parse_ex(tok, buf, len); \
	json_tokener_free(tok); \
	root; \
})

#define JSON_GET_OBJECT(root, name) \
	json_object_object_get(root, name)

#define JSON_GET_INT(root, name) \
	json_object_get_int(json_object_object_get(root, name))

#define JSON_GET_INT64(root, name) \
	json_object_get_int64(json_object_object_get(root, name))

#define JSON_GET_DOUBLE(root, name) \
	json_object_get_double(json_object_object_get(root, name))

#define JSON_GET_STRING(root, name) \
	json_object_get_string(json_object_object_get(root, name))

#define JSON_ADD_OBJECT(root, key, value) \
	json_object_object_add(root, key, value)

#define JSON_ADD_INT(root, key, value) \
	json_object_object_add(root, key, json_object_new_int(value))

#define JSON_ADD_INT64(root, key, value) \
	json_object_object_add(root, key, json_object_new_int64(value))

#define JSON_ADD_DOUBLE(root, key, value) \
	json_object_object_add(root, key, json_object_new_double(value))

#define JSON_ADD_STRING(root, key, value) \
	json_object_object_add(root, key, json_object_new_string(value))

#define JSON_DUMP(root) \
	json_object_to_json_string(root)

static inline bool
__check_json_item(const json_object *root, const char *name, int type)
{
	json_object *obj = JSON_GET_OBJECT(root, name);

	if (!obj)
		return false;

	if (type != -1 && !json_object_is_type(obj, (json_type)type))
		return false;

	return true;
}

#define JSON_HAS(root, name) \
	__check_json_item(root, name, -1)
#define JSON_HAS_INT(root, name) \
	__check_json_item(root, name, json_type_int)
#define JSON_HAS_DOUBLE(root, name) \
	__check_json_item(root, name, json_type_double)
#define JSON_HAS_STRING(root, name) \
	__check_json_item(root, name, json_type_string)
#define JSON_HAS_ARRAY(root, name) \
	__check_json_item(root, name, json_type_array)
#define JSON_HAS_OBJECT(root, name) \
	__check_json_item(root, name, json_type_object)

struct param_meta {
	const char *name;
	const int type;
	const int required;
	union {
		int64_t i;
		double d;
		char *s;
	} default_value;
};

#define INIT_PARAM_INT(name, required, __default_value) \
	{ name, json_type_int, required, .default_value.i = __default_value }
#define INIT_PARAM_DOUBLE(name, required, __default_value) \
	{ name, json_type_double, required, .default_value.d = __default_value }
#define INIT_PARAM_STRING(name, required, __default_value) \
	{ name, json_type_string, required, .default_value.s = __default_value }

#define INIT_REQUIRED_PARAM_INT(name) \
	INIT_PARAM_INT(name, 1, 0)
#define INIT_REQUIRED_PARAM_DOUBLE(name) \
	INIT_PARAM_DOUBLE(name, 1, 0)
#define INIT_REQUIRED_PARAM_STRING(name) \
	INIT_PARAM_STRING(name, 1, 0)
#define INIT_REQUIRED_PARAM(name) \
	{ name, -1, 1, .default_value.i = 0 }

#define INIT_UNREQUIRED_PARAM_INT(name, default_value) \
	INIT_PARAM_INT(name, 0, default_value)
#define INIT_UNREQUIRED_PARAM_DOUBLE(name, default_value) \
	INIT_PARAM_DOUBLE(name, 0, default_value)
#define INIT_UNREQUIRED_PARAM_STRING(name, default_value) \
	INIT_PARAM_STRING(name, 0, default_value)

#define INIT_PARAM_NONE() { NULL, 0, 0, .default_value.i = 0 }

static inline bool
check_params(json_object *root, const struct param_meta *meta)
{
	if (!meta->name) return true;

	json_object *obj = JSON_GET_OBJECT(root, meta->name);

	if (!obj) {
		if (meta->required)
			return false;
		else if (meta->type == json_type_int)
			JSON_ADD_INT(root, meta->name, meta->default_value.i);
		else if (meta->type == json_type_double)
			JSON_ADD_DOUBLE(root, meta->name, meta->default_value.d);
		else if (meta->type == json_type_string)
			JSON_ADD_STRING(root, meta->name, meta->default_value.s);
		else if (meta->type != -1)
			return false;

		obj = JSON_GET_OBJECT(root, meta->name);
	}

	if (meta->type != -1 && !json_object_is_type(obj, (json_type)meta->type))
		return false;

	return check_params(root, ++meta);
}

#ifdef __cplusplus
}
#endif
#endif
