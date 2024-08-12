/* cJSON */
/* JSON parser in C. */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include "cJSON.h"

static const char *ep; // 错误指针

const char *cJSON_GetErrorPtr(void) { return ep; }

static int cJSON_strcasecmp(const char *s1, const char *s2)
{
	if (!s1)
		return (s1 == s2) ? 0 : 1;
	if (!s2)
		return 1;
	for (; tolower(*s1) == tolower(*s2); ++s1, ++s2)
		if (*s1 == 0)
			return 0;
	return tolower(*(const unsigned char *)s1) - tolower(*(const unsigned char *)s2);
}

// 默认使用malloc和free作为内存分配和释放函数。
static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static char *cJSON_strdup(const char *str)
{
	size_t len;
	char *copy;

	len = strlen(str) + 1;
	if (!(copy = (char *)cJSON_malloc(len)))
		return 0;
	memcpy(copy, str, len);
	return copy;
}

void cJSON_InitHooks(cJSON_Hooks *hooks)
{
	if (!hooks)
	{ /* Reset hooks */
		cJSON_malloc = malloc;
		cJSON_free = free;
		return;
	}

	cJSON_malloc = (hooks->malloc_fn) ? hooks->malloc_fn : malloc;
	cJSON_free = (hooks->free_fn) ? hooks->free_fn : free;
}

/* JSON结构内部构造器 */
static cJSON *cJSON_New_Item(void)
{
	cJSON *node = (cJSON *)cJSON_malloc(sizeof(cJSON)); // 本质是用malloc函数分配内存。
	if (node)
		memset(node, 0, sizeof(cJSON)); // node初始化为0，初始化新分配的内存空间。
	return node;
}

/* 删除一个JSON结构体对象 */
void cJSON_Delete(cJSON *c)
{
	cJSON *next; // 用于暂存下一个节点指针。
	while (c)	 // 对象非空
	{
		next = c->next; // 暂存子节点指针。

		/*
		这里用位运算比直接判断 c->type == cJSON_IsReference 更快，
		cJSON_IsReference和cJSON_StringIsConst都是较大的二进制数，
		这里按位与操作相当于判断最高位是否是1。常规类型都是较小二进制数。
		*/
		// 如果当前节点不是引用类型并且有子节点，则递归释放子节点占用的内存
		if (!(c->type & cJSON_IsReference) && c->child)
			cJSON_Delete(c->child);
		// 如果当前节点不是引用类型并且值字符串不为空，则释放值字符串占用的内存
		if (!(c->type & cJSON_IsReference) && c->valuestring)
			cJSON_free(c->valuestring);
		// 如果当前节点的字符串不是常量并且字符串不为空，则释放字符串占用的内存
		if (!(c->type & cJSON_StringIsConst) && c->string)
			cJSON_free(c->string);
		cJSON_free(c); // 释放当前节点。
		c = next;	   // 更新循环判断条件，指向下一节点
	}
}

/* 解析输入的文本来生成一个数字，并将结果填充到item里。 */
static const char *parse_number(cJSON *item, const char *num)
{
	/*
		n用于累加解析出的数字值，sign用来存储数字的符号，scale用于记录小数点后的数字位数，
		subscale用于存储指数部分的数值，signsubscale用于存储指数部分的符号。
	*/
	double n = 0, sign = 1, scale = 0;
	int subscale = 0, signsubscale = 1;

	if (*num == '-')
		sign = -1, num++; /* 处理负号 */
	if (*num == '0')
		num++; /* 跳过首位零 */
	if (*num >= '1' && *num <= '9')
		do
			n = (n * 10.0) + (*num++ - '0');
		while (*num >= '0' && *num <= '9'); /* 获取整数部分 */
	if (*num == '.' && num[1] >= '0' && num[1] <= '9') // 小数点后有效
	{
		num++;
		do
			n = (n * 10.0) + (*num++ - '0'), scale--;
		while (*num >= '0' && *num <= '9');
	} /* 获取小数部分 */
	if (*num == 'e' || *num == 'E') /* 指数部分 */
	{
		num++;
		if (*num == '+')
			num++;
		else if (*num == '-')
			signsubscale = -1, num++; /* 有负号 */
		while (*num >= '0' && *num <= '9')
			subscale = (subscale * 10) + (*num++ - '0'); /* 获取指数部分 */
	} // bug：这样的数没有错误校验（2.3.3）

	n = sign * n * pow(10.0, (scale + subscale * signsubscale)); /* 计算最终结果 */

	item->valuedouble = n;
	item->valueint = (int)n;
	item->type = cJSON_Number;
	return num;
}

static int pow2gt(int x)
{
	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return x + 1;
}

typedef struct
{
	char *buffer;
	int length;
	int offset;
} printbuffer;

static char *ensure(printbuffer *p, int needed)
{
	char *newbuffer;
	int newsize;
	if (!p || !p->buffer)
		return 0;
	needed += p->offset;
	if (needed <= p->length)
		return p->buffer + p->offset;

	newsize = pow2gt(needed);
	newbuffer = (char *)cJSON_malloc(newsize);
	if (!newbuffer)
	{
		cJSON_free(p->buffer);
		p->length = 0, p->buffer = 0;
		return 0;
	}
	if (newbuffer)
		memcpy(newbuffer, p->buffer, p->length);
	cJSON_free(p->buffer);
	p->length = newsize;
	p->buffer = newbuffer;
	return newbuffer + p->offset;
}

