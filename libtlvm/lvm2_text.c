/*-
 * Copyright (C) 2012-2014 Erik Larsson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "lvm2_text.h"
#include "lvm2_endians.h"
#include "lvm2_osal.h"

#include <string.h>

#include <sys/errno.h>
#include <machine/limits.h>

#define LogTrace(...)

static const struct raw_locn null_raw_locn = { 0, 0, 0, 0 };

LVM2_EXPORT u32 lvm2_calc_crc(u32 initial, const void *buf, size_t size)
{
	static const u32 crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
	};
	size_t i;
	u32 crc = initial;
	const u8 *data = (const u8*) buf;

	LogTrace("%s: Entering with initial=0x%08" FMTlX " buf=%p "
		"size=%" FMTzu ".",
		__FUNCTION__, ARGlX(initial), buf, ARGzu(size));

	for(i = 0; i < size; i++) {
		crc ^= *data++;
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}

	return crc;
}

static int lvm2_bounded_string_create(const char *const content,
		const int length, struct lvm2_bounded_string **const out_string)
{
	int err;
	struct lvm2_bounded_string *string;

	if(length < 0)
		return EINVAL;

	err = lvm2_malloc(sizeof(struct lvm2_bounded_string) +
		((length + 1) * sizeof(char)), (void**) &string);
	if(!err) {
		string->length = length;
		memcpy(string->content, content, length * sizeof(char));
		string->content[length] = '\0';

		*out_string = string;
	}

	return err;
}

static int lvm2_bounded_string_dup(const struct lvm2_bounded_string *const orig,
		struct lvm2_bounded_string **const out_dup)
{
	int err;
	struct lvm2_bounded_string *dup;

	if(orig->length < 0)
		return EINVAL;

	err = lvm2_malloc(sizeof(struct lvm2_bounded_string) +
		((orig->length + 1) * sizeof(char)), (void**) &dup);
	if(!err) {
		dup->length = orig->length;
		memcpy(dup->content, orig->content,
			orig->length * sizeof(char));
		dup->content[orig->length] = '\0';

		*out_dup = dup;
	}

	return err;
}

static void lvm2_bounded_string_destroy(
		struct lvm2_bounded_string **string)
{
	lvm2_free((void**) string,
		sizeof(struct lvm2_bounded_string) +
		(((*string)->length + 1) * sizeof(char)));
}

static int lvm2_dom_obj_initialize(const lvm2_dom_type type,
		const char *const obj_name, const int obj_name_len,
		struct lvm2_dom_obj *const obj)
{
	int err;
	struct lvm2_bounded_string *obj_name_dup;

	memset(obj, 0, sizeof(struct lvm2_dom_obj));

	err = lvm2_bounded_string_create(obj_name, obj_name_len,
		&obj_name_dup);
	if(!err) {
		obj->type = type;
		obj->name = obj_name_dup;
	}

	return err;
}

static void lvm2_dom_obj_deinitialize(struct lvm2_dom_obj *const obj)
{
	lvm2_bounded_string_destroy(&obj->name);

	memset(obj, 0, sizeof(struct lvm2_dom_obj));
}

static int lvm2_dom_value_create(const char *const value_name,
		const int value_name_len, const char *const value_string,
		const int value_string_len,
		struct lvm2_dom_value **const out_value)
{
	int err;
	struct lvm2_dom_value *value;

	err = lvm2_malloc(sizeof(struct lvm2_dom_value), (void**) &value);
	if(err) {
		LogError("Error while allocating memory for struct "
			"lvm2_dom_value: %d", err);
	}
	else if((uintptr_t) &value->obj_super != (uintptr_t) value) {
		LogError("Incompatible compiler struct layout.");
		err = EINVAL;
	}
	else {
		memset(value, 0, sizeof(struct lvm2_dom_value));

		err = lvm2_dom_obj_initialize(LVM2_DOM_TYPE_VALUE,
			value_name, value_name_len, &value->obj_super);
		if(err) {
			LogError("Error while initializing super: %d", err);
		}
		else {
			struct lvm2_bounded_string *value_bounded_string;

			err = lvm2_bounded_string_create(value_string,
				value_string_len, &value_bounded_string);
			if(err) {
				LogError("Error while creating bounded "
					"value_string: %d", err);
			}
			else {
				value->value = value_bounded_string;
				*out_value = value;
			}
		}

		if(err) {
			lvm2_free((void**) &value,
				sizeof(struct lvm2_dom_value));
		}
	}

	return err;
}

static void lvm2_dom_value_destroy(struct lvm2_dom_value **const value)
{
	LogTrace("%s: Entering with value=%p.",
		__FUNCTION__, value);
	LogTrace("\t*value = %p", *value);

	if((*value)->obj_super.type != LVM2_DOM_TYPE_VALUE) {
		LogError("BUG: Wrong type (%d) of DOM object in %s.",
			(*value)->obj_super.type, __FUNCTION__);
		return;
	}

	lvm2_dom_obj_deinitialize(&(*value)->obj_super);

	lvm2_bounded_string_destroy(&(*value)->value);

	lvm2_free((void**) value, sizeof(struct lvm2_dom_value));
}

static int lvm2_dom_array_create(const char *const array_name,
		const int array_name_len,
		struct lvm2_dom_array **const out_array)
{
	int err;
	struct lvm2_dom_array *array;

	err = lvm2_malloc(sizeof(struct lvm2_dom_array), (void**) &array);
	if(err) {
		LogError("Error while allocating memory for struct "
			"lvm2_dom_array: %d", err);
	}
	else if((uintptr_t) &array->obj_super != (uintptr_t) array) {
		LogError("Incompatible compiler struct layout.");
		err = EINVAL;
	}
	else {
		memset(array, 0, sizeof(struct lvm2_dom_array));

		err = lvm2_dom_obj_initialize(LVM2_DOM_TYPE_ARRAY,
			array_name, array_name_len, &array->obj_super);
		if(err) {
			LogError("Error while initializing super: %d", err);
		}
		else {
			array->elements = NULL;
			array->elements_len = 0;

			*out_array = array;
		}
	}

	return err;
}

static int lvm2_dom_array_add_element(struct lvm2_dom_array *const array,
		struct lvm2_dom_value *const element)
{
	int err;
	struct lvm2_dom_value **old_elements;
	struct lvm2_dom_value **new_elements;
	size_t old_elements_len;
	size_t new_elements_len;
	size_t old_elements_size;
	size_t new_elements_size;

	LogTrace("%s: Entering with array=%p element=%p.",
		__FUNCTION__, array, element);

	if(!array) {
		LogError("NULL 'array'.");
		return EINVAL;
	}
	else if(array->obj_super.type != LVM2_DOM_TYPE_ARRAY) {
		LogError("Non-array type passed to %s.", __FUNCTION__);
		return EINVAL;
	}

	old_elements = array->elements;
	old_elements_len = array->elements_len;
	old_elements_size = old_elements_len * sizeof(struct lvm2_dom_obj*);

	new_elements_len = old_elements_len + 1;
	new_elements_size = new_elements_len * sizeof(struct lvm2_dom_obj*);
	err = lvm2_malloc(new_elements_size, (void**) &new_elements);
	if(err) {
		LogError("Error while allocating memory for elements array "
			"expansion: %d", err);
	}
	else {
		if(old_elements)
			memcpy(new_elements, old_elements, old_elements_size);
		new_elements[new_elements_len - 1] = element;

		array->elements = new_elements;
		array->elements_len = new_elements_len;

		LogDebug("Expanded array from %" FMTzu " (%p) to %" FMTzu " "
			"(%p) elements.", ARGzu(old_elements_len), old_elements,
			ARGzu(new_elements_len), new_elements);

		if(old_elements) {
			lvm2_free((void**) &old_elements, old_elements_size);
		}
	}

	return err;
}

static void lvm2_dom_array_destroy(struct lvm2_dom_array **const array,
		const lvm2_bool recursive)
{
	LogTrace("%s: Entering with array=%p recursive=%d.",
		__FUNCTION__, array, recursive);
	LogTrace("\t*array = %p", *array);

	if((*array)->obj_super.type != LVM2_DOM_TYPE_ARRAY) {
		LogError("BUG: Wrong type (%d) of DOM object in %s.",
			(*array)->obj_super.type, __FUNCTION__);
		return;
	}

	if(recursive) {
		size_t i;

		for(i = 0; i < (*array)->elements_len; ++i) {
			LogTrace("\t(*array)->elements[%" FMTzu "] = %p",
				ARGzu(i), (*array)->elements[i]);
		}

		for(i = 0; i < (*array)->elements_len; ++i) {
			lvm2_dom_value_destroy(&(*array)->elements[i]);
		}
	}

	lvm2_dom_obj_deinitialize(&(*array)->obj_super);

	if((*array)->elements) {
		lvm2_free((void**) &(*array)->elements,
			((*array)->elements_len *
			sizeof(struct lvm2_dom_value*)));
	}

	(*array)->elements_len = 0;

	lvm2_free((void**) array, sizeof(struct lvm2_dom_array));
}

static int lvm2_dom_section_create(const char *const section_name,
		const int section_name_len,
		struct lvm2_dom_section **const out_section)
{
	int err;
	struct lvm2_dom_section *section;

	err = lvm2_malloc(sizeof(struct lvm2_dom_section), (void**) &section);
	if(err) {
		LogError("Error while allocating memory for struct "
			"lvm2_dom_section: %d", err);
	}
	else if((uintptr_t) &section->obj_super != (uintptr_t) section) {
		LogError("Incompatible compiler struct layout.");
		err = EINVAL;
	}
	else {
		memset(section, 0, sizeof(struct lvm2_dom_section));

		err = lvm2_dom_obj_initialize(LVM2_DOM_TYPE_SECTION,
			section_name, section_name_len, &section->obj_super);
		if(err) {
			LogError("Error while initializing super: %d", err);
		}
		else {
			section->children = NULL;
			section->children_len = 0;

			*out_section = section;
		}
	}

	return err;
}

static int lvm2_dom_section_add_child(struct lvm2_dom_section *const section,
		struct lvm2_dom_obj *const child)
{
	int err;
	struct lvm2_dom_obj **old_children;
	struct lvm2_dom_obj **new_children;
	size_t old_children_len;
	size_t new_children_len;
	size_t old_children_size;
	size_t new_children_size;

	LogTrace("%s: Entering with section=%p child=%p.",
		__FUNCTION__, section, child);

	if(!section) {
		LogError("NULL 'section_obj'.");
		return EINVAL;
	}
	else if(section->obj_super.type != LVM2_DOM_TYPE_SECTION) {
		LogError("Non-section type passed to %s.", __FUNCTION__);
		return EINVAL;
	}

	old_children = section->children;
	LogTrace("old_children=%p", old_children);
	old_children_len = section->children_len;
	LogTrace("old_children_len=%" FMTzu, ARGzu(old_children_len));
	old_children_size = old_children_len * sizeof(struct lvm2_dom_obj*);
	LogTrace("old_children_size=%" FMTzu, ARGzu(old_children_size));

	new_children_len = old_children_len + 1;
	LogTrace("new_children_len=%" FMTzu, ARGzu(new_children_len));
	new_children_size = new_children_len * sizeof(struct lvm2_dom_obj*);
	LogTrace("Allocating %" FMTzu " bytes...", ARGzu(new_children_size));
	err = lvm2_malloc(new_children_size, (void**) &new_children);
	if(err) {
		LogError("Error while allocating memory for children array "
			"expansion: %d", err);
	}
	else {
		LogDebug("\tAllocated %" FMTzu " bytes.",
			ARGzu(new_children_size));
		if(old_children)
			memcpy(new_children, old_children, old_children_size);
		new_children[new_children_len - 1] = child;

		section->children = new_children;
		section->children_len = new_children_len;

		/*
		LogDebug("[%.*s] Expanded section from %" FMTzu " (%p) to "
			"%" FMTzu " (%p) elements.",
			section->obj_super.name->length,
			section->obj_super.name->content,
			ARGzu(old_children_len), old_children,
			ARGzu(new_children_len), new_children);
		LogDebug("[%.*s] Old array:",
			section->obj_super.name->length,
			section->obj_super.name->content);
		{
			size_t i;
			for(i = 0; i < old_children_len; ++i)
				LogDebug("[%.*s]     %" FMTzu ": %p",
					section->obj_super.name->length,
					section->obj_super.name->content,
					ARGzu(i), old_children[i]);
				
		}
		LogDebug("[%.*s] New array:",
			section->obj_super.name->length,
			section->obj_super.name->content);
		{
			size_t i;
			for(i = 0; i < new_children_len; ++i)
				LogDebug("[%.*s]     %" FMTzu ": %p",
					section->obj_super.name->length,
					section->obj_super.name->content,
					ARGzu(i), new_children[i]);
				
		}
		*/

		if(old_children) {
			lvm2_free((void**) &old_children, old_children_size);
		}
	}

	return err;
}

static void lvm2_dom_section_remove_last_child(
		struct lvm2_dom_section *const section)
{
	--section->children_len;
	section->children[section->children_len] = NULL;
}

