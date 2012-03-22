/*-
 * Copyright (C) 2012 Erik Larsson
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
#include "lvm2_osal.h"

#include <string.h>

#include <sys/errno.h>
#include <machine/limits.h>

#if (defined(__DARWIN__) || defined (__APPLE__)) && defined(KERNEL)
#define LVM2_TEXT_EXPORT __private_extern__
#else
#define LVM2_TEXT_EXPORT
#endif

LVM2_TEXT_EXPORT u32 lvm2_calc_crc(u32 initial, const void *buf, u32 size)
{
	static const u32 crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
	};
	u32 i, crc = initial;
	const u8 *data = (const u8*) buf;

	LogDebug("%s: Entering with initial=0x%08" FMTlX " buf=%p "
		"size=%" FMTlu ".",
		__FUNCTION__, ARGlX(initial), buf, ARGlu(size));

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

static void lvm2_bounded_string_destroy(
		struct lvm2_bounded_string **string)
{
	lvm2_free((void**) string,
		sizeof(struct lvm2_bounded_string) + (*string)->length);
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
	LogDebug("%s: Entering with value=%p.",
		__FUNCTION__, value);
	LogDebug("\t*value = %p", *value);

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

	LogDebug("%s: Entering with array=%p element=%p.",
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
	LogDebug("%s: Entering with array=%p recursive=%d.",
		__FUNCTION__, array, recursive);
	LogDebug("\t*array = %p", *array);

	if((*array)->obj_super.type != LVM2_DOM_TYPE_ARRAY) {
		LogError("BUG: Wrong type (%d) of DOM object in %s.",
			(*array)->obj_super.type, __FUNCTION__);
		return;
	}

	if(recursive) {
		size_t i;

		for(i = 0; i < (*array)->elements_len; ++i) {
			LogDebug("\t(*array)->elements[%" FMTzu "] = %p",
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

	LogDebug("%s: Entering with section=%p child=%p.",
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
	LogDebug("old_children=%p", old_children);
	old_children_len = section->children_len;
	LogDebug("old_children_len=%" FMTzu, ARGzu(old_children_len));
	old_children_size = old_children_len * sizeof(struct lvm2_dom_obj*);
	LogDebug("old_children_size=%" FMTzu, ARGzu(old_children_size));

	new_children_len = old_children_len + 1;
	LogDebug("new_children_len=%" FMTzu, ARGzu(new_children_len));
	new_children_size = new_children_len * sizeof(struct lvm2_dom_obj*);
	LogDebug("Allocating %" FMTzu " bytes...", ARGzu(new_children_size));
	err = lvm2_malloc(new_children_size, (void**) &new_children);
	if(err) {
		LogError("Error while allocating memory for children array "
			"expansion: %d", err);
	}
	else {
		LogDebug("\tAllocated %" FMTzu " bytes.", ARGzu(new_children_size));
		if(old_children)
			memcpy(new_children, old_children, old_children_size);
		new_children[new_children_len - 1] = child;

		section->children = new_children;
		section->children_len = new_children_len;

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

LVM2_TEXT_EXPORT void lvm2_dom_section_destroy(
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

static void lvm2_volume_group_destroy(struct lvm2_volume_group **vg)
{
	lvm2_bounded_string_destroy(&(*vg)->name);
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

	LogDebug("%s: Entering with builder=%p array_name=%p (%.*s) "
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

	LogDebug("%s: Entering with builder=%p section_name=%p (%.*s) "
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

	LogDebug("%s: Entering with text=%p textLen=%" FMTzu " builder=%p "
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


LVM2_TEXT_EXPORT lvm2_bool lvm2_parse_text(const char *const text,
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

static int lvm2_layout_parse_u64_value(const struct lvm2_dom_value *value,
		u64 *out_value)
{
	int i;
	u64 result = 0;

	if(value->value->length > 19)
		return EOVERFLOW;

	i = value->value->length;

	for(i = 0; i < value->value->length; ++i) {
		char cur_char =
			value->value->content[value->value->length - 1 - i];

		if(cur_char < '0' || cur_char > '9') {
			LogError("Invalid character in numeric string: '%c'",
				cur_char);
			return EINVAL;
		}
		if

		i += (cur_char - '0') * i;
	};

	return i;
}

LVM2_TEXT_EXPORT int lvm2_layout_create(
		const struct lvm2_dom_section *root_section,
		struct lvm2_layout **out_layout);
		
LVM2_TEXT_EXPORT int lvm2_layout_create(
		const struct lvm2_dom_section *const root_section,
		struct lvm2_layout **const out_layout)
{
	int err = 0;
	size_t i;
	const char *path_2l[3];
	const struct lvm2_dom_obj *cur_obj = NULL;

	const struct lvm2_bounded_string *vg_name = NULL;
	const struct lvm2_dom_section *physical_volumes = NULL;
	const struct lvm2_dom_section *logical_volumes = NULL;

	err = lvm2_malloc(sizeof(struct lvm2_layout), )

	/* Search for vg_name candidates. */
	for(i = 0; i < root_section->children_len; ++i) {
		const struct lvm2_dom_obj *child = root_section->children[i];
		if(child->type == LVM2_DOM_TYPE_SECTION) {
			if(vg_name) {
				LogError("More than one sub-section in root. "
					"Cannot determine which is the volume "
					"group.");
				return EINVAL;
			}

			vg_name = child->name;
		}
	}

	{
		path_2l[0] = vg_name->content;
		path_2l[1] = "id";
		path_2l[2] = NULL;

		cur_obj = lvm2_dom_tree_lookup(root_section, path_2l);
		if(!cur_obj) {
			LogError("Could not find 'id' section.");
			return ENOENT;
		}
		else if(cur_obj->type != LVM2_DOM_TYPE_VALUE) {
			LogError("Non-section type: 'id'");
			return ENOENT;
		}

		id = (struct lvm2_dom_value*) cur_obj;
	}

	{
		path_2l[0] = vg_name->content;
		path_2l[1] = "physical_volumes";
		path_2l[2] = NULL;

		cur_obj = lvm2_dom_tree_lookup(root_section, path_2l);
		if(!cur_obj) {
			LogError("Could not find 'physical_volumes' section.");
			return ENOENT;
		}
		else if(cur_obj->type != LVM2_DOM_TYPE_SECTION) {
			LogError("Non-section type: 'physical_volumes'");
			return ENOENT;
		}

		physical_volumes = (struct lvm2_dom_section*) cur_obj;
	}

	

	{
		path_2l[0] = vg_name->content;
		path_2l[1] = "logical_volumes";
		path_2l[2] = NULL;

		cur_obj = lvm2_dom_tree_lookup(root_section, path_2l);
		if(!cur_obj) {
			LogError("Could not find 'logical_volumes' section.");
			return ENOENT;
		}
		else if(cur_obj->type != LVM2_DOM_TYPE_SECTION) {
			LogError("Non-section type: 'logical_volumes'");
			return ENOENT;
		}

		logical_volumes = (struct lvm2_dom_section*) cur_obj;
	}

	return err;
}

LVM2_TEXT_EXPORT void lvm2_layout_destroy(
		struct lvm2_layout **parsed_text);

LVM2_TEXT_EXPORT void lvm2_layout_destroy(
		struct lvm2_layout **const parsed_text)
{
	if((*parsed_text)->vg)
		lvm2_volume_group_destroy(&(*parsed_text)->vg);
	/*if(parsed_text->contents)
		IOFree(&parsed_text->contents, parsed_text->contents_size);*/

	lvm2_free((void**) parsed_text, sizeof(struct lvm2_layout));
}