static int update(printbuffer *p)
{
	char *str;
	if (!p || !p->buffer)
		return 0;
	str = p->buffer + p->offset;
	return p->offset + strlen(str);
}

/* Render the number nicely from the given item into a string. */
static char *print_number(cJSON *item, printbuffer *p)
{
	char *str = 0;
	double d = item->valuedouble;
	if (d == 0)
	{
		if (p)
			str = ensure(p, 2);
		else
			str = (char *)cJSON_malloc(2); /* special case for 0. */
		if (str)
			strcpy(str, "0");
	}
	else if (fabs(((double)item->valueint) - d) <= DBL_EPSILON && d <= INT_MAX && d >= INT_MIN)
	{
		if (p)
			str = ensure(p, 21);
		else
			str = (char *)cJSON_malloc(21); /* 2^64+1 can be represented in 21 chars. */
		if (str)
			sprintf(str, "%d", item->valueint);
	}
	else
	{
		if (p)
			str = ensure(p, 64);
		else
			str = (char *)cJSON_malloc(64); /* This is a nice tradeoff. */
		if (str)
		{
			if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60)
				sprintf(str, "%.0f", d);
			else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9)
				sprintf(str, "%e", d);
			else
				sprintf(str, "%f", d);
		}
	}
	return str;
}

/* 解析一个长度为4的十六进制字符串，并返回对应的无符号整数。*/
static unsigned parse_hex4(const char *str)
{
	unsigned h = 0;
	if (*str >= '0' && *str <= '9') // 解析第一位
		h += (*str) - '0';
	else if (*str >= 'A' && *str <= 'F')
		h += 10 + (*str) - 'A';
	else if (*str >= 'a' && *str <= 'f')
		h += 10 + (*str) - 'a';
	else // 非十六进制字符，解析失败
		return 0;
	h = h << 4; // 将已解析的部分左移4位，为下一个字符做准备
	str++;		// 解析第二位
	if (*str >= '0' && *str <= '9')
		h += (*str) - '0';
	else if (*str >= 'A' && *str <= 'F')
		h += 10 + (*str) - 'A';
	else if (*str >= 'a' && *str <= 'f')
		h += 10 + (*str) - 'a';
	else
		return 0;
	h = h << 4;
	str++;
	if (*str >= '0' && *str <= '9')
		h += (*str) - '0';
	else if (*str >= 'A' && *str <= 'F')
		h += 10 + (*str) - 'A';
	else if (*str >= 'a' && *str <= 'f')
		h += 10 + (*str) - 'a';
	else
		return 0;
	h = h << 4;
	str++;
	if (*str >= '0' && *str <= '9')
		h += (*str) - '0';
	else if (*str >= 'A' && *str <= 'F')
		h += 10 + (*str) - 'A';
	else if (*str >= 'a' && *str <= 'f')
		h += 10 + (*str) - 'a';
	else
		return 0;
	return h; // 返回解析得到的十六进制数值。
}