LVM2_EXPORT void lvm2_dom_section_destroy(
		struct lvm2_dom_section **const section,
		const lvm2_bool recursive)
{
	LogDebug("%s: Entering with section=%p recursive=%d.",
		__FUNCTION__, section, recursive);
	LogDebug("\t*section = %p", *section);

	if((*section)->obj_super.type != LVM2_DOM_TYPE_SECTION) {
		LogError("BUG: Wrong type (%d) of DOM object in %s.",
			(*section)->obj_super.type, __FUNCTION__);
		return;
	}

	LogDebug("\tname: \"%.*s\"",
		(*section)->obj_super.name->length,
		(*section)->obj_super.name->content);

	if(recursive) {
		size_t i;

		for(i = 0; i < (*section)->children_len; ++i) {
			LogDebug("[%.*s] Iterating %" FMTzu "/%" FMTzu ": %p",
				(*section)->obj_super.name->length,
				(*section)->obj_super.name->content,
				ARGzu(i + 1), ARGzu((*section)->children_len),
				(*section)->children[i]);
			
			switch((*section)->children[i]->type) {
			case LVM2_DOM_TYPE_VALUE:
				LogDebug("[%.*s]     \"%.*s\": Value type.",
					(*section)->obj_super.name->length,
					(*section)->obj_super.name->content,
					(*section)->children[i]->name->length,
					(*section)->children[i]->name->content);
				lvm2_dom_value_destroy(
					(struct lvm2_dom_value**)
					(&(*section)->children[i]));
				break;
			case LVM2_DOM_TYPE_SECTION:
				LogDebug("[%.*s]     \"%.*s\": Section type.",
					(*section)->obj_super.name->length,
					(*section)->obj_super.name->content,
					(*section)->children[i]->name->length,
					(*section)->children[i]->name->content);
				lvm2_dom_section_destroy(
					(struct lvm2_dom_section**)
					&((*section)->children[i]),
					LVM2_TRUE);
				break;
			case LVM2_DOM_TYPE_ARRAY:
				LogDebug("[%.*s]     \"%.*s\": Array type.",
					(*section)->obj_super.name->length,
					(*section)->obj_super.name->content,
					(*section)->children[i]->name->length,
					(*section)->children[i]->name->content);
				lvm2_dom_array_destroy(
					(struct lvm2_dom_array**)
					&((*section)->children[i]),
					LVM2_TRUE);
				break;
			default:
				LogError("Unknown DOM type: %d Leaking...",
					(*section)->children[i]->type);
				break;
			};
		}
	}

	lvm2_dom_obj_deinitialize(&(*section)->obj_super);

	if((*section)->children) {
		lvm2_free((void**) &(*section)->children,
			((*section)->children_len *
			sizeof(struct lvm2_dom_obj*)));
	}

	(*section)->children_len = 0;

	lvm2_free((void**) section, sizeof(struct lvm2_dom_section));
}

static void parsed_lvm2_text_builder_init(
		struct parsed_lvm2_text_builder *const builder)
{
	memset(builder, 0, sizeof(struct parsed_lvm2_text_builder));
}

static int parsed_lvm2_text_builder_stack_push(
		struct parsed_lvm2_text_builder *const builder,
		struct lvm2_dom_obj *obj)
{
	int err;
	size_t new_elem_size = sizeof(struct parsed_lvm2_text_builder_section);
	struct parsed_lvm2_text_builder_section *new_elem;

	err = lvm2_malloc(new_elem_size, (void**) &new_elem);
	if(err) {
		LogError("Error while allocating memory for new stack element: "
			"%d", err);
	}
	else {
		memset(new_elem, 0, new_elem_size);

		new_elem->obj = obj;
		new_elem->parent = builder->stack;

		builder->stack = new_elem;
		++builder->stack_depth;
	}

	return err;
}

static void parsed_lvm2_text_builder_stack_pop(
		struct parsed_lvm2_text_builder *const builder)
{
	struct parsed_lvm2_text_builder_section *cur_elem = builder->stack;

	if(cur_elem) {
		LogDebug("Destroying top stack element (%.*s).",
			cur_elem->obj->name->length,
			cur_elem->obj->name->content);
		builder->stack = cur_elem->parent;
		--builder->stack_depth;

		lvm2_free((void**) &cur_elem,
			sizeof(struct parsed_lvm2_text_builder_section));
		cur_elem = NULL;
	}
}

static struct lvm2_dom_section* parsed_lvm2_text_builder_finalize(
		struct parsed_lvm2_text_builder *const builder)
{
	/* Pop elements off the stack and free them. */
	while(builder->stack)
		parsed_lvm2_text_builder_stack_pop(builder);

	LogDebug("%s: Returning %p.", __FUNCTION__, builder->root);

	return builder->root;
}

static int parsed_lvm2_text_builder_enter_array(
		struct parsed_lvm2_text_builder *const builder,
		const char *const array_name, const int array_name_len)
{
	int err;
	struct lvm2_dom_section *old_stack_top;
	struct lvm2_dom_array *dom_array;

	LogTrace("%s: Entering with builder=%p array_name=%p (%.*s) "
		"section_name_len=%d",
		__FUNCTION__, builder, array_name, array_name_len, array_name,
		array_name_len);

	if(!builder->stack) {
		LogError("Attempted to use array as root element. Aborting...");
		return EINVAL;
	}
	else if(builder->stack->obj->type != LVM2_DOM_TYPE_SECTION) {
		LogError("Unexpected type of top stack element: %d",
			builder->stack->obj->type);
		return EINVAL;
	}

	old_stack_top = (struct lvm2_dom_section*) builder->stack->obj;

	err = lvm2_dom_array_create(array_name, array_name_len,
		&dom_array);
	if(err) {
		LogError("Error while creating DOM array: %d", err);
	}
	else {
		err = lvm2_dom_section_add_child(old_stack_top,
			&dom_array->obj_super);
		if(err) {
			LogError("Error while adding element to parent "
				"array: %d", err);
		}
		else {
			err = parsed_lvm2_text_builder_stack_push(builder,
				&dom_array->obj_super);
			if(err) {
				LogError("Error while pushing new element to "
					"the stack: %d",
					err);
			}

			if(err) {
				lvm2_dom_section_remove_last_child(
					old_stack_top);
			}
		}

		if(err) {
			lvm2_dom_array_destroy(&dom_array, LVM2_FALSE);
		}
	}

	return err;
}

static int parsed_lvm2_text_builder_leave_array(
		struct parsed_lvm2_text_builder *const builder)
{
	parsed_lvm2_text_builder_stack_pop(builder);

	return 0;
}

static int parsed_lvm2_text_builder_enter_section(
		struct parsed_lvm2_text_builder *const builder,
		const char *const section_name, const int section_name_len)
{
	int err;
	struct lvm2_dom_section *old_stack_top;
	struct lvm2_dom_section *dom_section;

	LogTrace("%s: Entering with builder=%p section_name=%p (%.*s) "
		"section_name_len=%d",
		__FUNCTION__, builder, section_name, section_name_len,
		section_name, section_name_len);

	if(builder->stack && builder->stack->obj->type != LVM2_DOM_TYPE_SECTION)
	{
		LogError("Unexpected type of top stack element: %d",
			builder->stack->obj->type);
		return EINVAL;
	}

	old_stack_top = builder->stack ?
		(struct lvm2_dom_section*) builder->stack->obj : NULL;

	err = lvm2_dom_section_create(section_name, section_name_len,
		&dom_section);
	if(err) {
		LogError("Error while creating DOM section: %d", err);
	}
	else {
		if(!old_stack_top) {
			if(builder->root) {
				LogError("Multiple documents roots! "
					"Aborting...");
				err = EINVAL;
			}
			else {
				builder->root = dom_section;
			}
		}
		else {
			err = lvm2_dom_section_add_child(old_stack_top,
				&dom_section->obj_super);
			if(err) {
				LogError("Error while adding child to parent "
					"section: %d", err);
			}
		}

		if(!err) {
			err = parsed_lvm2_text_builder_stack_push(builder,
				&dom_section->obj_super);
			if(err) {
				LogError("Error while pushing new element to "
					"the stack: %d",
					err);
			}

			if(err && old_stack_top)
				lvm2_dom_section_remove_last_child(
					old_stack_top);

		}

		if(err) {
			lvm2_dom_section_destroy(&dom_section, LVM2_FALSE);
		}
	}

	return err;
}

static void parsed_lvm2_text_builder_leave_section(
		struct parsed_lvm2_text_builder *const builder)
{
	parsed_lvm2_text_builder_stack_pop(builder);
}

static int parsed_lvm2_text_builder_array_element(
		struct parsed_lvm2_text_builder *const builder,
		const char *const element_name, const int element_name_len)
{
	int err;
	struct lvm2_dom_array *stack_top;
	struct lvm2_dom_value *dom_value;

	if(!builder->stack) {
		LogError("No stack in place.");
		return EINVAL;
	}
	else if(builder->stack->obj->type != LVM2_DOM_TYPE_ARRAY) {
		LogError("Top stack element is not of type array.");
		return EINVAL;
	}

	stack_top = (struct lvm2_dom_array*) builder->stack->obj;

	LogDebug("Got array element: \"%.*s\"",
		element_name_len, element_name);

	err = lvm2_dom_value_create("", 0, element_name, element_name_len,
		&dom_value);
	if(err) {
		LogError("Error while creating lvm2_dom_value: %d", err);
	}
	else {
		err = lvm2_dom_array_add_element(stack_top, dom_value);
		if(err) {
			LogError("Error while adding array element: %d", err);
		}
	}

	return err;
}

static int parsed_lvm2_text_builder_section_element(
		struct parsed_lvm2_text_builder *const builder,
		const char *const key_string, const int key_string_len,
		const char *const value_string, const int value_string_len)
{
	int err;
	struct lvm2_dom_section *stack_top;
	struct lvm2_dom_value *dom_value;

	if(!builder->stack) {
		LogError("No stack in place.");
		return EINVAL;
	}
	else if(builder->stack->obj->type != LVM2_DOM_TYPE_SECTION) {
		LogError("Top stack element is not of type section.");
		return EINVAL;
	}

	stack_top = (struct lvm2_dom_section*) builder->stack->obj;

	LogDebug("Got dictionary entry: \"%.*s\" = \"%.*s\"",
		key_string_len, key_string, value_string_len, value_string);

	err = lvm2_dom_value_create(key_string, key_string_len, value_string,
		value_string_len, &dom_value);
	if(err) {
		LogError("Error while creating lvm2_dom_value: %d", err);
	}
	else {
		err = lvm2_dom_section_add_child(stack_top,
			&dom_value->obj_super);
		if(err) {
			LogError("Error while adding dictionary element: %d",
				err);
		}
	}

	return err;
}

static const char tokens[] = {
	'{', '}', '[', ']', '=', '#', ','
};

static const char whitespace[] = {
	' ', '\t', '\n', '\r'
};

static lvm2_bool containsChar(const char *set, size_t setSize, char theChar)
{
	size_t i;

	for(i = 0; i < setSize; ++i) {
		if(set[i] == theChar)
			return LVM2_TRUE;
	}

	return LVM2_FALSE;
}

static size_t nextToken(const char *const text, const size_t textLen,
		const char **const outToken, int *const outTokenLen)
{
	lvm2_bool quotedString = LVM2_FALSE;
	size_t i;
	size_t tokenStart = 0;
	size_t tokenLen = 0;

	/*LogDebug("nextToken iterating over %" FMTzu " characters...",
		ARGzu(textLen));*/
	for(i = 0; i < textLen; ++i) {
		char curChar = text[i];

		//LogDebug("%" FMTzu ":'%c'", ARGzu(i), curChar);

		if(!quotedString && containsChar(whitespace,
			sizeof(whitespace) / sizeof(char), (int) curChar))
		{
			if(tokenLen) {
				/* End of token. */
				break;
			}
			else {
				/* Whitespace before start of token. */
				continue;
			}
		}
		else if(!quotedString && containsChar(tokens,
			sizeof(tokens) / sizeof(char), curChar))
		{
			if(tokenLen) {
				/* End of token. (Start of new reserved
				 * token.) */
				break;
			}
			else {
				/* Reserved token. Return immediately. */
				tokenStart = i;
				tokenLen = 1;
				++i;
				break;
			}
		}
		else if(curChar == '\"') {
			if(quotedString) {
				/* End of token. */

				/* Consume the last '\"'. */
				++i;

				break;
			}
			else if(tokenLen) {
				/* End of token. */
				break;
			}
			else if(!quotedString) {
				quotedString = LVM2_TRUE;
				continue;
			}
		}

		if(!tokenLen)
			tokenStart = i;

		++tokenLen;
	}

	if(outToken)
		*outToken = i >= textLen ? NULL : &text[tokenStart];
	if(outTokenLen)
		*outTokenLen = tokenLen > INT_MAX ? INT_MAX : (int) tokenLen;

	return i;
}

static lvm2_bool parseArray(const char *const text, const size_t textLen,
		struct parsed_lvm2_text_builder *const builder,
		size_t *const bytesProcessed)
{
	lvm2_bool res = LVM2_TRUE;
	size_t i = 0;
	size_t j = 0;

	for(; i < textLen;) {
		const char *token = NULL;
		int tokenLen;

		i += nextToken(&text[i], textLen - i,
			&token, &tokenLen);
		if(!token) {
			LogError("End of text inside "
				 "array.");
			res = LVM2_FALSE;
			break;
		}

		if(tokenLen == 1 &&
			token[0] == ']')
		{
			/* End of array. */
			break;
		}
		else if(!(j++ % 2)) {
			/* Array element. */
			
			parsed_lvm2_text_builder_array_element(builder,
				token, tokenLen);
		}
		else if(tokenLen == 1 &&
			token[0] == ',')
		{
			/* Another array element coming
			 * up. */
		}
		else {
			LogError("Unexpected token "
				"inside array: '%.*s'",
				tokenLen, token);
			res = LVM2_FALSE;
			break;
		}
	}

	*bytesProcessed = i;

	return res;
}

