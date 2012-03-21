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

LVM2_TEXT_EXPORT void lvm2_bounded_string_destroy(
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

static int lvm2_dom_section_create(const char *const section_name,
	const int section_name_len, struct lvm2_dom_section **const out_section)
{
	int err;
	struct lvm2_dom_section *section;

	err = lvm2_malloc(sizeof(struct lvm2_dom_section), (void**) &section);
	if(err) {
		LogError("Error while allocating memory for struct "
			"lvm2_dom_section: %d", err);
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

static int lvm2_dom_section_add_child(struct lvm2_dom_section **const section,
		struct lvm2_dom_obj **const child)
{
	
}

static void lvm2_dom_section_destroy(struct lvm2_dom_section **const section)
{
	if((*section)->obj_super.type != LVM2_DOM_TYPE_SECTION) {
		LogError("BUG: Wrong type (%d) of DOM object in %s.",
			(*section)->obj_super.type, __FUNCTION__);
		return;
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

LVM2_TEXT_EXPORT void lvm2_volume_group_destroy(struct lvm2_volume_group **vg)
{
	lvm2_bounded_string_destroy(&(*vg)->name);
}

LVM2_TEXT_EXPORT void parsed_lvm2_text_destroy(
		struct parsed_lvm2_text **parsed_text)
{
	if((*parsed_text)->vg)
		lvm2_volume_group_destroy(&(*parsed_text)->vg);
	/*if(parsed_text->contents)
		IOFree(&parsed_text->contents, parsed_text->contents_size);*/

	lvm2_free((void**) parsed_text, sizeof(struct parsed_lvm2_text));
}


static void parsed_lvm2_text_builder_init(
		struct parsed_lvm2_text_builder *const builder)
{
	memset(builder, 0, sizeof(struct parsed_lvm2_text_builder));
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

	return builder->root;
}

static int parsed_lvm2_text_builder_enter_section(
		struct parsed_lvm2_text_builder *const builder,
		const char *const section_name, const int section_name_len,
		lvm2_bool is_array)
{
	const size_t stack_element_size =
		sizeof(struct parsed_lvm2_text_builder_section);

	int err;
	struct lvm2_dom_section *dom_section;
	struct parsed_lvm2_text_builder_section *stack_element;
	struct parsed_lvm2_text *parsed_text;

	LogDebug("%s: Entering with builder=%p section_name=%p (%.*s) "
		"section_name_len=%d",
		__FUNCTION__, builder, section_name, section_name_len,
		section_name, section_name_len);

	err = lvm2_dom_section_create(section_name, section_name_len,
		&dom_section);
	if(err) {
		dom_section = NULL;
		LogError("Error while creating DOM section: %d", err);
	}
	else if((err = lvm2_malloc(stack_element_size, (void**) &stack_element))
		!= 0)
	{
		stack_element = NULL;
		LogError("Error while allocating memory for 'stack_element': "
			"%d", err);
	}
	else {
		if(!builder->stack) {
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
			err = lvm2_dom_section_add_child(builder->stack->obj,
				&dom_section->obj_super);
			if(err) {
				LogError("Error while adding child to parent "
					"section: %d", err);
			}
		}
	}

	if(!err) {
		/* Push new element onto stack. */
		stack_element->obj = dom_section;
		stack_element->parent = builder->stack;

		builder->stack = stack_element;
		++builder->stack_depth;
	}
	else {
		if(parsed_text)
			lvm2_free((void**) &parsed_text, parsed_text_size);
		if(stack_element)
			lvm2_free((void**) &stack_element, stack_element_size);
		if(section_name_dup)
			lvm2_bounded_string_destroy(&section_name_dup);
	}

	return err;
}

static int parsed_lvm2_text_builder_leave_section(
		struct parsed_lvm2_text_builder *const builder)
{
	parsed_lvm2_text_builder_stack_pop(builder);

	return 0;
}

static int parsed_lvm2_text_builder_array_element(
		struct parsed_lvm2_text_builder *const builder,
		const char *const element_name, const int element_name_len)
{
	LogDebug("Got array element: \"%.*s\"",
		element_name_len, element_name);

	return 0;
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
				identifierToken, identifierTokenLen,
				LVM2_FALSE);
			if(err) {
				return LVM2_FALSE;
			}

			if(!parseDictionary(&text[i], textLen - i, builder,
				LVM2_FALSE, depth + 1, &bytesProcessed))
			{
				return LVM2_FALSE;
			}
			
			err = parsed_lvm2_text_builder_leave_section(builder);
			if(err) {
				return LVM2_FALSE;
			}

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

				err = parsed_lvm2_text_builder_enter_section(
					builder, identifierToken,
					identifierTokenLen, LVM2_TRUE);
				if(err) {
					return LVM2_FALSE;
				}

				if(!parseArray(&text[i], textLen - i,
					builder, &bytesProcessed))
				{
					return LVM2_FALSE;
				}

				err = parsed_lvm2_text_builder_leave_section(
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
				const char *valueToken = token;
				int valueTokenLen = tokenLen;

				LogDebug("%s[Depth: %" FMTlu "] \"%.*s\" = "
					"\"%.*s\"",
					prefix, ARGlu(depth),
					identifierTokenLen, identifierToken,
					valueTokenLen, valueToken);
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
		const size_t textLen, struct parsed_lvm2_text **const outResult)
{
	lvm2_bool res;
	int res2;
	struct parsed_lvm2_text *result;
	struct parsed_lvm2_text_builder builder;

	parsed_lvm2_text_builder_init(&builder);
	res2 = parsed_lvm2_text_builder_enter_section(&builder, "", 0,
		LVM2_FALSE);
	if(res2) {
		LogError("Error in parsed_lvm2_text_builder_enter_section: %d",
			res2);
		return LVM2_FALSE;
	}

	res = parseDictionary(text, textLen, &builder, LVM2_TRUE, 0, NULL);

	LogDebug("Leaving section.");
	res2 = parsed_lvm2_text_builder_leave_section(&builder);
	LogDebug("  res2=%d", res2);

	result = parsed_lvm2_text_builder_finalize(&builder);
	if(!res)
		parsed_lvm2_text_destroy(&result);
	else
		*outResult = result;

	return res;
}