/* 用于解析输入文本为未转义的C字符串，填充到一个名为 item 的结构中。
通过检查输入文本中每个字符的第一个字节与这些标志的匹配情况，
可以确定字符的编码长度并进行相应的解析处理。 */
static const unsigned char firstByteMark[7] = {0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC};
static const char *parse_string(cJSON *item, const char *str)
{
	const char *ptr = str + 1; // 跳过第一个匹配字符串的引号
	char *ptr2;				   // 用于字符串赋值时存储字符串指针
	char *out;				   // 用于存储输出字符串
	int len = 0;			   // 记录字符串长度
	unsigned uc, uc2;		   // 用于存储Unicode字符，uc2用于存储低位代理字符
	if (*str != '\"')
	{
		ep = str;
		return 0;
	} /* 不是string，匹配失败! */

	// ptr不是字符串结束的双引号 (")，不是空字符（'\0' C 语言中字符串是以空字符结尾 ）同时对字符串长度累加
	while (*ptr != '\"' && *ptr && ++len)
		if (*ptr++ == '\\')
			ptr++; /* 跳过转义符 */

	out = (char *)cJSON_malloc(len + 1); /* 根据字符串长度分配内存 */
	if (!out)
		return 0; // 内存分配失败

	ptr = str + 1; // 重置指针位置
	ptr2 = out;
	while (*ptr != '\"' && *ptr) // 将字符串有效内容复制到ptr2
	{
		if (*ptr != '\\') // 只要不是转义符，直接复制
			*ptr2++ = *ptr++;
		else // 是转义符，判断转义符类型
		{
			ptr++; // 跳过转义符
			switch (*ptr)
			{
			case 'b':
				*ptr2++ = '\b';
				break;
			case 'f':
				*ptr2++ = '\f';
				break;
			case 'n':
				*ptr2++ = '\n';
				break;
			case 'r':
				*ptr2++ = '\r';
				break;
			case 't':
				*ptr2++ = '\t';
				break;
			case 'u':					  /* 将UTF-16转换为UTF-8. */
				uc = parse_hex4(ptr + 1); // 将由字符串解析后的16进制数赋给uc
				ptr += 4;				  /* 指针后移，跳过已解析字符 */

				if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0)
					break; /* 检查无效的Unicode字符，0xDC00 到 0xDFFF 之间的字符
					是UTF-16编码中的低位代理区（Low Surrogate）。低位代理字符本身不能独立存在，
					必须与高位代理字符（范围 0xD800 到 0xDBFF）结合使用，
					组成一个有效的Unicode码点。如果低位代理字符单独出现，它就是无效的。 */

				if (uc >= 0xD800 && uc <= 0xDBFF) /* 处理 UTF16 代理对.	*/
				{
					if (ptr[1] != '\\' || ptr[2] != 'u') // 高位代理对
						break;							 /* 缺少低位代理对 */
					uc2 = parse_hex4(ptr + 3);			 // 解析低位代理对
					ptr += 6;							 // 指针后移，跳过已解析字符
					if (uc2 < 0xDC00 || uc2 > 0xDFFF)
						break; /* 无效的低位代理对	*/
					/*
						合并代理对：
							uc & 0x3FF: 提取高代理对的最后10位。
							(uc & 0x3FF) << 10: 将高代理对的最后10位左移10位。
							uc2 & 0x3FF: 提取低代理对的最后10位。
							((uc & 0x3FF) << 10) | (uc2 & 0x3FF): 将两个10位数字合并起来。
							0x10000 + ...: 加上 0x10000 来形成完整的增补平面的Unicode代码点。
					*/
					uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF)); // 高低代理对组合得到完整的代码点
				}

				len = 4;
				/*
				   UTF-8 的编码规则如下：
					代码点在 U+0000 到 U+007F 之间的字符使用 1 字节编码。
					代码点在 U+0080 到 U+07FF 之间的字符使用 2 字节编码。
					代码点在 U+0800 到 U+FFFF 之间的字符使用 3 字节编码。
					代码点在 U+10000 到 U+10FFFF 之间的字符使用 4 字节编码。
				*/
				if (uc < 0x80)
					len = 1;
				else if (uc < 0x800)
					len = 2;
				else if (uc < 0x10000)
					len = 3;
				ptr2 += len; // 为当前处理的 Unicode 字符预留空间

				switch (len) // 利用switch语句的贯穿特性处理不同长度的UTF-8编码
				{
				case 4:									 // 对于需要4字节编码的Unicode字符
					*--ptr2 = ((uc | 0x80) & 0xBF);		 // 写入第4个字节
					uc >>= 6;							 // 将Unicode字符右移6位，准备写入下一个字节
				case 3:									 // 对于需要3字节编码的Unicode字符
					*--ptr2 = ((uc | 0x80) & 0xBF);		 // 写入第3个字节
					uc >>= 6;							 // 将Unicode字符右移6位，准备写入下一个字节
				case 2:									 // 对于需要2字节编码的Unicode字符
					*--ptr2 = ((uc | 0x80) & 0xBF);		 // 写入第2个字节
					uc >>= 6;							 // 将Unicode字符右移6位，准备写入下一个字节
				case 1:									 // 对于需要1字节编码的Unicode字符
					*--ptr2 = (uc | firstByteMark[len]); // 写入第1个字节
				}
				ptr2 += len; // 指针复位
				break;
			default:
				*ptr2++ = *ptr;
				break;
			}
			ptr++; // 指针后移，指向下一个待处理的字符
		}
	}
	*ptr2 = 0;		  // 字符串结束，将字符串结束符添加到输出字符串末尾
	if (*ptr == '\"') // 当前解析的字符串类型结束，完整的JSON还未解析完
		ptr++;		  // 解析下一类型，指向下一个待处理的字符
	item->valuestring = out;
	item->type = cJSON_String;
	return ptr;
}

/* Render the cstring provided to an escaped version that can be printed. */
static char *print_string_ptr(const char *str, printbuffer *p)
{
	const char *ptr;
	char *ptr2, *out;
	int len = 0, flag = 0;
	unsigned char token;

	for (ptr = str; *ptr; ptr++)
		flag |= ((*ptr > 0 && *ptr < 32) || (*ptr == '\"') || (*ptr == '\\')) ? 1 : 0;
	if (!flag)
	{
		len = ptr - str;
		if (p)
			out = ensure(p, len + 3);
		else
			out = (char *)cJSON_malloc(len + 3);
		if (!out)
			return 0;
		ptr2 = out;
		*ptr2++ = '\"';
		strcpy(ptr2, str);
		ptr2[len] = '\"';
		ptr2[len + 1] = 0;
		return out;
	}

	if (!str)
	{
		if (p)
			out = ensure(p, 3);
		else
			out = (char *)cJSON_malloc(3);
		if (!out)
			return 0;
		strcpy(out, "\"\"");
		return out;
	}
	ptr = str;
	while ((token = *ptr) && ++len)
	{
		if (strchr("\"\\\b\f\n\r\t", token))
			len++;
		else if (token < 32)
			len += 5;
		ptr++;
	}

	if (p)
		out = ensure(p, len + 3);
	else
		out = (char *)cJSON_malloc(len + 3);
	if (!out)
		return 0;

	ptr2 = out;
	ptr = str;
	*ptr2++ = '\"';
	while (*ptr)
	{
		if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\')
			*ptr2++ = *ptr++;
		else
		{
			*ptr2++ = '\\';
			switch (token = *ptr++)
			{
			case '\\':
				*ptr2++ = '\\';
				break;
			case '\"':
				*ptr2++ = '\"';
				break;
			case '\b':
				*ptr2++ = 'b';
				break;
			case '\f':
				*ptr2++ = 'f';
				break;
			case '\n':
				*ptr2++ = 'n';
				break;
			case '\r':
				*ptr2++ = 'r';
				break;
			case '\t':
				*ptr2++ = 't';
				break;
			default:
				sprintf(ptr2, "u%04x", token);
				ptr2 += 5;
				break; /* escape and print */
			}
		}
	}
	*ptr2++ = '\"';
	*ptr2++ = 0;
	return out;
}
/* Invote print_string_ptr (which is useful) on an item. */
static char *print_string(cJSON *item, printbuffer *p) { return print_string_ptr(item->valuestring, p); }