static lvm2_bool parseDictionary(const char *const text, const size_t textLen,
		struct parsed_lvm2_text_builder *const builder,
		const lvm2_bool isRoot, const u32 depth,
		size_t *const outBytesProcessed)
{
	size_t i;
	//lvm2_bool withinArray = LVM2_FALSE;
	//UInt32 level = 0;
	char prefix[5];

	LogTrace("%s: Entering with text=%p textLen=%" FMTzu " builder=%p "
		"isRoot=%d depth=%" FMTlu " outBytesProcessed=%p.",
		__FUNCTION__, text, ARGzu(textLen), builder, isRoot,
		ARGlu(depth), outBytesProcessed);

	if(depth > 4) {
		LogError("Hit dictionary depth limit.");
		return LVM2_FALSE;
	}

	for(i = 0; i < depth; ++i) {
		prefix[i] = '\t';
	}
	prefix[i] = '\0';

	for(i = 0; i < textLen;) {
		const char *token = NULL;
		int tokenLen = 0;

		while(i < textLen) {
			i += nextToken(&text[i], textLen - i, &token,
				&tokenLen);
			if(!token || i >= textLen) {
				/* End of text. */
				break;
			}
			else if(tokenLen == 1 && token[0] == '#') {
				/* Ignore all text until next newline. */
				for(; i < textLen; ++i) {
					if(text[i] == '\r' || text[i] == '\n')
						break;
				}

				continue;
			}
			else if(!isRoot && tokenLen == 1 && token[0] == '}') {
				/* End of dictionary. */
				token = NULL;
				break;
			}
			else if(tokenLen == 1 && containsChar(tokens,
				sizeof(tokens) / sizeof(char), token[0]))
			{
				LogError("Expected identifier. Got: '%c'",
					token[0]);
				return LVM2_FALSE;
			}

			break;
		}

		if(!token || i >= textLen) {
			/* End of text. */
			break;
		}

		const char *identifierToken;
		int identifierTokenLen;
		identifierToken = token;
		identifierTokenLen = tokenLen;

		/*LogDebug("Got identifier: \"%.*s\" (len: %d)",
			(int) identifierTokenLen, identifierToken,
			identifierTokenLen);*/

		i += nextToken(&text[i], textLen - i, &token, &tokenLen);
		if(!token || i >= textLen) {
			/* End of text. */
			LogError("Unexpected end of text inside statement.");
			return LVM2_FALSE;
		}

		if(tokenLen == 1 && token[0] == '{') {
			int err;
			size_t bytesProcessed;

			LogDebug("%s[Depth: %" FMTlu "] \"%.*s\" = {",
				prefix, ARGlu(depth), identifierTokenLen,
				identifierToken);

			err = parsed_lvm2_text_builder_enter_section(builder,
				identifierToken, identifierTokenLen);
			if(err) {
				return LVM2_FALSE;
			}

			if(!parseDictionary(&text[i], textLen - i, builder,
				LVM2_FALSE, depth + 1, &bytesProcessed))
			{
				return LVM2_FALSE;
			}
			
			parsed_lvm2_text_builder_leave_section(builder);

			i += bytesProcessed;

			LogDebug("%s[Depth: %" FMTlu "] }",
				prefix, ARGlu(depth));
		}
		else if(tokenLen == 1 && token[0] == '=') {
			i += nextToken(&text[i], textLen - i,
				&token, &tokenLen);
			if(!token || i >= textLen) {
				/* End of text. */
				LogError("Unexpected end of text "
					"inside statement.");
				return LVM2_FALSE;
			}
			else if(tokenLen == 1 && token[0] == '[') {
				/* We have an array value. */
				size_t bytesProcessed;
				int err;

				LogDebug("%s[Depth: %" FMTlu "] \"%.*s\" = [",
					prefix, ARGlu(depth),
					identifierTokenLen, identifierToken);

				err = parsed_lvm2_text_builder_enter_array(
					builder, identifierToken,
					identifierTokenLen);
				if(err) {
					return LVM2_FALSE;
				}

				if(!parseArray(&text[i], textLen - i,
					builder, &bytesProcessed))
				{
					return LVM2_FALSE;
				}

				err = parsed_lvm2_text_builder_leave_array(
					builder);
				if(err) {
					return LVM2_FALSE;
				}

				i += bytesProcessed;

				LogDebug("%s[Depth: %" FMTlu "] ]",
					prefix, ARGlu(depth));
			}
			else if(tokenLen == 1 && containsChar(tokens,
				sizeof(tokens) / sizeof(char), token[0]))
			{
				LogError("Expected value. Found: '%c'",
					token[0]);
			}
			else {
				/* We have a plain value. */
				int err;
				const char *valueToken = token;
				int valueTokenLen = tokenLen;

				LogDebug("%s[Depth: %" FMTlu "] \"%.*s\" = "
					"\"%.*s\"",
					prefix, ARGlu(depth),
					identifierTokenLen, identifierToken,
					valueTokenLen, valueToken);

				err = parsed_lvm2_text_builder_section_element(
					builder, identifierToken,
					identifierTokenLen, valueToken,
					valueTokenLen);
				if(err) {
					return LVM2_FALSE;
				}
			}
		}
		else {
			LogError("Expected '=' or '{' after identifier. Got: "
				"\"%.*s\"",
				tokenLen, token);
			return LVM2_FALSE;
		}
	}

	if(outBytesProcessed)
		*outBytesProcessed = i;

	return LVM2_TRUE;
}


LVM2_EXPORT lvm2_bool lvm2_parse_text(const char *const text,
		const size_t text_len,
		struct lvm2_dom_section **const out_result)
{
	int res2;
	lvm2_bool res;
	struct lvm2_dom_section *result;
	struct parsed_lvm2_text_builder builder;

	parsed_lvm2_text_builder_init(&builder);
	res2 = parsed_lvm2_text_builder_enter_section(&builder, "", 0);
	if(res2) {
		LogError("Error in parsed_lvm2_text_builder_enter_section: %d",
			res2);
		return LVM2_FALSE;
	}

	res = parseDictionary(text, text_len, &builder, LVM2_TRUE, 0, NULL);

	parsed_lvm2_text_builder_leave_section(&builder);
	result = parsed_lvm2_text_builder_finalize(&builder);

	if(!res) {
		if(result)
			lvm2_dom_section_destroy(&result, LVM2_TRUE);
	}
	else if(!result) {
		LogError("Unexpected: result == NULL after successfully "
			"parsing dictionary.");
		res = LVM2_FALSE;
	}
	else
		*out_result = result;

	return res;
}

static int lvm2_parse_u64_value(const char *const string_value,
		const int string_value_len, u64 *const out_value)
{
	int i;
	u64 result = 0;

	if(string_value_len > 19)
		return EOVERFLOW;

	/*LogDebug("%s parsing string value \"%.*s\" to u64...",
		__FUNCTION__, string_value_len, string_value);*/

	i = string_value_len;

	for(i = 0; i < string_value_len; ++i) {
		int j;
		char cur_char =
			string_value[string_value_len - 1 - i];
		char cur_digit;
		u64 cur_value;

		if(cur_char < '0' || cur_char > '9') {
			LogError("Invalid character in numeric string: '%c'",
				cur_char);
			return EINVAL;
		}
		/*LogDebug("\t[%d] cur_char='%c'", i, cur_char);*/

		cur_digit = cur_char - '0';
		/*LogDebug("\t[%d] cur_digit=%d", i, cur_digit);*/

		if(i == 18 && cur_digit > 1)
			return EOVERFLOW;

		cur_value = (u64) cur_digit;
		for(j = 0; j < i; ++j)
			cur_value *= 10;
		/*LogDebug("\t[%d] cur_value=%" FMTllu, i, ARGllu(cur_value));*/

		if(result > 0xFFFFFFFFFFFFFFFFULL - cur_value)
			return EOVERFLOW;

		result += cur_value;
	};

	/* LogDebug("\tfinal result: %" FMTllu, ARGllu(result)); */

	*out_value = result;

	return 0;
}

static int lvm2_layout_parse_u64_value(const struct lvm2_dom_value *value,
		u64 *out_value)
{
	return lvm2_parse_u64_value(value->value->content,
		value->value->length, out_value);
}

#if 0
static const struct lvm2_dom_obj* lvm2_dom_tree_lookup(
		const struct lvm2_dom_section *const root_section,
		const char **const path)
{
	const struct lvm2_dom_obj *cur_obj = &root_section->obj_super;
	size_t i;

	for(i = 0; path[i] != NULL; ++i) {
		const char *const cur_elem = path[i];
		const size_t cur_elem_len = strlen(path[i]);
		size_t j;
		const struct lvm2_dom_section *cur_section = NULL;
		const struct lvm2_dom_obj *match = NULL;

		if(cur_obj->type != LVM2_DOM_TYPE_SECTION) {
			LogError("Intermediate element of non-section type in "
				"lookup.");
			return NULL;
		}
		else if(cur_elem_len > INT_MAX) {
			LogError("'cur_elem_len' overflows: %" FMTzu,
				ARGzu(cur_elem_len));
			return NULL;
		}

		cur_section = (struct lvm2_dom_section*) cur_obj;

		for(j = 0; j < cur_section->children_len; ++j) {
			const struct lvm2_dom_obj *const child =
				cur_section->children[j];
			if(child->name->length == (int) cur_elem_len &&
				!memcmp(child->name->content, cur_elem,
				cur_elem_len))
			{
				match = child;
				break;
			}
		}

		if(!match) {
			LogError("No match for \"%.*s\".",
				(int) cur_elem_len, cur_elem);
			return NULL;
		}

		cur_obj = match;
	}

	return cur_obj;
}

static int lvm2_dom_tree_lookup_string(
		const struct lvm2_dom_section *const root_section,
		const char **const path,
		struct lvm2_bounded_string **const out_result)
{
	int err;
	const struct lvm2_dom_obj *dom_obj = NULL;
	const struct lvm2_bounded_string *dom_string = NULL;
	struct lvm2_bounded_string *result = NULL;

	dom_obj = lvm2_dom_tree_lookup(root_section, path);
	if(!dom_obj) {
		LogError("Could not find 'id' section.");
		return ENOENT;
	}
	else if(dom_obj->type != LVM2_DOM_TYPE_VALUE) {
		LogError("Non-value type.");
		return ENOENT;;
	}

	dom_string = ((struct lvm2_dom_value*) dom_obj)->value;

	err = lvm2_bounded_string_dup(dom_string, &result);
	if(err) {
		LogError("Error while duplicating bounded string.", err);
		return err;
	}

	*out_result = result;

	return 0;
}

static int lvm2_dom_tree_lookup_u64(
		const struct lvm2_dom_section *const root_section,
		const char **const path, u64 *const out_result)
{
	const struct lvm2_dom_obj *dom_obj = NULL;
	int err;

	dom_obj = lvm2_dom_tree_lookup(root_section, path);
	if(!dom_obj) {
		LogError("Could not find 'id' section.");
		return ENOENT;
	}
	else if(dom_obj->type != LVM2_DOM_TYPE_VALUE) {
		LogError("Non-value type.");
		return ENOENT;;
	}
	
	err = lvm2_layout_parse_u64_value((struct lvm2_dom_value*) dom_obj,
		out_result);
	if(err) {
		LogError("Error while parsing integer value: %d", err);
		return err;
	}

	return 0;
}
#endif

static int lvm2_pv_location_create(
		const struct lvm2_bounded_string *const pv_name,
		const u64 extent_start,
		struct lvm2_pv_location **const out_stripe)
{
	int err;
	struct lvm2_pv_location *stripe = NULL;
	struct lvm2_bounded_string *dup_pv_name = NULL;

	err = lvm2_malloc(sizeof(struct lvm2_pv_location), (void**) &stripe);
	if(err) {
		LogError("Error while allocating memory for struct "
			"lvm2_pv_location: %d", err);
	}
	else if((err = lvm2_bounded_string_dup(pv_name, &dup_pv_name)) != 0) {
		LogError("Error while duplicating bounded string: %d", err);
	}

	if(err) {
		if(dup_pv_name)
			lvm2_bounded_string_destroy(&dup_pv_name);
		if(stripe)
			lvm2_free((void**) &stripe,
				sizeof(struct lvm2_pv_location));
	}
	else {
		stripe->pv_name = dup_pv_name;
		stripe->extent_start = extent_start;

		*out_stripe = stripe;
	}

	return err;
}

static void lvm2_pv_location_destroy(struct lvm2_pv_location **const stripe)
{
	lvm2_bounded_string_destroy(&(*stripe)->pv_name);

	lvm2_free((void**) stripe, sizeof(struct lvm2_pv_location));
}

static int lvm2_segment_create(
		const struct lvm2_dom_section *const segment_section,
		struct lvm2_segment **const out_segment)
{
	int err;
	size_t i;
	struct lvm2_segment *segment = NULL;

	lvm2_bool start_extent_defined = LVM2_FALSE;
	u64 start_extent = 0;

	lvm2_bool extent_count_defined = LVM2_FALSE;
	u64 extent_count = 0;

	struct lvm2_bounded_string *type = NULL;

	/* Variables only existing in striped or plain (stripe_count = 1)
	 * volumes. */

	lvm2_bool stripe_count_defined = LVM2_FALSE;
	u64 stripe_count = 0;

	lvm2_bool stripe_size_defined = LVM2_FALSE;
	u64 stripe_size = 0;

	size_t stripes_len = 0;
	struct lvm2_pv_location **stripes = NULL;

	/* Variables only existing in mirrored volumes. */

	lvm2_bool mirror_count_defined = LVM2_FALSE;
	u64 mirror_count = 0;

	struct lvm2_bounded_string *mirror_log = NULL;

	lvm2_bool region_size_defined = LVM2_FALSE;
	u64 region_size = 0;

	size_t mirrors_len = 0;
	struct lvm2_pv_location **mirrors = NULL;