/* Predeclare these prototypes. */
static const char *parse_value(cJSON *item, const char *value);
static char *print_value(cJSON *item, int depth, int fmt, printbuffer *p);
static const char *parse_array(cJSON *item, const char *value);
static char *print_array(cJSON *item, int depth, int fmt, printbuffer *p);
static const char *parse_object(cJSON *item, const char *value);
static char *print_object(cJSON *item, int depth, int fmt, printbuffer *p);

/* 用于跳过空白字符以及回车换行字符的工具函数。 */
static const char *skip(const char *in)
{
	// 判断输入字符串指针是否为空，以及字符串中当前字符是否为空，以及当前字符是否为不可见字符。
	while (in && *in && (unsigned char)*in <= 32)
		in++; // 跳过以上字符
	return in;
}

/* 解析一个对象 - 创建一个新的根节点，并填充数据。 */
cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, int require_null_terminated) // mark:3
{
	// end用于记录解析结束时的位置。
	const char *end = 0;
	// 创建一个新的cJSON对象，用于返回。
	cJSON *c = cJSON_New_Item();
	// 初始化ep为0，ep用于记录解析过程中的错误位置。
	ep = 0;
	if (!c)
		return 0; /* 创建失败 */

	end = parse_value(c, skip(value)); // 把解析后返回的字符串指针赋值给end。
	if (!end)						   // 空指针说明解析失败
	{
		cJSON_Delete(c); // 释放创建失败的cJSON对象
		return 0;		 // 返回空指针表示解析失败
	}

	/* if we require null-terminated JSON without appended garbage, skip and then check for a null terminator */
	if (require_null_terminated)
	{
		end = skip(end);
		if (*end)
		{
			cJSON_Delete(c);
			ep = end;
			return 0;
		}
	}
	if (return_parse_end)
		*return_parse_end = end;
	return c;
}
/* cJSON_Parse的默认选项 */
cJSON *cJSON_Parse(const char *value) { return cJSON_ParseWithOpts(value, 0, 0); } // mark:2

/* Render a cJSON item/entity/structure to text. */
char *cJSON_Print(cJSON *item) { return print_value(item, 0, 1, 0); }
char *cJSON_PrintUnformatted(cJSON *item) { return print_value(item, 0, 0, 0); }

char *cJSON_PrintBuffered(cJSON *item, int prebuffer, int fmt)
{
	printbuffer p;
	p.buffer = (char *)cJSON_malloc(prebuffer);
	p.length = prebuffer;
	p.offset = 0;
	return print_value(item, 0, fmt, &p);
	return p.buffer;
}

/* 解析器核心 - 当遇到文本时，适当处理。 */
static const char *parse_value(cJSON *item, const char *value)
{
	if (!value)
		return 0; /* 空值失败 */
				  // strncmp 函数用于比较两个字符串的前 n 个字符，不考虑终止的空字符。
	if (!strncmp(value, "null", 4))
	{
		item->type = cJSON_NULL;
		return value + 4; // 更新解析结束位置
	}
	if (!strncmp(value, "false", 5))
	{
		item->type = cJSON_False;
		return value + 5;
	}
	if (!strncmp(value, "true", 4))
	{
		item->type = cJSON_True;
		item->valueint = 1; // 赋值
		return value + 4;
	}
	if (*value == '\"') // " 开头表字符串，调用parse_string函数
	{
		return parse_string(item, value);
	}
	if (*value == '-' || (*value >= '0' && *value <= '9')) // 如果是数字或负号，解析为数字
	{
		return parse_number(item, value);
	}
	if (*value == '[') // 如果是左中括号，解析为数组
	{
		return parse_array(item, value);
	}
	if (*value == '{') // 如果是左大括号，解析为对象
	{
		return parse_object(item, value);
	}

	ep = value; // 指向解析失败的字符位置
	return 0;	/* 失败 */
}

/* Render a value to text. */
static char *print_value(cJSON *item, int depth, int fmt, printbuffer *p)
{
	char *out = 0;
	if (!item)
		return 0;
	if (p)
	{
		switch ((item->type) & 255)
		{
		case cJSON_NULL:
		{
			out = ensure(p, 5);
			if (out)
				strcpy(out, "null");
			break;
		}
		case cJSON_False:
		{
			out = ensure(p, 6);
			if (out)
				strcpy(out, "false");
			break;
		}
		case cJSON_True:
		{
			out = ensure(p, 5);
			if (out)
				strcpy(out, "true");
			break;
		}
		case cJSON_Number:
			out = print_number(item, p);
			break;
		case cJSON_String:
			out = print_string(item, p);
			break;
		case cJSON_Array:
			out = print_array(item, depth, fmt, p);
			break;
		case cJSON_Object:
			out = print_object(item, depth, fmt, p);
			break;
		}
	}
	else
	{
		switch ((item->type) & 255)
		{
		case cJSON_NULL:
			out = cJSON_strdup("null");
			break;
		case cJSON_False:
			out = cJSON_strdup("false");
			break;
		case cJSON_True:
			out = cJSON_strdup("true");
			break;
		case cJSON_Number:
			out = print_number(item, 0);
			break;
		case cJSON_String:
			out = print_string(item, 0);
			break;
		case cJSON_Array:
			out = print_array(item, depth, fmt, 0);
			break;
		case cJSON_Object:
			out = print_object(item, depth, fmt, 0);
			break;
		}
	}
	return out;
}

/* 根据输入文本构建一个数组 */
static const char *parse_array(cJSON *item, const char *value)
{
	cJSON *child;
	if (*value != '[')
	{
		ep = value;
		return 0;
	} /* 不是数组 */

	item->type = cJSON_Array; // 设置当前项的类型为数组
	value = skip(value + 1);  // 跳过左括号和一些空白字符
	if (*value == ']')		  // 数组结束标志
		return value + 1;	  /* 空数组直接返回 */

	item->child = child = cJSON_New_Item(); // 为child分配空间，并将item的子指针指向child
	if (!item->child)
		return 0;								   /* 内存分配失败 */
	value = skip(parse_value(child, skip(value))); /* 跳过空白字符，将数组中解析的值赋给child后返回下一位置 */
	if (!value)
		return 0; // 空指针，解析失败

	// 匹配到逗号，说明还有元素，继续解析数组中其他元素，以链表的方式插入元素
	while (*value == ',')
	{
		cJSON *new_item;
		if (!(new_item = cJSON_New_Item()))
			return 0;									   /* 为新元素分配空间失败 */
		child->next = new_item;							   // 数组间成员用next链接，区别于上面的child
		new_item->prev = child;							   // 将新元素插入数组
		child = new_item;								   // 更新child指针
		value = skip(parse_value(child, skip(value + 1))); // 为新元素赋值
		if (!value)
			return 0; /* 同上解析失败 */
	}

	if (*value == ']')	  // 匹配到右括号，说明数组解析结束
		return value + 1; /* 跳过右括号，更新解析结束位置并返回 */
	ep = value;
	return 0; /* 非正常的情况，一般是数组的格式不正确 */
}

/* Render an array to text */
static char *print_array(cJSON *item, int depth, int fmt, printbuffer *p)
{
	char **entries;
	char *out = 0, *ptr, *ret;
	int len = 5;
	cJSON *child = item->child;
	int numentries = 0, i = 0, fail = 0;
	size_t tmplen = 0;

	/* How many entries in the array? */
	while (child)
		numentries++, child = child->next;
	/* Explicitly handle numentries==0 */
	if (!numentries)
	{
		if (p)
			out = ensure(p, 3);
		else
			out = (char *)cJSON_malloc(3);
		if (out)
			strcpy(out, "[]");
		return out;
	}

	if (p)
	{
		/* Compose the output array. */
		i = p->offset;
		ptr = ensure(p, 1);
		if (!ptr)
			return 0;
		*ptr = '[';
		p->offset++;
		child = item->child;
		while (child && !fail)
		{
			print_value(child, depth + 1, fmt, p);
			p->offset = update(p);
			if (child->next)
			{
				len = fmt ? 2 : 1;
				ptr = ensure(p, len + 1);
				if (!ptr)
					return 0;
				*ptr++ = ',';
				if (fmt)
					*ptr++ = ' ';
				*ptr = 0;
				p->offset += len;
			}
			child = child->next;
		}
		ptr = ensure(p, 2);
		if (!ptr)
			return 0;
		*ptr++ = ']';
		*ptr = 0;
		out = (p->buffer) + i;
	}
	else
	{
		/* Allocate an array to hold the values for each */
		entries = (char **)cJSON_malloc(numentries * sizeof(char *));
		if (!entries)
			return 0;
		memset(entries, 0, numentries * sizeof(char *));
		/* Retrieve all the results: */
		child = item->child;
		while (child && !fail)
		{
			ret = print_value(child, depth + 1, fmt, 0);
			entries[i++] = ret;
			if (ret)
				len += strlen(ret) + 2 + (fmt ? 1 : 0);
			else
				fail = 1;
			child = child->next;
		}

		/* If we didn't fail, try to malloc the output string */
		if (!fail)
			out = (char *)cJSON_malloc(len);
		/* If that fails, we fail. */
		if (!out)
			fail = 1;

		/* Handle failure. */
		if (fail)
		{
			for (i = 0; i < numentries; i++)
				if (entries[i])
					cJSON_free(entries[i]);
			cJSON_free(entries);
			return 0;
		}

		/* Compose the output array. */
		*out = '[';
		ptr = out + 1;
		*ptr = 0;
		for (i = 0; i < numentries; i++)
		{
			tmplen = strlen(entries[i]);
			memcpy(ptr, entries[i], tmplen);
			ptr += tmplen;
			if (i != numentries - 1)
			{
				*ptr++ = ',';
				if (fmt)
					*ptr++ = ' ';
				*ptr = 0;
			}
			cJSON_free(entries[i]);
		}
		cJSON_free(entries);
		*ptr++ = ']';
		*ptr++ = 0;
	}
	return out;
}