	for(i = 0; i < segment_section->children_len; ++i) {
		const struct lvm2_dom_obj *const cur_obj =
			segment_section->children[i];
		const struct lvm2_bounded_string *const name = cur_obj->name;

		if(cur_obj->type == LVM2_DOM_TYPE_VALUE) {
			const struct lvm2_dom_value *const value =
				(struct lvm2_dom_value*) cur_obj;

			if(!strncmp(name->content, "start_extent",
				name->length))
			{
				if(start_extent_defined) {
					LogError("Duplicate definition of "
						"'start_extent'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&start_extent);
				if(err)
					break;

				start_extent_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "extent_count",
				name->length))
			{
				if(extent_count_defined) {
					LogError("Duplicate definition of "
						"'extent_count'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&extent_count);
				if(err)
					break;

				extent_count_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "type", name->length)) {
				if(type) {
					LogError("Duplicate definition of "
						"'type'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&type);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "stripe_count",
				name->length))
			{
				if(stripe_count_defined) {
					LogError("Duplicate definition of "
						"'stripe_count'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&stripe_count);
				if(err)
					break;

				stripe_count_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "stripe_size",
				name->length))
			{
				if(stripe_size_defined) {
					LogError("Duplicate definition of "
						"'stripe_size'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&stripe_size);
				if(err) {
					LogError("Error while parsing value of "
						"'stripe_size' as u64'.");
					break;
				}

				stripe_size_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "mirror_count",
				name->length))
			{
				if(mirror_count_defined) {
					LogError("Duplicate definition of "
						"'mirror_count'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&mirror_count);
				if(err) {
					LogError("Error while parsing value of "
						"'mirror_count' as u64'.");
					break;
				}

				mirror_count_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "mirror_log",
				name->length))
			{
				if(mirror_log) {
					LogError("Duplicate definition of "
						"'mirror_log'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&mirror_log);
				if(err) {
					break;
				}
			}
			else if(!strncmp(name->content, "region_size",
				name->length))
			{
				if(region_size_defined) {
					LogError("Duplicate definition of "
						"'region_size'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&region_size);
				if(err) {
					LogError("Error while parsing value of "
						"'region_size' as u64'.");
					break;
				}

				region_size_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized value-type member in "
					"lvm2_segment: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(cur_obj->type == LVM2_DOM_TYPE_ARRAY) {
			struct lvm2_dom_array *const array =
				(struct lvm2_dom_array*) cur_obj;

			struct lvm2_pv_location ***location_array_ptr = NULL;
			size_t *location_array_length_ptr = NULL;
			const char *location_array_name = "";

			if(!strncmp(name->content, "stripes", name->length)) {
				location_array_ptr = &stripes;
				location_array_length_ptr = &stripes_len;
				location_array_name = "stripes";
			}
			else if(!strncmp(name->content, "mirrors",
				name->length))
			{
				location_array_ptr = &mirrors;
				location_array_length_ptr = &mirrors_len;
				location_array_name = "mirrors";
			}
			else {
				LogError("Unrecognized array-type member in "
					 "lvm2_segment: '%.*s'",
					 name->length, name->content);
				err = EINVAL;
				break;
			}

			if(location_array_ptr && location_array_length_ptr) {
				size_t j;
				struct lvm2_pv_location **new_location_array =
					NULL;
				size_t new_location_array_len;

				if(*location_array_ptr) {
					LogError("Duplicate definition of "
						"'%s'.", location_array_name);
					err = EINVAL;
					break;
				}

				if(array->elements_len % 2 != 0) {
					LogError("Uneven '%s' array length: "
						"%" FMTzu,
						location_array_name,
						ARGzu(array->elements_len));
					err = EINVAL;
					break;
				}

				new_location_array_len =
					array->elements_len / 2;

				err = lvm2_malloc(new_location_array_len *
					sizeof(struct lvm2_pv_location*),
					(void**) &new_location_array);
				if(err) {
					LogError("Error while allocating "
						"memory for "
						"'new_location_array' array.");
					break;
				}

				memset(new_location_array, 0,
					new_location_array_len *
					sizeof(struct lvm2_pv_location*));

				for(j = 0; j < new_location_array_len; ++j) {
					const struct lvm2_dom_value
						*const pv_name_obj =
						array->elements[j];
					const struct lvm2_dom_value
						*const extent_start_obj =
						array->elements[j+1];

					const struct lvm2_bounded_string
						*const pv_name =
						pv_name_obj->value;
					u64 extent_start = 0;

					struct lvm2_pv_location *location =
						NULL;

					err = lvm2_layout_parse_u64_value(
						extent_start_obj,
						&extent_start);
					if(err) {
						LogError("Error while parsing "
							"u64 value: %d", err);
						break;
					}

					err = lvm2_pv_location_create(pv_name,
						extent_start, &location);
					if(err) {
						LogError("Error while creating "
							"'lvm2_pv_location': "
							"%d", err);
						break;
					}

					new_location_array[j] = location;
				}

				if(err) {
					for(j = 0; j < new_location_array_len;
						++j)
					{
						if(new_location_array[j]) {
							lvm2_pv_location_destroy(
								&new_location_array[j]
							);
						}
					}

					lvm2_free((void**) &new_location_array,
						new_location_array_len *
						sizeof(struct
						lvm2_pv_location*));
				}
				else {
					*location_array_ptr =
						new_location_array;
					*location_array_length_ptr =
						new_location_array_len;
				}
			}
		}
		else if(cur_obj->type == LVM2_DOM_TYPE_SECTION) {
			if(0) {
				/* No known section-type member in segment. */
			}
			else {
				LogError("Unrecognized section-type member in "
					"lvm2_segment: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else {
			LogError("Unrecognized child type: %d", cur_obj->type);
			err = EINVAL;
			break;
		}
	}

	if(!err) {
		if(!start_extent_defined ||
			!extent_count_defined ||
			!type)
		{
			LogError("Missing members in lvm2_segment:%s%s%s",
				!start_extent_defined ? " start_extent" : "",
				!extent_count_defined ? " extent_count" : "",
				!type ? " type" : "");
			err = EINVAL;
		}
	}

	if(!err) {
		err = lvm2_malloc(sizeof(struct lvm2_segment),
			(void**) &segment);
		if(err) {
			LogError("Error while allocating memory for struct "
				"lvm2_segment: %d",
				err);
		}
		else
			memset(segment, 0, sizeof(struct lvm2_segment));
	}

	if(!err) {
		segment->start_extent = start_extent;

		segment->extent_count = extent_count;

		segment->type = type;

		segment->stripe_count_defined = stripe_count_defined;
		segment->stripe_count = stripe_count_defined ? stripe_count : 0;

		segment->stripe_size_defined = stripe_size_defined;
		segment->stripe_size = stripe_size_defined ? stripe_size : 0;

		segment->stripes_len = stripes ? stripes_len : 0;
		segment->stripes = stripes;

		segment->mirror_count_defined = mirror_count_defined;
		segment->mirror_count = mirror_count_defined ? mirror_count : 0;

		segment->mirror_log = mirror_log;

		segment->region_size_defined = region_size_defined;
		segment->region_size = region_size_defined ? region_size : 0;

		segment->mirrors_len = mirrors ? mirrors_len : 0;
		segment->mirrors = mirrors;

		*out_segment = segment;
	}
	else {
		if(segment)
			lvm2_free((void**) &segment,
				sizeof(struct lvm2_segment));

		if(mirrors) {
			size_t j;

			for(j = 0; j < mirrors_len; ++j) {
				lvm2_pv_location_destroy(&mirrors[j]);
			}

			mirrors_len = 0;
			lvm2_free((void**) &mirrors,
				mirrors_len * sizeof(struct lvm2_pv_location*));
		}

		if(mirror_log) {
			lvm2_bounded_string_destroy(&mirror_log);
		}

		if(stripes) {
			size_t j;

			for(j = 0; j < stripes_len; ++j) {
				lvm2_pv_location_destroy(&stripes[j]);
			}

			stripes_len = 0;
			lvm2_free((void**) &stripes,
				stripes_len * sizeof(struct lvm2_pv_location*));
		}
		if(type)
			lvm2_bounded_string_destroy(&type);
	}

	return err;
}

static void lvm2_segment_destroy(struct lvm2_segment **const segment)
{
	size_t i;

	if((*segment)->mirrors) {
		size_t j;

		for(j = 0; j < (*segment)->mirrors_len; ++j) {
			lvm2_pv_location_destroy(&(*segment)->mirrors[j]);
		}

		lvm2_free((void**) &(*segment)->mirrors,
			(*segment)->mirrors_len *
			sizeof(struct lvm2_pv_location*));
		(*segment)->mirrors_len = 0;
	}

	if((*segment)->mirror_log) {
		lvm2_bounded_string_destroy(&(*segment)->mirror_log);
	}

	if((*segment)->stripes) {
		for(i = 0; i < (*segment)->stripes_len; ++i) {
			lvm2_pv_location_destroy(&(*segment)->stripes[i]);
		}

		lvm2_free((void**) &(*segment)->stripes,
			  (*segment)->stripes_len *
			  sizeof(struct lvm2_pv_location*));
		(*segment)->stripes_len = 0;
	}

	lvm2_bounded_string_destroy(&(*segment)->type);

	lvm2_free((void**) segment, sizeof(struct lvm2_segment));
}

static int lvm2_logical_volume_create(
		const struct lvm2_bounded_string *const lv_name,
		const struct lvm2_dom_section *const lv_section,
		struct lvm2_logical_volume **const out_lv)
{
	int err;
	size_t i;
	struct lvm2_logical_volume *lv = NULL;

	struct lvm2_bounded_string *lv_name_dup = NULL;

	struct lvm2_bounded_string *id = NULL;

	lvm2_bool status_defined = LVM2_FALSE;
	lvm2_logical_volume_status status = 0;

	lvm2_bool flags_defined = LVM2_FALSE;
	lvm2_logical_volume_flags flags = 0;

	struct lvm2_bounded_string *creation_host = NULL;

	struct lvm2_bounded_string *creation_time = NULL;

	struct lvm2_bounded_string *allocation_policy = NULL;

	lvm2_bool segment_count_defined = LVM2_FALSE;
	u64 segment_count = 0;

	size_t segments_len = 0;
	struct lvm2_segment **segments = NULL;

	for(i = 0; i < lv_section->children_len; ++i) {
		const struct lvm2_dom_obj *const cur_obj =
			lv_section->children[i];
		const lvm2_dom_type type = cur_obj->type;
		const struct lvm2_bounded_string *const name = cur_obj->name;

		if(type == LVM2_DOM_TYPE_VALUE) {
			const struct lvm2_dom_value *const value =
				(struct lvm2_dom_value*) cur_obj;

			if(!strncmp(name->content, "id", name->length)) {
				if(id) {
					LogError("Duplicate definition of "
						"'id'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&id);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "creation_host",
				name->length))
			{
				if(creation_host) {
					LogError("Duplicate definition of "
						"'creation_host'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&creation_host);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "creation_time",
				name->length))
			{
				if(creation_time) {
					LogError("Duplicate definition of "
						"'creation_time'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&creation_time);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "allocation_policy",
				name->length))
			{
				if(allocation_policy) {
					LogError("Duplicate definition of "
						"'allocation_policy'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&allocation_policy);
				if(err) {
					break;
				}
			}
			else if(!strncmp(name->content, "segment_count",
				name->length))
			{
				if(segment_count_defined) {
					LogError("Duplicate definition of "
						"'segment_count'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&segment_count);
				if(err)
					break;

				segment_count_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized value-type member in "
					"lvm2_logical_volume: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(type == LVM2_DOM_TYPE_ARRAY) {
			const struct lvm2_dom_array *const array =
				(struct lvm2_dom_array*) cur_obj;

			if(!strncmp(name->content, "status", name->length)) {
				size_t j;

				if(status_defined) {
					LogError("Duplicate definition of "
						"'status'.");
					err = EINVAL;
					break;
				}

				status = 0;
				for(j = 0; j < array->elements_len; ++j) {
					char *value = array->elements[j]->
						value->content;
					int value_len = array->elements[j]->
						value->length;

					if(!strncmp("READ", value,
						value_len))
					{
						status |= LVM2_LOGICAL_VOLUME_STATUS_READ;
					}
					else if(!strncmp("WRITE", value,
						value_len))
					{
						status |= LVM2_LOGICAL_VOLUME_STATUS_WRITE;
					}
					else if(!strncmp("VISIBLE", value,
						value_len))
					{
						status |= LVM2_LOGICAL_VOLUME_STATUS_VISIBLE;
					}
					else {
						LogError("Unrecognized value "
							"in 'status' array: "
							"'%.*s'",
							value_len, value);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;

				status_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "flags", name->length))
			{
				size_t j;

				if(flags_defined) {
					LogError("Duplicate definition of "
						"'flags'.");
					err = EINVAL;
					break;
				}

				flags = 0;

				for(j = 0; j < array->elements_len; ++j) {
					char *value = array->elements[j]->
						value->content;
					int value_len = array->elements[j]->
						value->length;

					if(0) {
						/* Currently we don't have any
						 * known values for 'flags'.
						 * Read the LVM2 source. */
					}
					else {
						LogError("Unrecognized value "
							"in 'flags' array: "
							"'%.*s'",
							value_len, value);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;

				flags_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized array-type member in "
					"lvm2_logical_volume: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(type == LVM2_DOM_TYPE_SECTION) {
			struct lvm2_dom_section *const section =
				(struct lvm2_dom_section*) cur_obj;
			u64 segment_number;

			if(name->length > 7 &&
				!strncmp(name->content, "segment", 7) &&
				!(err = lvm2_parse_u64_value(&name->content[7],
				name->length - 7, &segment_number)) &&
				segment_number == (segments_len + 1))
			{
				/* Note that we assume that segments come in
				 * order. Otherwise we refuse to parse. This can
				 * be improved if needed, but I don't think it
				 * will be necessary. */

				/* Expand 'segments' array. */
				struct lvm2_segment **new_segments = NULL;
				size_t new_segments_len = segments_len + 1;

				struct lvm2_segment **old_segments = segments;
				size_t old_segments_len = segments_len;

				struct lvm2_segment *new_segment = NULL;

				err = lvm2_malloc(new_segments_len *
					sizeof(struct lvm2_segment*),
					(void**) &new_segments);
				if(err)
					break;

				if(old_segments && old_segments_len) {
					memcpy(new_segments, old_segments,
						old_segments_len *
						sizeof(struct lvm2_segment*));
				}

				err = lvm2_segment_create(section,
					&new_segment);
				if(err) {
					/* Clean up new_segments allocation. */
					lvm2_free((void**) &new_segments,
						new_segments_len *
						sizeof(struct lvm2_segment*));
					break;
				}

				new_segments[old_segments_len] = new_segment;

				segments = new_segments;
				segments_len = new_segments_len;

				if(old_segments) {
					lvm2_free((void**) &old_segments,
						old_segments_len *
						sizeof(struct lvm2_segment*));
				}
			}
			else {
				LogError("Unrecognized array-type member in "
					"lvm2_logical_volume: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else {
			LogError("Unrecognized child type: %d", type);
			err = EINVAL;
			break;
		}
	}

	if(!err) {
		err = lvm2_bounded_string_dup(lv_name, &lv_name_dup);
		if(err) {
			LogError("Error while duplicating string: %d", err);
		}
	}

	if(!err) {
		if(!lv_name_dup ||
			!id ||
			!status_defined ||
			!segment_count_defined ||
			!segments_len ||
			!segments)
		{
			LogError("Missing members in lvm2_logical_volume:%s%s%s"
				"%s%s%s",
				!lv_name_dup ? " lv_name_dup" : "",
				!id ? " id" : "",
				!status_defined ? " status" : "",
				!segment_count_defined ? " segment_count" : "",
				!segments_len ? " segments_len" : "",
				!segments ? " segments" : "");
			err = EINVAL;
		}
	}

	if(!err) {
		if(segment_count != segments_len) {
			LogError("'segment_count' doesn't match the actual "
				"number of segments.");
			err = EINVAL;
		}
	}

	if(!err) {
		err = lvm2_malloc(sizeof(struct lvm2_logical_volume),
			(void**) &lv);
		if(err) {
			LogError("Error while allocating memory for struct "
				"lvm2_logical_volume: %d",
				err);
		}
		else
			memset(lv, 0, sizeof(struct lvm2_logical_volume));
	}

	if(!err) {
		lv->name = lv_name_dup;
		lv->id = id;
		lv->status = status;
		lv->flags_defined = flags_defined;
		if(flags_defined) {
			lv->flags = flags;
		}
		else {
			lv->flags = 0;
		}
		lv->creation_host = creation_host;
		lv->creation_time = creation_time;
		lv->allocation_policy = allocation_policy;
		lv->segment_count = segment_count;
		lv->segments_len = segments_len;
		lv->segments = segments;

		*out_lv = lv;
	}
	else {
		if(lv)
			lvm2_free((void**) &lv,
				sizeof(struct lvm2_logical_volume));

		if(lv_name_dup)
			lvm2_bounded_string_destroy(&lv_name_dup);

		if(segments_len) {
			size_t j;

			for(j = 0; j < segments_len; ++j) {
				lvm2_segment_destroy(&segments[j]);
			}

			lvm2_free((void**) &segments,
				segments_len * sizeof(struct lvm2_segment*));
		}

		if(allocation_policy) {
			lvm2_bounded_string_destroy(&allocation_policy);
		}

		if(creation_time) {
			lvm2_bounded_string_destroy(&creation_time);
		}

		if(creation_host) {
			lvm2_bounded_string_destroy(&creation_host);
		}

		if(id)
			lvm2_bounded_string_destroy(&id);
	}

	return err;
}

static void lvm2_logical_volume_destroy(struct lvm2_logical_volume **lv)
{
	lvm2_bounded_string_destroy(&(*lv)->name);

	if(&(*lv)->segments_len) {
		size_t i;

		for(i = 0; i < (*lv)->segments_len; ++i) {
			lvm2_segment_destroy(&(*lv)->segments[i]);
		}

		lvm2_free((void**) &(*lv)->segments,
			(*lv)->segments_len * sizeof(struct lvm2_segment*));
	}

	if((*lv)->allocation_policy) {
		lvm2_bounded_string_destroy(&(*lv)->allocation_policy);
	}

	if((*lv)->creation_time) {
		lvm2_bounded_string_destroy(&(*lv)->creation_time);
	}

	if((*lv)->creation_host) {
		lvm2_bounded_string_destroy(&(*lv)->creation_host);
	}

	lvm2_bounded_string_destroy(&(*lv)->id);

	lvm2_free((void**) lv, sizeof(struct lvm2_logical_volume));
}

static int lvm2_physical_volume_create(
		const struct lvm2_bounded_string *const pv_name,
		const struct lvm2_dom_section *const pv_section,
		struct lvm2_physical_volume **const out_pv)
{
	int err;
	size_t i;
	struct lvm2_physical_volume *pv = NULL;

	struct lvm2_bounded_string *pv_name_dup = NULL;

	struct lvm2_bounded_string *id = NULL;

	struct lvm2_bounded_string *device = NULL;

	lvm2_bool status_defined = LVM2_FALSE;
	lvm2_physical_volume_status status = 0;

	lvm2_bool flags_defined = LVM2_FALSE;
	lvm2_physical_volume_flags flags = 0;

	lvm2_bool dev_size_defined = LVM2_FALSE;
	u64 dev_size = 0;

	lvm2_bool pe_start_defined = LVM2_FALSE;
	u64 pe_start = 0;

	lvm2_bool pe_count_defined = LVM2_FALSE;
	u64 pe_count = 0;

	for(i = 0; i < pv_section->children_len; ++i) {
		const struct lvm2_dom_obj *const cur_obj =
			pv_section->children[i];
		const struct lvm2_bounded_string *const name = cur_obj->name;

		if(cur_obj->type == LVM2_DOM_TYPE_VALUE) {
			const struct lvm2_dom_value *const value =
				(struct lvm2_dom_value*) cur_obj;

			if(!strncmp(name->content, "id", name->length)) {
				if(id) {
					LogError("Duplicate definition of "
						"'id'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&id);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "device", name->length))
			{
				if(device) {
					LogError("Duplicate definition of "
						"'device'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&device);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "dev_size",
				name->length))
			{
				if(dev_size_defined) {
					LogError("Duplicate definition of "
						"'dev_size'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&dev_size);
				if(err)
					break;

				dev_size_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "pe_start",
				name->length))
			{
				if(pe_start_defined) {
					LogError("Duplicate definition of "
						"'pe_start'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&pe_start);
				if(err)
					break;

				pe_start_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "pe_count",
				name->length))
			{
				if(pe_count_defined) {
					LogError("Duplicate definition of "
						"'pe_count'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&pe_count);
				if(err)
					break;

				pe_count_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized value-type member in "
					"lvm2_physical_volume: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(cur_obj->type == LVM2_DOM_TYPE_ARRAY) {
			const struct lvm2_dom_array *const array =
				(struct lvm2_dom_array*) cur_obj;

			if(!strncmp(name->content, "status", name->length)) {
				size_t j;

				if(status_defined) {
					LogError("Duplicate definition of "
						"'status'.");
					err = EINVAL;
					break;
				}

				status = 0;
				for(j = 0; j < array->elements_len; ++j) {
					char *value = array->elements[j]->
						value->content;
					int value_len = array->elements[j]->
						value->length;

					if(!strncmp("ALLOCATABLE", value,
						value_len))
					{
						status |= LVM2_PHYSICAL_VOLUME_STATUS_ALLOCATABLE;
					}
					else {
						LogError("Unrecognized value "
							"in 'status' array: "
							"'%.*s'",
							value_len, value);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;

				status_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "flags", name->length))
			{
				size_t j;

				if(flags_defined) {
					LogError("Duplicate definition of "
						"'flags'.");
					err = EINVAL;
					break;
				}

				flags = 0;

				for(j = 0; j < array->elements_len; ++j) {
					char *value = array->elements[j]->
						value->content;
					int value_len = array->elements[j]->
						value->length;

					if(0) {
						/* Currently we don't have any
						 * known values for 'flags'.
						 * Read the LVM2 source. */
					}
					else {
						LogError("Unrecognized value "
							"in 'flags' array: "
							"'%.*s'",
							value_len, value);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;

				flags_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized array-type member in "
					"lvm2_physical_volume: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(cur_obj->type == LVM2_DOM_TYPE_SECTION) {
			LogError("No section-type objects expected in "
				"lvm2_physical_volume.");
			err = EINVAL;
			break;
		}
		else {
			LogError("Unrecognized child type: %d", cur_obj->type);
			err = EINVAL;
			break;
		}
	}

	if(!err) {
		err = lvm2_bounded_string_dup(pv_name, &pv_name_dup);
		if(err) {
			LogError("Error while duplicating string: %d", err);
		}
	}

	if(!err) {
		if(!pv_name_dup ||
			!id ||
			!device ||
			!status_defined ||
			!pe_start_defined ||
			!pe_count_defined)
		{
			LogError("Missing members in lvm2_physical_volume:%s%s"
				"%s%s%s%s",
				!pv_name_dup ? " pv_name_dup" : "",
				!id ? " id" : "",
				!device ? " device" : "",
				!status_defined ? " status" : "",
				!pe_start_defined ? " pe_start" : "",
				!pe_count_defined ? " pe_count" : "");
			err = EINVAL;
		}
	}

	if(!err) {
		err = lvm2_malloc(sizeof(struct lvm2_physical_volume),
			(void**) &pv);
		if(err) {
			LogError("Error while allocating memory for struct "
				"lvm2_physical_volume: %d",
				err);
		}
		else
			memset(pv, 0, sizeof(struct lvm2_physical_volume));
	}

	if(!err) {
		pv->name = pv_name_dup;
		pv->id = id;
		pv->device = device;
		pv->status = status;

		pv->flags_defined = flags_defined;
		if(flags_defined) {
			pv->flags = flags;
		}
		else {
			pv->flags = 0;
		}

		pv->dev_size_defined = dev_size_defined;
		if(dev_size_defined) {
			pv->dev_size = dev_size;
		}
		else {
			pv->dev_size = 0;
		}

		pv->pe_start = pe_start;
		pv->pe_count = pe_count;

		*out_pv = pv;
	}
	else {
		if(pv)
			lvm2_free((void**) &pv,
				sizeof(struct lvm2_physical_volume));
		if(pv_name_dup)
			lvm2_bounded_string_destroy(&pv_name_dup);
		if(device)
			lvm2_bounded_string_destroy(&device);
		if(id)
			lvm2_bounded_string_destroy(&id);
	}

	return err;
}

static void lvm2_physical_volume_destroy(struct lvm2_physical_volume **pv)
{
	lvm2_bounded_string_destroy(&(*pv)->name);
	lvm2_bounded_string_destroy(&(*pv)->device);
	lvm2_bounded_string_destroy(&(*pv)->id);

	lvm2_free((void**) pv, sizeof(struct lvm2_physical_volume));
}

static int lvm2_volume_group_create(
		const struct lvm2_dom_section *const vg_section,
		struct lvm2_volume_group **const out_vg)
{
	int err;
	size_t i;
	struct lvm2_volume_group *vg = NULL;

	struct lvm2_bounded_string *id = NULL;

	lvm2_bool seqno_defined = LVM2_FALSE;
	u64 seqno = 0;

	struct lvm2_bounded_string *format = NULL;

	lvm2_bool status_defined = LVM2_FALSE;
	lvm2_volume_group_status status = 0;

	lvm2_bool flags_defined = LVM2_FALSE;
	lvm2_volume_group_flags flags = 0;

	lvm2_bool extent_size_defined = LVM2_FALSE;
	u64 extent_size = 0;

	lvm2_bool max_lv_defined = LVM2_FALSE;
	u64 max_lv = 0;

	lvm2_bool max_pv_defined = LVM2_FALSE;
	u64 max_pv = 0;

	lvm2_bool metadata_copies_defined = LVM2_FALSE;
	u64 metadata_copies = 0;

	size_t physical_volumes_len = 0;
	struct lvm2_physical_volume **physical_volumes = NULL;

	size_t logical_volumes_len = 0;
	struct lvm2_logical_volume **logical_volumes = NULL;

	for(i = 0; i < vg_section->children_len; ++i) {
		const struct lvm2_dom_obj *const cur_obj =
			vg_section->children[i];
		const lvm2_dom_type type = cur_obj->type;
		const struct lvm2_bounded_string *const name = cur_obj->name;

		if(type == LVM2_DOM_TYPE_VALUE) {
			const struct lvm2_dom_value *const value =
				(struct lvm2_dom_value*) cur_obj;

			if(!strncmp(name->content, "id", name->length)) {
				if(id) {
					LogError("Duplicate definition of "
						"'id'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&id);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "seqno", name->length))
			{
				if(seqno_defined) {
					LogError("Duplicate definition of "
						"'seqno'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&seqno);
				if(err)
					break;

				seqno_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "format", name->length))
			{
				if(format) {
					LogError("Duplicate definition of "
						"'format'.");
					err = EINVAL;
					break;
				}
				else if(value->value->length != 4 ||
					memcmp(value->value->content, "lvm2",
					4))
				{
					LogError("Unrecognized value for key "
						"'format': '%.*s'",
						value->value->length,
						value->value->content);
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&format);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "extent_size",
				name->length))
			{
				if(extent_size_defined) {
					LogError("Duplicate definition of "
						"'extent_size'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&extent_size);
				if(err)
					break;

				extent_size_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "max_lv", name->length))
			{
				if(max_lv_defined) {
					LogError("Duplicate definition of "
						"'max_lv'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&max_lv);
				if(err)
					break;

				max_lv_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "max_pv", name->length))
			{
				if(max_pv_defined) {
					LogError("Duplicate definition of "
						"'max_pv'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&max_pv);
				if(err)
					break;

				max_pv_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "metadata_copies",
					name->length))
			{
				if(metadata_copies_defined) {
					LogError("Duplicate definition of "
						"'metadata_copies'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&metadata_copies);
				if(err)
					break;

				metadata_copies_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized value-type member in "
					"lvm2_volume_group: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(type == LVM2_DOM_TYPE_ARRAY) {
			const struct lvm2_dom_array *const array =
				(struct lvm2_dom_array*) cur_obj;

			if(!strncmp(name->content, "status", name->length)) {
				size_t j;

				if(status_defined) {
					LogError("Duplicate definition of "
						"'status'.");
					err = EINVAL;
					break;
				}

				status = 0;
				for(j = 0; j < array->elements_len; ++j) {
					char *value = array->elements[j]->
						value->content;
					int value_len = array->elements[j]->
						value->length;

					if(!strncmp("RESIZEABLE", value,
						value_len))
					{
						status |= LVM2_VOLUME_GROUP_STATUS_RESIZEABLE;
					}
					else if(!strncmp("READ", value,
						value_len))
					{
						status |= LVM2_VOLUME_GROUP_STATUS_READ;
					}
					else if(!strncmp("WRITE", value,
						value_len))
					{
						status |= LVM2_VOLUME_GROUP_STATUS_WRITE;
					}
					else {
						LogError("Unrecognized value "
							"in 'status' array: "
							"'%.*s'",
							value_len, value);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;

				status_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "flags", name->length))
			{
				size_t j;

				if(flags_defined) {
					LogError("Duplicate definition of "
						"'flags'.");
					err = EINVAL;
					break;
				}

				flags = 0;

				for(j = 0; j < array->elements_len; ++j) {
					char *value = array->elements[j]->
						value->content;
					int value_len = array->elements[j]->
						value->length;

					if(0) {
						/* Currently we don't have any
						 * known values for 'flags'.
						 * Read the LVM2 source. */
					}
					else {
						LogError("Unrecognized value "
							"in 'flags' array: "
							"'%.*s'",
							value_len, value);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;

				flags_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized array-type member in "
					"lvm2_volume_group: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(type == LVM2_DOM_TYPE_SECTION) {
			struct lvm2_dom_section *const section =
				(struct lvm2_dom_section*) cur_obj;

			if(!strncmp(name->content, "physical_volumes",
				name->length))
			{
				size_t j;

				if(physical_volumes) {
					LogError("Duplicate definition of "
						"'physical_volumes'.");
					err = EINVAL;
					break;
				}

				err = lvm2_malloc(section->children_len *
					sizeof(struct lvm2_physical_volume*),
					(void**) &physical_volumes);
				if(err)
					break;

				physical_volumes_len = section->children_len;
				memset(physical_volumes, 0,
					physical_volumes_len *
					sizeof(struct lvm2_physical_volume*));

				for(j = 0; j < section->children_len; ++j) {
					struct lvm2_dom_obj *grandchild_obj =
						section->children[j];

					if(grandchild_obj->type !=
						LVM2_DOM_TYPE_SECTION)
					{
						LogError("Non-section type "
							"member in "
							"'physical_volumes' "
							"section.");
						err = EINVAL;
						break;
					}

					err = lvm2_physical_volume_create(
						grandchild_obj->name,
						(struct lvm2_dom_section*)
						grandchild_obj,
						&physical_volumes[j]);
					if(err) {
						LogError("Error while creating "
							"lvm2_physical_volume: "
							"%d", err);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;
			}
			else if(!strncmp(name->content, "logical_volumes",
				name->length))
			{
				size_t j;

				if(logical_volumes) {
					LogError("Duplicate definition of "
						"'logical_volumes'.");
					err = EINVAL;
					break;
				}

				err = lvm2_malloc(section->children_len *
					sizeof(struct lvm2_logical_volume*),
					(void**) &logical_volumes);
				if(err)
					break;

				logical_volumes_len = section->children_len;
				memset(logical_volumes, 0,
					logical_volumes_len *
					sizeof(struct lvm2_logical_volume*));

				for(j = 0; j < section->children_len; ++j) {
					struct lvm2_dom_obj *grandchild_obj =
						section->children[j];

					if(grandchild_obj->type !=
						LVM2_DOM_TYPE_SECTION)
					{
						LogError("Non-section type "
							"member in "
							"'logical_volumes' "
							"section.");
						err = EINVAL;
						break;
					}

					err = lvm2_logical_volume_create(
						grandchild_obj->name,
						(struct lvm2_dom_section*)
						grandchild_obj,
						&logical_volumes[j]);
					if(err) {
						LogError("Error while creating "
							"lvm2_logical_volume: "
							"%d", err);
						err = EINVAL;
						break;
					}
				}

				if(err)
					break;
			}
			else {
				LogError("Unrecognized section-type member in "
					"lvm2_volume_group: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else {
			LogError("Unrecognized child type: %d", type);
			err = EINVAL;
			break;
		}
	}

	if(!err) {
		if(!id) {
			LogError("Missing member 'id' in "
				"lvm2_volume_group.");
			err = EINVAL;
		}
		if(!seqno_defined) {
			LogError("Missing member 'seqno' in "
				"lvm2_volume_group.");
			err = EINVAL;
		}
		if(!status_defined) {
			LogError("Missing member 'status' in "
				"lvm2_volume_group.");
			err = EINVAL;
		}
		if(!extent_size_defined) {
			LogError("Missing member 'extent_size' in "
				"lvm2_volume_group.");
			err = EINVAL;
		}
		if(!max_lv_defined) {
			LogError("Missing member 'max_lv' in "
				"lvm2_volume_group.");
			err = EINVAL;
		}
		if(!max_pv_defined) {
			LogError("Missing member 'max_pv' in "
				"lvm2_volume_group.");
			err = EINVAL;
		}
	}

	if(!err) {
		err = lvm2_malloc(sizeof(struct lvm2_volume_group),
			(void**) &vg);
		if(err) {
			LogError("Error while allocating memory for struct "
				"lvm2_volume_group: %d",
				err);
		}
		else
			memset(vg, 0, sizeof(struct lvm2_volume_group));
	}

	if(!err) {
		vg->id = id;
		vg->seqno = seqno;
		vg->format = format;
		vg->status = status;
		if(vg->flags_defined) {
			vg->flags = flags;
		}
		else {
			vg->flags = 0;
		}
		vg->extent_size = extent_size;
		vg->max_lv = max_lv;
		vg->max_pv = max_pv;
		if(metadata_copies_defined)
			vg->metadata_copies = metadata_copies;
		else
			vg->metadata_copies = 1;
		vg->physical_volumes_len = physical_volumes_len;
		vg->physical_volumes = physical_volumes;
		vg->logical_volumes_len = logical_volumes_len;
		vg->logical_volumes = logical_volumes;

		*out_vg = vg;
	}
	else {
		if(vg)
			lvm2_free((void**) &vg,
				sizeof(struct lvm2_volume_group));
		if(logical_volumes) {
			for(i = 0; i < logical_volumes_len; ++i) {
				if(logical_volumes[i])
					lvm2_logical_volume_destroy(
						&logical_volumes[i]);
			}

			lvm2_free((void**) &logical_volumes,
				logical_volumes_len *
				sizeof(struct lvm2_logical_volume*));
		}
		if(physical_volumes) {
			for(i = 0; i < physical_volumes_len; ++i) {
				if(physical_volumes[i])
					lvm2_physical_volume_destroy(
						&physical_volumes[i]);
			}

			lvm2_free((void**) &physical_volumes,
				physical_volumes_len *
				sizeof(struct lvm2_physical_volume*));
		}

		if(format) {
			lvm2_bounded_string_destroy(&format);
		}

		if(id)
			lvm2_bounded_string_destroy(&id);
	}

	return err;
}

static void lvm2_volume_group_destroy(struct lvm2_volume_group **vg)
{
	if((*vg)->logical_volumes) {
		size_t i;

		for(i = 0; i < (*vg)->logical_volumes_len; ++i) {
			lvm2_logical_volume_destroy(
				&(*vg)->logical_volumes[i]);
		}

		lvm2_free((void**) &(*vg)->logical_volumes,
			(*vg)->logical_volumes_len *
			sizeof(struct lvm2_logical_volume*));
		
	}

	if((*vg)->physical_volumes) {
		size_t i;

		for(i = 0; i < (*vg)->physical_volumes_len; ++i) {
			lvm2_physical_volume_destroy(
				&(*vg)->physical_volumes[i]);
		}

		lvm2_free((void**) &(*vg)->physical_volumes,
			(*vg)->physical_volumes_len *
			sizeof(struct lvm2_physical_volume*));
	}

	if((*vg)->format) {
		lvm2_bounded_string_destroy(&(*vg)->format);
	}

	lvm2_bounded_string_destroy(&(*vg)->id);

	lvm2_free((void**) vg, sizeof(struct lvm2_volume_group));
}

LVM2_EXPORT int lvm2_layout_create(
		const struct lvm2_dom_section *const root_section,
		struct lvm2_layout **const out_layout)
{
	int err = 0;
	size_t i;

	struct lvm2_bounded_string *vg_name = NULL;
	struct lvm2_volume_group *vg = NULL;

	struct lvm2_bounded_string *contents = NULL;

	lvm2_bool version_defined = LVM2_FALSE;
	u64 version = 0;

	struct lvm2_bounded_string *description = NULL;

	struct lvm2_bounded_string *creation_host = NULL;

	lvm2_bool creation_time_defined = LVM2_FALSE;
	u64 creation_time = 0;

	struct lvm2_layout *layout = NULL;

	/* Search for vg_name candidates. */
	for(i = 0; i < root_section->children_len; ++i) {
		const struct lvm2_dom_obj *child = root_section->children[i];
		const struct lvm2_bounded_string *const name = child->name;

		if(child->type == LVM2_DOM_TYPE_VALUE) {
			const struct lvm2_dom_value *const value =
				(struct lvm2_dom_value*) child;

			if(!strncmp(name->content, "contents", name->length)) {
				if(contents) {
					LogError("Duplicate definition of "
						"'contents'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&contents);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "version",
				name->length))
			{
				if(version_defined) {
					LogError("Duplicate definition of "
						"'version'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&version);
				if(err)
					break;

				version_defined = LVM2_TRUE;
			}
			else if(!strncmp(name->content, "description",
				name->length))
			{
				if(description) {
					LogError("Duplicate definition of "
						"'description'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&description);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "creation_host",
				name->length))
			{
				if(creation_host) {
					LogError("Duplicate definition of "
						"'creation_host'.");
					err = EINVAL;
					break;
				}

				err = lvm2_bounded_string_dup(value->value,
					&creation_host);
				if(err)
					break;
			}
			else if(!strncmp(name->content, "creation_time",
				name->length))
			{
				if(creation_time_defined) {
					LogError("Duplicate definition of "
						"'creation_time'.");
					err = EINVAL;
					break;
				}

				err = lvm2_layout_parse_u64_value(value,
					&creation_time);
				if(err)
					break;

				creation_time_defined = LVM2_TRUE;
			}
			else {
				LogError("Unrecognized value-type member in "
					"root section: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
			
		}
		else if(child->type == LVM2_DOM_TYPE_ARRAY) {
			if(0) {
				/* No known arrays in root section. */
			}
			else {
				LogError("Unrecognized array-type member in "
					"root section: '%.*s'",
					name->length, name->content);
				err = EINVAL;
				break;
			}
		}
		else if(child->type == LVM2_DOM_TYPE_SECTION) {
			struct lvm2_bounded_string *dup_vg_name = NULL;

			if(vg_name || vg) {
				LogError("More than one sub-section in root. "
					"Cannot determine which is the volume "
					"group.");
				err = EINVAL;
				break;
			}

			err = lvm2_bounded_string_dup(child->name,
				&dup_vg_name);
			if(err)
				break;

			err = lvm2_volume_group_create(
				(struct lvm2_dom_section*) child, &vg);
			if(err) {
				lvm2_bounded_string_destroy(&dup_vg_name);
				break;
			}

			vg_name = dup_vg_name;

		}
		else {
			LogError("Unrecognized child type: %d", child->type);
			err = EINVAL;
			break;
		}
	}

	if(!err) {
		if(!vg_name ||
			!vg ||
			!contents ||
			!version_defined ||
			!description ||
			!creation_host ||
			!creation_time_defined)
		{
			LogError("Missing members in lvm2_layout:%s%s%s%s%s%s"
				"%s",
				!vg_name ? " vg_name" : "",
				!vg ? " vg" : "",
				!contents ? " contents" : "",
				!version_defined ? " version" : "",
				!description ? " description" : "",
				!creation_host ? " creation_host" : "",
				!creation_time_defined ? " creation_time" : "");
			err = EINVAL;
		}
	}

	if(!err) {
		err = lvm2_malloc(sizeof(struct lvm2_layout), (void**) &layout);
		if(err) {
			LogError("Error while allocating memory for struct "
				"lvm2_layout: %d", err);
		}
		else {
			memset(layout, 0, sizeof(struct lvm2_layout));

			layout->vg_name = vg_name;
			layout->vg = vg;
			layout->contents = contents;
			layout->version = version;
			layout->description = description;
			layout->creation_host = creation_host;
			layout->creation_time = creation_time;			
		}
	}

	if(err) {
		if(layout)
			lvm2_free((void**) &layout, sizeof(struct lvm2_layout));
		if(creation_host)
			lvm2_bounded_string_destroy(&creation_host);
		if(description)
			lvm2_bounded_string_destroy(&description);
		if(contents)
			lvm2_bounded_string_destroy(&contents);
		if(vg)
			lvm2_volume_group_destroy(&vg);
		if(vg_name)
			lvm2_bounded_string_destroy(&vg_name);
	}
	else
		*out_layout = layout;	

	return err;
}

LVM2_EXPORT void lvm2_layout_destroy(
		struct lvm2_layout **const layout)
{
	lvm2_bounded_string_destroy(&(*layout)->creation_host);
	lvm2_bounded_string_destroy(&(*layout)->description);
	lvm2_bounded_string_destroy(&(*layout)->contents);

	lvm2_volume_group_destroy(&(*layout)->vg);
	lvm2_bounded_string_destroy(&(*layout)->vg_name);

	lvm2_free((void**) layout, sizeof(struct lvm2_layout));
}

#define AlignSize(size, alignment) \
        ((((size) + (alignment) - 1) / (alignment)) * (alignment))

LVM2_EXPORT int lvm2_read_text(struct lvm2_device *dev,
		const u64 metadata_offset, const u64 metadata_size,
		const struct raw_locn *const locn,
		struct lvm2_layout **const out_layout)
{
	const u64 media_block_size = lvm2_device_get_alignment(dev);

	const u64 locn_offset = le64_to_cpu(locn->offset);
	const u64 locn_size = le64_to_cpu(locn->size);
	const u32 locn_checksum __attribute__((unused)) =
		le32_to_cpu(locn->checksum);
	const u32 locn_filler __attribute__((unused)) =
		le32_to_cpu(locn->filler);

	int err = EIO;

	struct lvm2_io_buffer *text_buffer = NULL;
	size_t text_buffer_inset;
	size_t text_buffer_size;

	u64 read_offset;

	char *text;
	size_t text_len;

	struct lvm2_dom_section *parse_result = NULL;
	struct lvm2_layout *layout = NULL;

	LogDebug("%s: Entering with dev=%p metadata_offset = %" FMTllu " "
		"metadata_size = %" FMTllu " locn=%p out_layout=%p...",
		__FUNCTION__, dev, ARGllu(metadata_offset),
		ARGllu(metadata_size), locn, out_layout);

	LogDebug("media_block_size = %" FMTllu, ARGllu(media_block_size));
	LogDebug("locn_offset = %" FMTllu, ARGllu(locn_offset));
	LogDebug("locn_size = %" FMTllu, ARGllu(locn_size));
	LogDebug("locn_checksum = 0x%" FMTlX, ARGlX(locn_checksum));
	LogDebug("locn_filler = 0x%" FMTlX, ARGlX(locn_filler));

	if(locn_offset >= metadata_size) {
		LogError("locn offset out of range for metadata area (offset: "
			"%" FMTllu " max: %" FMTllu ").",
			ARGllu(locn_offset), ARGllu(metadata_size));
		err = EINVAL;
		goto err_out;
	}
	else if(locn_size > (metadata_size - locn_offset)) {
		LogError("locn size out of range for metadata area (size: "
			"%" FMTllu " max: %" FMTllu ").",
			ARGllu(locn_size), ARGllu(metadata_size - locn_offset));
		err = EINVAL;
		goto err_out;
	}
	else if(locn_size > SIZE_MAX) {
		LogError("locn_size out of range (%" FMTllu ").",
			ARGllu(locn_size));
		err = EINVAL;
		goto err_out;
	}
	else if(media_block_size > SIZE_MAX) {
		LogError("media_block_size out of range (%" FMTllu ").",
			ARGllu(media_block_size));
		err = EINVAL;
		goto err_out;
	}

	text_buffer_inset = (size_t) (locn_offset % media_block_size);
	LogDebug("text_buffer_inset = %" FMTzu, ARGzu(text_buffer_inset));

	text_buffer_size = (size_t) AlignSize(text_buffer_inset + locn_size,
		media_block_size);
	LogDebug("text_buffer_size = %" FMTzu, ARGzu(text_buffer_size));

	err = lvm2_io_buffer_create(text_buffer_size, &text_buffer);
	if(err) {
		LogError("Error while allocating %" FMTzu " bytes of memory "
			"for 'textBuffer': %d", ARGzu(text_buffer_size), err);
		goto err_out;
	}

	read_offset = metadata_offset + (locn_offset - text_buffer_inset);
	LogDebug("read_offset = %" FMTllu, ARGllu(read_offset));

	err = lvm2_device_read(dev, read_offset, text_buffer_size, text_buffer);
	if(err) {
		LogError("Error %d while reading LVM2 text.", err);
		goto err_out;
	}

	text = &((char*) lvm2_io_buffer_get_bytes(text_buffer))
		[text_buffer_inset];
	text_len = (size_t) locn_size;

	//LogDebug("LVM2 text: %.*s", text_len, text);

	if(!lvm2_parse_text(text, text_len, &parse_result)) {
		LogError("Error while parsing text.");
		err = EIO;
		goto err_out;
	}

	err = lvm2_layout_create(parse_result, &layout);
	if(err) {
		LogError("Error while converting parsed result into structured "
			"data: %d", err);
		goto err_out;
	}

	lvm2_dom_section_destroy(&parse_result, LVM2_TRUE);

	*out_layout = layout;
cleanup:
	if(err && layout)
		lvm2_layout_destroy(&layout);
	if(parse_result)
		lvm2_dom_section_destroy(&parse_result, LVM2_TRUE);
	if(text_buffer)
		lvm2_io_buffer_destroy(&text_buffer);

	return err;
err_out:
	if(!err) {
		LogError("Warning: At err_out, but err not set. Setting "
			"default errno value (EIO).");
		err = EIO;
	}

	goto cleanup;
}

static lvm2_bool lvm2_parse_device_find_pv_location(
		const struct lvm2_logical_volume *const lv,
		const struct lvm2_physical_volume *const pv,
		struct lvm2_segment **const out_segment,
		struct lvm2_pv_location **const out_location,
		lvm2_bool *const out_lv_is_incomplete)
{
	u64 seg_no;
	u64 stripe_no;
	u64 mirror_no;

	for(seg_no = 0; seg_no < lv->segment_count; ++seg_no) {
		if(lv->segments[seg_no]->stripes &&
			lv->segments[seg_no]->mirrors)
		{
			LogError("Segment %" FMTzu " of logical volume "
				"\"%.*s\" has both stripes and mirrors "
				"(corrupt LVM metadata or new LVM feature?). "
				"Marking as incomplete.",
				ARGzu(seg_no),
				lv->name->length,
				lv->name->content);

			*out_lv_is_incomplete = LVM2_TRUE;
		}

		if(lv->segments[seg_no]->stripes) {
			LogDebug("Matching with %" FMTzu " stripes...",
				ARGzu(lv->segments[seg_no]->stripes_len));

			if(lv->segments[seg_no]->stripes_len != 1) {
				LogError("More than one stripe in segment "
					"%" FMTzu " of logical volume "
					"\"%.*s\". Marking as incomplete.",
					ARGzu(seg_no),
					lv->name->length,
					lv->name->content);

				*out_lv_is_incomplete = LVM2_TRUE;
			}

			for(stripe_no = 0;
				stripe_no < lv->segments[seg_no]->stripes_len;
				++stripe_no)
			{
				const struct lvm2_bounded_string *pv_name;

				pv_name = lv->segments[seg_no]->
					stripes[stripe_no]->pv_name;

				LogDebug("\tMatching with stripe \"%.*s\"...",
					pv_name->length,
					pv_name->content);

				if(pv_name->length == pv->name->length &&
					!strncmp(pv_name->content,
					pv->name->content, pv_name->length))
				{
					/* Match found. Break inner loop. */
					break;
				}
			}

			/* Break outer loop if a match was found among the
			 * stripes. */
			if(stripe_no < lv->segments[seg_no]->stripes_len) {
				break;
			}
		}

		if(lv->segments[seg_no]->mirrors) {
			LogDebug("Matching with %" FMTzu " mirrors...",
				ARGzu(lv->segments[seg_no]->mirrors_len));

			if(lv->segments[seg_no]->mirrors_len > 1) {
				LogError("More than one mirror in segment "
					"%" FMTzu " of logical volume "
					"\"%.*s\". Marking as incomplete.",
					ARGzu(seg_no),
					lv->name->length,
					lv->name->content);

				*out_lv_is_incomplete = LVM2_TRUE;
			}

			for(mirror_no = 0;
				mirror_no < lv->segments[seg_no]->mirrors_len;
				++mirror_no)
			{
				const struct lvm2_bounded_string *pv_name;

				pv_name = lv->segments[seg_no]->
					mirrors[mirror_no]->pv_name;

				LogDebug("\tMatching with mirror \"%.*s\"...",
					pv_name->length,
					pv_name->content);

				if(pv_name->length == pv->name->length &&
					!strncmp(pv_name->content,
					pv->name->content, pv_name->length))
				{
					/* Match found. Break inner loop. */
					break;
				}
			}

			/* Break outer loop if a match was found among the
			 * mirrors. */
			if(mirror_no < lv->segments[seg_no]->mirrors_len) {
				break;
			}
		}
	}

	if(seg_no == lv->segment_count) {
		/* No match. */
		return LVM2_FALSE;
	}
	else if(stripe_no < lv->segments[seg_no]->stripes_len) {
		/* Match found in stripes. */
		*out_segment = lv->segments[seg_no];
		*out_location = lv->segments[seg_no]->stripes[stripe_no];
		return LVM2_TRUE;
	}
	else if(mirror_no < lv->segments[seg_no]->mirrors_len) {
		/* Match found in mirrors. */
		*out_segment = lv->segments[seg_no];
		*out_location = lv->segments[seg_no]->mirrors[mirror_no];
		return LVM2_TRUE;
	}
	else {
		LogError("Unexpected: both stripe_no (%" FMTzu ") and "
			"mirror_no (%" FMTzu ") have reached the end.",
			ARGzu(stripe_no),
			ARGzu(mirror_no));
		return LVM2_FALSE;
	}
}

LVM2_EXPORT int lvm2_parse_device(struct lvm2_device *const dev,
		lvm2_bool (*const volume_callback)(void *private_data,
			u64 device_size, const char *volume_name,
			u64 volume_start, u64 volume_length, lvm2_bool is_incomplete),
		void *const private_data)
{
	const u64 media_block_size = lvm2_device_get_alignment(dev);

	int err;
	struct lvm2_io_buffer *buffer = NULL;
	struct lvm2_io_buffer *secondary_buffer = NULL;
	u64 read_offset = 0;
	size_t buffer_size = 0;
	size_t secondary_buffer_size = 0;
	u32 i;
	s32 firstLabel = -1;
	lvm2_bool manual_break = LVM2_FALSE;

	LogDebug("%s: Entering with dev=%p.", __FUNCTION__, dev);

	if(media_block_size > SIZE_MAX) {
		LogError("Unrealistic media block size: %" FMTllu,
			ARGllu(media_block_size));
		goto out_err;
	}

	/* Allocate a suitably sized buffer. */

	buffer_size = (size_t) AlignSize(LVM_SECTOR_SIZE, media_block_size);
	err = lvm2_io_buffer_create(buffer_size, &buffer);
	if(err) {
		LogError("Error while allocating 'buffer' (%" FMTzu " bytes).",
			ARGzu(buffer_size));
		goto out_err;
	}

	LogDebug("\tAllocated 'buffer': %" FMTzu " bytes",
		ARGzu(buffer_size));

	secondary_buffer_size =
		(size_t) AlignSize(LVM_MDA_HEADER_SIZE, media_block_size);
	err = lvm2_io_buffer_create(secondary_buffer_size, &secondary_buffer);
	if(err) {
		LogError("Error while allocating 'secondary_buffer' "
			"(%" FMTzu "bytes).",
			ARGzu(secondary_buffer_size));
		goto out_err;
	}

	LogDebug("\tAllocated 'secondary_buffer': %" FMTzu " bytes",
		ARGzu(secondary_buffer_size));

	/* Open the media with read-only access. */

	LogDebug("\tMedia is open.");

	for(i = 0; i < LVM_LABEL_SCAN_SECTORS; ++i) {
		const struct label_header *labelHeader;
		u64 labelSector;
		u32 labelCrc;
		u32 calculatedCrc;

		read_offset = i * LVM_SECTOR_SIZE;

		LogDebug("Searching for LVM label at sector %" FMTlu "...",
			ARGlu(i));

		err = lvm2_device_read(dev, read_offset, buffer_size, buffer);
		if(err) {
			LogError("Error while reading sector %" FMTlu ".",
				ARGlu(i));
			goto out_err;
		}

		labelHeader = (const struct label_header*)
			lvm2_io_buffer_get_bytes(buffer);

		LogDebug("\tlabel_header = {");
		LogDebug("\t\t.id = '%.*s'", 8, labelHeader->id);
		LogDebug("\t\t.sector_xl = %" FMTllu,
			ARGllu(le64_to_cpu(labelHeader->sector_xl)));
		LogDebug("\t\t.crc_xl = 0x%08" FMTlX,
			ARGlX(le32_to_cpu(labelHeader->crc_xl)));
		LogDebug("\t\t.offset_xl = %" FMTlu,
			ARGlu(le32_to_cpu(labelHeader->offset_xl)));
		LogDebug("\t\t.type = '%.*s'", 8, labelHeader->type);
		LogDebug("\t}");

		if(memcmp(labelHeader->id, "LABELONE", 8)) {
			LogDebug("\t'id' magic does not match.");
			continue;
		}

		labelSector = le64_to_cpu(labelHeader->sector_xl);
		if(labelSector != i) {
			LogError("'sector_xl' does not match actual sector "
				"(%" FMTllu " != %" FMTlu ").",
				ARGllu(labelSector), ARGlu(i));
			continue;
		}

		labelCrc = le32_to_cpu(labelHeader->crc_xl);
		calculatedCrc = lvm2_calc_crc(LVM_INITIAL_CRC,
			&labelHeader->offset_xl, LVM_SECTOR_SIZE -
			offsetof(struct label_header, offset_xl));
		if(labelCrc != calculatedCrc) {
			LogError("Stored and calculated CRC32 checksums don't "
				"match (0x%08" FMTlX " != 0x%08" FMTlX ").",
				ARGlX(labelCrc), ARGlX(calculatedCrc));
			continue;
		}

		if(firstLabel == -1) {
			firstLabel = i;
		}
		else {
			LogError("Ignoring additional label at sector %" FMTlu,
				ARGlu(i));
		}
	}

	if(firstLabel < 0) {
		LogDebug("No LVM label found on volume.");
		goto out_err;
	}

	{
		const void *sectorBytes;
		const struct label_header *labelHeader;
		const struct pv_header *pvHeader;

		u64 labelSector;
		u32 labelCrc;
		u32 calculatedCrc;
		u32 contentOffset;

		u64 deviceSize;

		u32 disk_areas_idx;
		size_t dataAreasLength;
		size_t metadataAreasLength;

		struct lvm2_layout *layout = NULL;

		read_offset = firstLabel * LVM_SECTOR_SIZE;

		err = lvm2_device_read(dev, read_offset, buffer_size, buffer);
		if(err) {
			LogError("\tError while reading label sector "
				"%" FMTlu ": %d",
				ARGlu(i), err);
			goto out_err;
		}

		sectorBytes = lvm2_io_buffer_get_bytes(buffer);
		labelHeader = (const struct label_header*) sectorBytes;

		/* Re-verify label fields. If the first three don't verify we
		 * have a very strange situation, indicated with the tag
		 * 'Unexpected:' before the error messages. */
		if(memcmp(labelHeader->id, "LABELONE", 8)) {
			LogError("Unexpected: 'id' magic does not match.");
			goto out_err;
		}

		labelSector = le64_to_cpu(labelHeader->sector_xl);
		if(labelSector != (u32) firstLabel) {
			LogError("Unexpected: 'sector_xl' does not match "
				 "actual sector (%" FMTllu " != %" FMTlu ").",
				 ARGllu(labelSector), ARGlu(firstLabel));
			goto out_err;
		}

		labelCrc = le32_to_cpu(labelHeader->crc_xl);
		calculatedCrc = lvm2_calc_crc(LVM_INITIAL_CRC,
			&labelHeader->offset_xl, LVM_SECTOR_SIZE -
			offsetof(struct label_header, offset_xl));
		if(labelCrc != calculatedCrc) {
			LogError("Unexpected: Stored and calculated CRC32 "
				 "checksums don't match (0x%08" FMTlX " "
				 "!= 0x%08" FMTlX ").",
				 ARGlx(labelCrc), ARGlX(calculatedCrc));
			goto out_err;
		}

		contentOffset = le32_to_cpu(labelHeader->offset_xl);
		if(contentOffset < sizeof(struct label_header)) {
			LogError("Content overlaps header (content offset: "
				 "%" FMTlu ").", ARGlu(contentOffset));
			goto out_err;
		}

		if(memcmp(labelHeader->type, LVM_LVM2_LABEL, 8)) {
			LogError("Unsupported label type: '%.*s'.",
				8, labelHeader->type);
			goto out_err;
		}

		pvHeader = (struct pv_header*)
			&((char*) sectorBytes)[contentOffset];

		disk_areas_idx = 0;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0) {
			const uintptr_t ptrDiff = (uintptr_t)
				&pvHeader->disk_areas_xl[disk_areas_idx] -
				(uintptr_t) sectorBytes;

			if(ptrDiff > buffer_size) {
				LogError("Data areas overflow into the next "
					"sector (index %" FMTzu ").",
					ARGzu(disk_areas_idx));
				goto out_err;
			}

			++disk_areas_idx;
		}
		dataAreasLength = disk_areas_idx;
		++disk_areas_idx;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0) {
			const uintptr_t ptrDiff = (uintptr_t)
				&pvHeader->disk_areas_xl[disk_areas_idx] -
				(uintptr_t) sectorBytes;

			if(ptrDiff > buffer_size) {
				LogError("Metadata areas overflow into the "
					"next sector (index %" FMTzu ").",
					ARGzu(disk_areas_idx));
				goto out_err;
			}

			++disk_areas_idx;
		}
		metadataAreasLength = disk_areas_idx - (dataAreasLength + 1);

		if(dataAreasLength != metadataAreasLength) {
			LogError("Size mismatch between PV data and metadata "
				"areas (%" FMTzu " != %" FMTzu ").",
				ARGzu(dataAreasLength),
				ARGzu(metadataAreasLength));
			goto out_err;
		}

#if defined(DEBUG)
		LogDebug("\tpvHeader = {");
		LogDebug("\t\tpv_uuid = '%.*s'", LVM_ID_LEN, pvHeader->pv_uuid);
		LogDebug("\t\tdevice_size_xl = %" FMTllu,
			ARGllu(le64_to_cpu(pvHeader->device_size_xl)));
		LogDebug("\t\tdisk_areas_xl = {");
		LogDebug("\t\t\tdata_areas = {");
		disk_areas_idx = 0;
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0 &&
			disk_areas_idx < dataAreasLength)
		{
			LogDebug("\t\t\t\t{");
			LogDebug("\t\t\t\t\toffset = %" FMTllu,
				ARGllu(le64_to_cpu(pvHeader->
				disk_areas_xl[disk_areas_idx].offset)));
			LogDebug("\t\t\t\t\tsize = %" FMTllu,
				ARGllu(le64_to_cpu(pvHeader->
				disk_areas_xl[disk_areas_idx].size)));
			LogDebug("\t\t\t\t}");

			++disk_areas_idx;
		}
		LogDebug("\t\t\t}");
		++disk_areas_idx;
		LogDebug("\t\t\tmetadata_areas = {");
		while(pvHeader->disk_areas_xl[disk_areas_idx].offset != 0 &&
			disk_areas_idx < (dataAreasLength + 1 +
			metadataAreasLength))
		{
			LogDebug("\t\t\t\t{");
			LogDebug("\t\t\t\t\toffset = %" FMTllu,
				ARGllu(le64_to_cpu(pvHeader->
				disk_areas_xl[disk_areas_idx].offset)));
			LogDebug("\t\t\t\t\tsize = %" FMTllu,
				ARGllu(le64_to_cpu(pvHeader->
				disk_areas_xl[disk_areas_idx].size)));
			LogDebug("\t\t\t\t}");

			++disk_areas_idx;
		}
		LogDebug("\t\t\t}");
		LogDebug("\t\t}");
		LogDebug("\t}");
#endif /* defined(DEBUG) */

		deviceSize = le64_to_cpu(pvHeader->device_size_xl);
		for(disk_areas_idx = 0; !manual_break &&
			pvHeader->disk_areas_xl[disk_areas_idx].offset != 0;
			++disk_areas_idx)
		{
			const struct disk_locn *locn;
			const struct disk_locn *meta_locn;
			u64 meta_offset;
			u64 meta_size;

			const struct mda_header *mdaHeader;
			u32 mda_checksum;
			u32 mda_version;
			u64 mda_start;
			u64 mda_size;

			u32 mda_calculated_checksum;

			if(disk_areas_idx >= dataAreasLength) {
				LogError("Overflow when iterating through disk "
					"areas (%" FMTzu " >= %" FMTzu ").",
					ARGzu(disk_areas_idx),
					ARGzu(dataAreasLength));
				goto out_err;
			}

			locn = &pvHeader->disk_areas_xl[disk_areas_idx];
			meta_locn = &pvHeader->disk_areas_xl[dataAreasLength +
				1 + disk_areas_idx];

			meta_offset = le64_to_cpu(meta_locn->offset);
			meta_size = le64_to_cpu(meta_locn->size);

			err = lvm2_device_read(dev, meta_offset,
				secondary_buffer_size, secondary_buffer);
			if(err) {
				LogError("Error while reading first metadata "
					"sector of PV number %" FMTlu " "
					"(offset %" FMTzu " bytes): %d",
					ARGlu(disk_areas_idx),
					ARGzu(meta_offset), err);
				goto out_err;
			}

			mdaHeader = (const struct mda_header*)
				lvm2_io_buffer_get_bytes(secondary_buffer);

#if defined(DEBUG)
			LogDebug("mdaHeader[%" FMTlu "] = {",
				ARGlu(disk_areas_idx));
			LogDebug("\tchecksum_xl = 0x%08" FMTlX,
				ARGlX(le32_to_cpu(mdaHeader->checksum_xl)));
			LogDebug("\tmagic = '%.*s'", 16, mdaHeader->magic);
			LogDebug("\tversion = %" FMTlu,
				ARGlu(le32_to_cpu(mdaHeader->version)));
			LogDebug("\tstart = %" FMTllu,
				ARGllu(le64_to_cpu(mdaHeader->start)));
			LogDebug("\tsize = %" FMTllu,
				ARGllu(le64_to_cpu(mdaHeader->size)));
			LogDebug("\traw_locns = {");
			{
				size_t raw_locns_idx = 0;

				while(memcmp(&null_raw_locn,
					&mdaHeader->raw_locns[raw_locns_idx],
					sizeof(struct raw_locn)) != 0)
				{
					const struct raw_locn *const cur_locn =
						&mdaHeader->
						raw_locns[raw_locns_idx];

					LogDebug("\t\t[%" FMTllu "] = {",
						ARGllu(raw_locns_idx));
					LogDebug("\t\t\toffset = %" FMTllu,
						ARGllu(le64_to_cpu(
						cur_locn->offset)));
					LogDebug("\t\t\tsize = %" FMTllu,
						ARGllu(le64_to_cpu(
						cur_locn->size)));
					LogDebug("\t\t\tchecksum = 0x%08" FMTlX,
						ARGlX(le32_to_cpu(
						cur_locn->checksum)));
					LogDebug("\t\t\tfiller = %" FMTlu,
						ARGlu(le32_to_cpu(
						cur_locn->filler)));
					LogDebug("\t\t}");

					++raw_locns_idx;
				}
			}
			LogDebug("\t}");
			LogDebug("}");
#endif /* defined(DEBUG) */

			mda_checksum = le32_to_cpu(mdaHeader->checksum_xl);
			mda_version = le32_to_cpu(mdaHeader->version);
			mda_start = le64_to_cpu(mdaHeader->start);
			mda_size = le64_to_cpu(mdaHeader->size);

			mda_calculated_checksum = lvm2_calc_crc(LVM_INITIAL_CRC,
				mdaHeader->magic, LVM_MDA_HEADER_SIZE -
				offsetof(struct mda_header, magic));
			if(mda_calculated_checksum != mda_checksum) {
				LogError("mda_header checksum mismatch "
					"(calculated: 0x%" FMTlX " expected: "
					"0x%" FMTlX ").",
					ARGlX(mda_calculated_checksum),
					ARGlX(mda_checksum));
				continue;
			}

			if(mda_version != 1) {
				LogError("Unsupported mda_version: %" FMTlu,
					ARGlu(mda_version));
				continue;
			}

			if(mda_start != meta_offset) {
				LogError("mda_start does not match metadata "
					"offset (%" FMTllu " != %" FMTllu ").",
					ARGllu(mda_start), ARGllu(meta_offset));
				continue;
			}

			if(mda_size != meta_size) {
				LogError("mda_size does not match metadata "
					"size (%" FMTllu " != %" FMTllu ").",
					ARGllu(mda_size), ARGllu(meta_size));
				continue;
			}

			if(memcmp(&mdaHeader->raw_locns[0], &null_raw_locn,
				sizeof(struct raw_locn)) == 0)
			{
				LogError("Missing first raw_locn.");
				continue;
			}
			else if(memcmp(&mdaHeader->raw_locns[1], &null_raw_locn,
				sizeof(struct raw_locn)) != 0)
			{
				LogError("Found more than one raw_locn "
					"(currently unsupported).");
				continue;
			}

			err = lvm2_read_text(dev, meta_offset, meta_size,
				&mdaHeader->raw_locns[0], &layout);
			if(err) {
				LogDebug("Error while reading LVM2 text: %d",
					err);
				continue;
			}

			LogDebug("Successfully read LVM2 text.");

			{
				size_t j;
				const struct lvm2_physical_volume *match = NULL;

				for(j = 0; j < layout->vg->physical_volumes_len;
					++j)
				{
					const char *const our_uuid =
						(char*) pvHeader->pv_uuid;
					const struct lvm2_physical_volume *const
						pv =
						layout->vg->physical_volumes[j];

					if(pv->id->length != 38) {
						LogError("Invalid id length %d",
							pv->id->length);
						continue;
					}

					if(!strncmp(&pv->id->content[0],
						&our_uuid[0], 6) &&
						pv->id->content[6] == '-' &&
						!strncmp(&pv->id->content[7],
						&our_uuid[6], 4) &&
						pv->id->content[11] == '-' &&
						!strncmp(&pv->id->content[12],
						&our_uuid[10], 4) &&
						pv->id->content[16] == '-' &&
						!strncmp(&pv->id->content[17],
						&our_uuid[14], 4) &&
						pv->id->content[21] == '-' &&
						!strncmp(&pv->id->content[22],
						&our_uuid[18], 4) &&
						pv->id->content[26] == '-' &&
						!strncmp(&pv->id->content[27],
						&our_uuid[22], 4) &&
						pv->id->content[31] == '-' &&
						!strncmp(&pv->id->content[32],
						&our_uuid[26], 6))
					{
						LogDebug("Found physical "
							"volume: '%.*s'",
							pv->name->length,
							pv->name->content);
						match = pv;
						break;
					}
				}

				if(!match) {
					LogError("No physical volume match "
						"found in LVM2 database.");
				}
				else for(j = 0;
					j < layout->vg->logical_volumes_len;
					++j)
				{
					const struct lvm2_logical_volume *const
						lv =
						layout->vg->logical_volumes[j];
					u64 partitionStart;
					u64 partitionLength;
					lvm2_bool is_incomplete = LVM2_FALSE;
					struct lvm2_segment *segment_match =
						NULL;
					struct lvm2_pv_location *pv_match =
						NULL;

					if(lv->segment_count != 1) {
						LogError("More than one "
							"segment in volume. "
							"Marking as "
							"incomplete.");
						is_incomplete = LVM2_TRUE;
					}

					/* Search for our PV among the LV's
					 * segments and stripes. */
					if(!lvm2_parse_device_find_pv_location(
						lv, match, &segment_match,
						&pv_match, &is_incomplete))
					{
						LogError("Physical volume "
							"\"%.*s\" not found in "
							"logical volume's "
							"descriptors.",
							match->name->length,
							match->name->content);
						continue;
					}

					partitionStart = (match->pe_start +
						pv_match->extent_start *
						layout->vg->extent_size) *
						media_block_size /* 512? */;
					LogDebug("partitionStart: %" FMTllu,
						ARGllu(partitionStart));

					partitionLength =
						segment_match->extent_count *
						layout->vg->extent_size *
						media_block_size /* 512? */;

					LogDebug("partitionLength: %" FMTllu,
						ARGllu(partitionLength));

					if(!volume_callback(private_data,
						deviceSize, lv->name->content,
						partitionStart,
						partitionLength,
						is_incomplete))
					{
						manual_break = LVM2_TRUE;
						break;
					}
				}

				lvm2_layout_destroy(&layout);
			}
		}
	}

cleanup:
	/* Release our resources. */

	if(secondary_buffer)
		lvm2_io_buffer_destroy(&secondary_buffer);
	if(buffer)
		lvm2_io_buffer_destroy(&buffer);

	return err;
out_err:
	if(!err)
		err = EIO;

	goto cleanup;
}

LVM2_EXPORT lvm2_bool lvm2_check_layout()
{
	lvm2_bool res = LVM2_TRUE;

	if(sizeof(struct label_header) != 32) {
		LogError("Invalid size of struct label_header: %" FMTzu,
			ARGzu(sizeof(struct label_header)));
		res = LVM2_FALSE;
	}
	else {
		/* TODO: Add assertions for individual field offsets. */
	}

	if(sizeof(struct disk_locn) != 16) {
		LogError("Invalid size of struct disk_locn: %" FMTzu,
			ARGzu(sizeof(struct disk_locn)));
		res = LVM2_FALSE;
	}
	else {
		/* TODO: Add assertions for individual field offsets. */
	}

	if(sizeof(struct pv_header) != 40) {
		LogError("Invalid size of struct pv_header: %" FMTzu,
			ARGzu(sizeof(struct pv_header)));
		res = LVM2_FALSE;
	}
	else {
		/* TODO: Add assertions for individual field offsets. */
	}

	if(sizeof(struct raw_locn) != 24) {
		LogError("Invalid size of struct raw_locn: %" FMTzu,
			ARGzu(sizeof(struct raw_locn)));
		res = LVM2_FALSE;
	}
	else {
		/* TODO: Add assertions for individual field offsets. */
	}

	if(sizeof(struct mda_header) != 40) {
		LogError("Invalid size of struct mda_header: %" FMTzu,
			ARGzu(sizeof(struct mda_header)));
		res = LVM2_FALSE;
	}
	else {
		/* TODO: Add assertions for individual field offsets. */
	}

	return res;
}