/* 根据文本构建对象 */
static const char *parse_object(cJSON *item, const char *value)
{
	cJSON *child;
	if (*value != '{')
	{
		ep = value;
		return 0;
	} /* 不是对象 */

	item->type = cJSON_Object; // 设置当前项的类型为对象
	value = skip(value + 1);   // 跳过左括号和一些空白字符
	if (*value == '}')
		return value + 1; /* 空对象直接返回 */

	item->child = child = cJSON_New_Item(); // 为child分配空间，并将item的子指针指向child
	if (!item->child)
		return 0;									// 内存分配失败
	value = skip(parse_string(child, skip(value))); // 跳过空白字符，将对象中解析的字符串赋给child后更新value指针
	if (!value)
		return 0;						// 未匹配右括号value就结束了，解析失败
	child->string = child->valuestring; // 把解析到的字符串赋给第一个键名
	child->valuestring = 0;				// 清空第一个键名所对应的值
	if (*value != ':')
	{
		ep = value;
		return 0;
	} /* 非对象的情况，键名后面没有冒号，解析失败 */
	value = skip(parse_value(child, skip(value + 1))); /* 跳过冒号与空白字符，将冒号后解析的值赋给child后跳过空白字符，返回下一位置 */
	if (!value)
		return 0; // 未匹配右括号value就结束了，解析失败

	while (*value == ',') // 匹配到逗号，继续解析对象中其他键值对，基本与上同
	{
		cJSON *new_item;
		if (!(new_item = cJSON_New_Item()))					// 为new_item分配空间，用来存储下一个键值对
			return 0;										/* 内存分配失败 */
		child->next = new_item;								// 对象间成员用next链接，区别于上面的child
		new_item->prev = child;								// 将新元素插入对象
		child = new_item;									// 更新child指针
		value = skip(parse_string(child, skip(value + 1))); // 跳过分隔逗号和空白字符，将键名赋给child后返回下一位置
		if (!value)
			return 0;
		child->string = child->valuestring;
		child->valuestring = 0;
		if (*value != ':')
		{
			ep = value;
			return 0;
		} /* 失败! */
		value = skip(parse_value(child, skip(value + 1))); /* 解析并赋键值 */
		if (!value)
			return 0;
	}

	if (*value == '}')
		return value + 1; /* 对象解析结束 */
	ep = value;
	return 0; /* 格式错误，更新错误指针 */
}

/* Render an object to text. */
static char *print_object(cJSON *item, int depth, int fmt, printbuffer *p)
{
	char **entries = 0, **names = 0;
	char *out = 0, *ptr, *ret, *str;
	int len = 7, i = 0, j;
	cJSON *child = item->child;
	int numentries = 0, fail = 0;
	size_t tmplen = 0;
	/* Count the number of entries. */
	while (child)
		numentries++, child = child->next;
	/* Explicitly handle empty object case */
	if (!numentries)
	{
		if (p)
			out = ensure(p, fmt ? depth + 4 : 3);
		else
			out = (char *)cJSON_malloc(fmt ? depth + 4 : 3);
		if (!out)
			return 0;
		ptr = out;
		*ptr++ = '{';
		if (fmt)
		{
			*ptr++ = '\n';
			for (i = 0; i < depth - 1; i++)
				*ptr++ = '\t';
		}
		*ptr++ = '}';
		*ptr++ = 0;
		return out;
	}
	if (p)
	{
		/* Compose the output: */
		i = p->offset;
		len = fmt ? 2 : 1;
		ptr = ensure(p, len + 1);
		if (!ptr)
			return 0;
		*ptr++ = '{';
		if (fmt)
			*ptr++ = '\n';
		*ptr = 0;
		p->offset += len;
		child = item->child;
		depth++;
		while (child)
		{
			if (fmt)
			{
				ptr = ensure(p, depth);
				if (!ptr)
					return 0;
				for (j = 0; j < depth; j++)
					*ptr++ = '\t';
				p->offset += depth;
			}
			print_string_ptr(child->string, p);
			p->offset = update(p);

			len = fmt ? 2 : 1;
			ptr = ensure(p, len);
			if (!ptr)
				return 0;
			*ptr++ = ':';
			if (fmt)
				*ptr++ = '\t';
			p->offset += len;

			print_value(child, depth, fmt, p);
			p->offset = update(p);

			len = (fmt ? 1 : 0) + (child->next ? 1 : 0);
			ptr = ensure(p, len + 1);
			if (!ptr)
				return 0;
			if (child->next)
				*ptr++ = ',';
			if (fmt)
				*ptr++ = '\n';
			*ptr = 0;
			p->offset += len;
			child = child->next;
		}
		ptr = ensure(p, fmt ? (depth + 1) : 2);
		if (!ptr)
			return 0;
		if (fmt)
			for (i = 0; i < depth - 1; i++)
				*ptr++ = '\t';
		*ptr++ = '}';
		*ptr = 0;
		out = (p->buffer) + i;
	}
	else
	{
		/* Allocate space for the names and the objects */
		entries = (char **)cJSON_malloc(numentries * sizeof(char *));
		if (!entries)
			return 0;
		names = (char **)cJSON_malloc(numentries * sizeof(char *));
		if (!names)
		{
			cJSON_free(entries);
			return 0;
		}
		memset(entries, 0, sizeof(char *) * numentries);
		memset(names, 0, sizeof(char *) * numentries);

		/* Collect all the results into our arrays: */
		child = item->child;
		depth++;
		if (fmt)
			len += depth;
		while (child)
		{
			names[i] = str = print_string_ptr(child->string, 0);
			entries[i++] = ret = print_value(child, depth, fmt, 0);
			if (str && ret)
				len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0);
			else
				fail = 1;
			child = child->next;
		}

		/* Try to allocate the output string */
		if (!fail)
			out = (char *)cJSON_malloc(len);
		if (!out)
			fail = 1;

		/* Handle failure */
		if (fail)
		{
			for (i = 0; i < numentries; i++)
			{
				if (names[i])
					cJSON_free(names[i]);
				if (entries[i])
					cJSON_free(entries[i]);
			}
			cJSON_free(names);
			cJSON_free(entries);
			return 0;
		}

		/* Compose the output: */
		*out = '{';
		ptr = out + 1;
		if (fmt)
			*ptr++ = '\n';
		*ptr = 0;
		for (i = 0; i < numentries; i++)
		{
			if (fmt)
				for (j = 0; j < depth; j++)
					*ptr++ = '\t';
			tmplen = strlen(names[i]);
			memcpy(ptr, names[i], tmplen);
			ptr += tmplen;
			*ptr++ = ':';
			if (fmt)
				*ptr++ = '\t';
			strcpy(ptr, entries[i]);
			ptr += strlen(entries[i]);
			if (i != numentries - 1)
				*ptr++ = ',';
			if (fmt)
				*ptr++ = '\n';
			*ptr = 0;
			cJSON_free(names[i]);
			cJSON_free(entries[i]);
		}

		cJSON_free(names);
		cJSON_free(entries);
		if (fmt)
			for (i = 0; i < depth - 1; i++)
				*ptr++ = '\t';
		*ptr++ = '}';
		*ptr++ = 0;
	}
	return out;
}

/* Get Array size/item / object item. */
int cJSON_GetArraySize(cJSON *array)
{
	cJSON *c = array->child;
	int i = 0;
	while (c)
		i++, c = c->next;
	return i;
}
cJSON *cJSON_GetArrayItem(cJSON *array, int item)
{
	cJSON *c = array->child;
	while (c && item > 0)
		item--, c = c->next;
	return c;
}
cJSON *cJSON_GetObjectItem(cJSON *object, const char *string)
{
	cJSON *c = object->child;
	while (c && cJSON_strcasecmp(c->string, string))
		c = c->next;
	return c;
}

/* Utility for array list handling. */
static void suffix_object(cJSON *prev, cJSON *item)
{
	prev->next = item;
	item->prev = prev;
}
/* Utility for handling references. */
static cJSON *create_reference(cJSON *item)
{
	cJSON *ref = cJSON_New_Item();
	if (!ref)
		return 0;
	memcpy(ref, item, sizeof(cJSON));
	ref->string = 0;
	ref->type |= cJSON_IsReference;
	ref->next = ref->prev = 0;
	return ref;
}

/* Add item to array/object. */
void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
	cJSON *c = array->child;
	if (!item)
		return;
	if (!c)
	{
		array->child = item;
	}
	else
	{
		while (c && c->next)
			c = c->next;
		suffix_object(c, item);
	}
}
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
	if (!item)
		return;
	if (item->string)
		cJSON_free(item->string);
	item->string = cJSON_strdup(string);
	cJSON_AddItemToArray(object, item);
}
void cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
	if (!item)
		return;
	if (!(item->type & cJSON_StringIsConst) && item->string)
		cJSON_free(item->string);
	item->string = (char *)string;
	item->type |= cJSON_StringIsConst;
	cJSON_AddItemToArray(object, item);
}
void cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item) { cJSON_AddItemToArray(array, create_reference(item)); }
void cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item) { cJSON_AddItemToObject(object, string, create_reference(item)); }

cJSON *cJSON_DetachItemFromArray(cJSON *array, int which)
{
	cJSON *c = array->child;
	while (c && which > 0)
		c = c->next, which--;
	if (!c)
		return 0;
	if (c->prev)
		c->prev->next = c->next;
	if (c->next)
		c->next->prev = c->prev;
	if (c == array->child)
		array->child = c->next;
	c->prev = c->next = 0;
	return c;
}
void cJSON_DeleteItemFromArray(cJSON *array, int which) { cJSON_Delete(cJSON_DetachItemFromArray(array, which)); }
cJSON *cJSON_DetachItemFromObject(cJSON *object, const char *string)
{
	int i = 0;
	cJSON *c = object->child;
	while (c && cJSON_strcasecmp(c->string, string))
		i++, c = c->next;
	if (c)
		return cJSON_DetachItemFromArray(object, i);
	return 0;
}
void cJSON_DeleteItemFromObject(cJSON *object, const char *string) { cJSON_Delete(cJSON_DetachItemFromObject(object, string)); }

/* Replace array/object items with new ones. */
void cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem)
{
	cJSON *c = array->child;
	while (c && which > 0)
		c = c->next, which--;
	if (!c)
	{
		cJSON_AddItemToArray(array, newitem);
		return;
	}
	newitem->next = c;
	newitem->prev = c->prev;
	c->prev = newitem;
	if (c == array->child)
		array->child = newitem;
	else
		newitem->prev->next = newitem;
}
void cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem)
{
	cJSON *c = array->child;
	while (c && which > 0)
		c = c->next, which--;
	if (!c)
		return;
	newitem->next = c->next;
	newitem->prev = c->prev;
	if (newitem->next)
		newitem->next->prev = newitem;
	if (c == array->child)
		array->child = newitem;
	else
		newitem->prev->next = newitem;
	c->next = c->prev = 0;
	cJSON_Delete(c);
}
void cJSON_ReplaceItemInObject(cJSON *object, const char *string, cJSON *newitem)
{
	int i = 0;
	cJSON *c = object->child;
	while (c && cJSON_strcasecmp(c->string, string))
		i++, c = c->next;
	if (c)
	{
		newitem->string = cJSON_strdup(string);
		cJSON_ReplaceItemInArray(object, i, newitem);
	}
}

/* Create basic types: */
cJSON *cJSON_CreateNull(void)
{
	cJSON *item = cJSON_New_Item();
	if (item)
		item->type = cJSON_NULL;
	return item;
}
cJSON *cJSON_CreateTrue(void)
{
	cJSON *item = cJSON_New_Item();
	if (item)
		item->type = cJSON_True;
	return item;
}
cJSON *cJSON_CreateFalse(void)
{
	cJSON *item = cJSON_New_Item();
	if (item)
		item->type = cJSON_False;
	return item;
}
cJSON *cJSON_CreateBool(int b)
{
	cJSON *item = cJSON_New_Item();
	if (item)
		item->type = b ? cJSON_True : cJSON_False;
	return item;
}
cJSON *cJSON_CreateNumber(double num)
{
	cJSON *item = cJSON_New_Item();
	if (item)
	{
		item->type = cJSON_Number;
		item->valuedouble = num;
		item->valueint = (int)num;
	}
	return item;
}
cJSON *cJSON_CreateString(const char *string)
{
	cJSON *item = cJSON_New_Item();
	if (item)
	{
		item->type = cJSON_String;
		item->valuestring = cJSON_strdup(string);
	}
	return item;
}
cJSON *cJSON_CreateArray(void)
{
	cJSON *item = cJSON_New_Item();
	if (item)
		item->type = cJSON_Array;
	return item;
}
cJSON *cJSON_CreateObject(void)
{
	cJSON *item = cJSON_New_Item();
	if (item)
		item->type = cJSON_Object;
	return item;
}

/* Create Arrays: */
cJSON *cJSON_CreateIntArray(const int *numbers, int count)
{
	int i;
	cJSON *n = 0, *p = 0, *a = cJSON_CreateArray();
	for (i = 0; a && i < count; i++)
	{
		n = cJSON_CreateNumber(numbers[i]);
		if (!i)
			a->child = n;
		else
			suffix_object(p, n);
		p = n;
	}
	return a;
}
cJSON *cJSON_CreateFloatArray(const float *numbers, int count)
{
	int i;
	cJSON *n = 0, *p = 0, *a = cJSON_CreateArray();
	for (i = 0; a && i < count; i++)
	{
		n = cJSON_CreateNumber(numbers[i]);
		if (!i)
			a->child = n;
		else
			suffix_object(p, n);
		p = n;
	}
	return a;
}
cJSON *cJSON_CreateDoubleArray(const double *numbers, int count)
{
	int i;
	cJSON *n = 0, *p = 0, *a = cJSON_CreateArray();
	for (i = 0; a && i < count; i++)
	{
		n = cJSON_CreateNumber(numbers[i]);
		if (!i)
			a->child = n;
		else
			suffix_object(p, n);
		p = n;
	}
	return a;
}
cJSON *cJSON_CreateStringArray(const char **strings, int count)
{
	int i;
	cJSON *n = 0, *p = 0, *a = cJSON_CreateArray();
	for (i = 0; a && i < count; i++)
	{
		n = cJSON_CreateString(strings[i]);
		if (!i)
			a->child = n;
		else
			suffix_object(p, n);
		p = n;
	}
	return a;
}

/* Duplication */
cJSON *cJSON_Duplicate(cJSON *item, int recurse)
{
	cJSON *newitem, *cptr, *nptr = 0, *newchild;
	/* Bail on bad ptr */
	if (!item)
		return 0;
	/* Create new item */
	newitem = cJSON_New_Item();
	if (!newitem)
		return 0;
	/* Copy over all vars */
	newitem->type = item->type & (~cJSON_IsReference), newitem->valueint = item->valueint, newitem->valuedouble = item->valuedouble;
	if (item->valuestring)
	{
		newitem->valuestring = cJSON_strdup(item->valuestring);
		if (!newitem->valuestring)
		{
			cJSON_Delete(newitem);
			return 0;
		}
	}
	if (item->string)
	{
		newitem->string = cJSON_strdup(item->string);
		if (!newitem->string)
		{
			cJSON_Delete(newitem);
			return 0;
		}
	}
	/* If non-recursive, then we're done! */
	if (!recurse)
		return newitem;
	/* Walk the ->next chain for the child. */
	cptr = item->child;
	while (cptr)
	{
		newchild = cJSON_Duplicate(cptr, 1); /* Duplicate (with recurse) each item in the ->next chain */
		if (!newchild)
		{
			cJSON_Delete(newitem);
			return 0;
		}
		if (nptr)
		{
			nptr->next = newchild, newchild->prev = nptr;
			nptr = newchild;
		} /* If newitem->child already set, then crosswire ->prev and ->next and move on */
		else
		{
			newitem->child = newchild;
			nptr = newchild;
		} /* Set newitem->child and move to it */
		cptr = cptr->next;
	}
	return newitem;
}

void cJSON_Minify(char *json)
{
	char *into = json;
	while (*json)
	{
		if (*json == ' ')
			json++;
		else if (*json == '\t')
			json++; /* Whitespace characters. */
		else if (*json == '\r')
			json++;
		else if (*json == '\n')
			json++;
		else if (*json == '/' && json[1] == '/')
			while (*json && *json != '\n')
				json++; /* double-slash comments, to end of line. */
		else if (*json == '/' && json[1] == '*')
		{
			while (*json && !(*json == '*' && json[1] == '/'))
				json++;
			json += 2;
		} /* multiline comments. */
		else if (*json == '\"')
		{
			*into++ = *json++;
			while (*json && *json != '\"')
			{
				if (*json == '\\')
					*into++ = *json++;
				*into++ = *json++;
			}
			*into++ = *json++;
		} /* string literals, which are \" sensitive. */
		else
			*into++ = *json++; /* All other characters. */
	}
	*into = 0; /* and null-terminate. */
}
