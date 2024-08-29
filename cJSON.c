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

const char *cJSON_GetErrorPtr(void) { return ep; } // 获取错误指针，该指针指向出现错误的第一个字符

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

/* 手动分配内存渲染字符串 */
static char *cJSON_strdup(const char *str)
{
	size_t len; // 记录字符串长度
	char *copy; // 存储复制后的字符串

	len = strlen(str) + 1; // 加上结束字符'\0'
	if (!(copy = (char *)cJSON_malloc(len)))
		return 0;			// 分配失败
	memcpy(copy, str, len); // 复制字符串
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

	item->valuedouble = n;	 // valuedouble存精确值
	item->valueint = (int)n; // valueint存整数值
	item->type = cJSON_Number;
	return num;
}

/* 计算大于或等于x的最小的2的幂次方 */
static int pow2gt(int x) // 本质是将二进制数的最高位1赋给所有位后再加1
{
	--x;		  // 处理已经是2的幂次方的边界情况
	x |= x >> 1;  // 最高位1赋给次高位
	x |= x >> 2;  // 最高两位1赋给次高两位
	x |= x >> 4;  // 最高四位1赋给次高四位
	x |= x >> 8;  // 最高八位1赋给次高八位
	x |= x >> 16; // 最高十六位1赋给次高十六位，至此，最高位下所有位都为1
	return x + 1; // 加1得到最小大于等于x的2的幂次方
}

typedef struct // 打印缓冲区结构体
{
	char *buffer; // 缓冲区字符串
	int length;	  // 缓冲区最大长度
	int offset;	  // 缓冲区字符串偏移量（已用长度）
} printbuffer;

/* 确保printbuffer结构体中的缓冲区足够大以容纳needed字节 */
static char *ensure(printbuffer *p, int needed)
{
	// 分配新的缓冲区
	char *newbuffer;
	// 新的缓冲区大小
	int newsize;

	// 检查缓冲区是否有效
	if (!p || !p->buffer)
		return 0;

	needed += p->offset; // 加上偏移量后的长度

	if (needed <= p->length)		  // 缓冲区大小足够
		return p->buffer + p->offset; // 返回偏移量后的缓冲区字符指针

	newsize = pow2gt(needed);				   // 内存对齐
	newbuffer = (char *)cJSON_malloc(newsize); // 分配新的缓冲区内存
	if (!newbuffer)							   // 分配失败
	{
		cJSON_free(p->buffer);		  // 释放旧的缓冲区内存
		p->length = 0, p->buffer = 0; // 重置缓冲区信息
		return 0;
	}
	if (newbuffer)								 // 分配成功
		memcpy(newbuffer, p->buffer, p->length); // 将旧的缓冲区内容复制到新的缓冲区
	cJSON_free(p->buffer);						 // 释放旧的缓冲区内存
	p->length = newsize;						 // 更新缓冲区大小
	p->buffer = newbuffer;						 // 更新缓冲区字符指针
	return newbuffer + p->offset;				 // 返回偏移量后的缓冲区字符指针
}

/* 更新打印缓冲区的偏移量 */
static int update(printbuffer *p)
{
	char *str;
	if (!p || !p->buffer)
		return 0;					// 缓冲区无效
	str = p->buffer + p->offset;	// 获取新增字符指针
	return p->offset + strlen(str); // 在原有偏移量上加上新增字符的长度
}

/* 把数字从所给的cJSON对象优雅地渲染成字符串。 */
static char *print_number(cJSON *item, printbuffer *p)
{
	char *str = 0;				  // 用于接取数字字符串
	double d = item->valuedouble; // 获取数字值
	if (d == 0)					  // 如果双精度值为 0，进行特殊处理。
	{
		if (p)					// 如果存在打印缓冲区
			str = ensure(p, 2); // 确保缓冲区足够大,赋给str
		else
			str = (char *)cJSON_malloc(2); /* 特殊处理0的情况 */
		if (str)
			strcpy(str, "0"); // 将字符串"0"复制给str
							  // bug:似乎缺少分配失败的处理
	}
	/*
		如果双精度值与整数值相等，并且在 INT_MIN 和 INT_MAX 范围内，
		则以整数形式输出。DBL_EPSILON是双精度浮点数的最小正误差值。
		浮点数比较注意误差不要直接用 ==
	*/
	else if (fabs(((double)item->valueint) - d) <= DBL_EPSILON && d <= INT_MAX && d >= INT_MIN)
	{
		if (p)
			str = ensure(p, 21);
		else
			str = (char *)cJSON_malloc(21); /* 一个64位无符号整数的最大值加1可以用21个字符来表示 */
		if (str)
			sprintf(str, "%d", item->valueint); // 将整数值转换为字符串，并复制给str
	}
	else // 以浮点形式输出
	{
		if (p)
			str = ensure(p, 64);
		else
			str = (char *)cJSON_malloc(64); /* 这里选择了一个合适的内存分配大小作为权衡 */
		if (str)
		{
			// 当d是非常接近整数的小数，或者d的绝对值小于1.0e60时，保留整数部分即可
			if (fabs(floor(d) - d) <= DBL_EPSILON && fabs(d) < 1.0e60) // floor是向下取整，给了64个字符，不超过60位不用科学计数法
				sprintf(str, "%.0f", d);							   // 小数点后保留零位
			// 当d的绝对值非常小（小于1.0e-6）或非常大（大于1.0e9）时，使用科学计数法格式化
			else if (fabs(d) < 1.0e-6 || fabs(d) > 1.0e9)
				sprintf(str, "%e", d);
			// 在其他情况下，使用标准的浮点数格式化
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

/* 将提供的 C 字符串转换为可以打印的转义版本。 */
static char *print_string_ptr(const char *str, printbuffer *p)
{
	const char *ptr;	   // 用来遍历的指针
	char *ptr2, *out;	   // ptr2临时指针用来赋值，out记录返回字符串
	int len = 0, flag = 0; // len用于记录字符串长度，flag用于标记是否有特殊字符
	unsigned char token;   // 遍历字符串时暂存字符

	for (ptr = str; *ptr; ptr++) // 遍历字符串str检查是否有特殊字符
		flag |= ((*ptr > 0 && *ptr < 32) || (*ptr == '\"') || (*ptr == '\\')) ? 1 : 0;
	if (!flag) // 没有特殊字符，直接返回
	{
		len = ptr - str;						 // 记录字符串长度
		if (p)									 // 有输出缓冲区
			out = ensure(p, len + 3);			 // +3是为首尾引号和一个终结字符
		else									 // 没有输出缓冲区
			out = (char *)cJSON_malloc(len + 3); // 手动分配
		if (!out)
			return 0;	   // 内存分配失败
		ptr2 = out;		   // ptr2指向新分配的空间
		*ptr2++ = '\"';	   // 首引号
		strcpy(ptr2, str); // 复制字符串
		ptr2[len] = '\"';  // 尾引号
		ptr2[len + 1] = 0; // 终结字符
		return out;
	}

	if (!str) // 处理空指针
	{
		if (p)
			out = ensure(p, 3);
		else
			out = (char *)cJSON_malloc(3);
		if (!out)
			return 0;
		strcpy(out, "\"\""); // 复制一个只包含引号的空字符串
		return out;
	}
	ptr = str; // ptr复位，计算有特殊字符时的长度
	while ((token = *ptr) && ++len)
	{
		if (strchr("\"\\\b\f\n\r\t", token)) // strchr函数用于查找一个字符在字符串中首次出现的位置。
			len++;							 // 为转义符增加额外长度
		else if (token < 32)
			len += 5; // 为不可见字符增加额外长度
		ptr++;
	}
	// 同上
	if (p)
		out = ensure(p, len + 3);
	else
		out = (char *)cJSON_malloc(len + 3);
	if (!out)
		return 0; // 内存分配失败

	ptr2 = out;		// ptr2指向新分配的空间
	ptr = str;		// ptr复位，开始复制字符串
	*ptr2++ = '\"'; // 首引号
	while (*ptr)	// 字符串内容
	{
		if ((unsigned char)*ptr > 31 && *ptr != '\"' && *ptr != '\\') // 普通字符
			*ptr2++ = *ptr++;										  // 直接复制
		else
		{
			*ptr2++ = '\\'; // 先加转义字符
			switch (token = *ptr++)
			{
				// 连入对应的转义字符
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
				sprintf(ptr2, "u%04x", token); // 特殊字符以16进制转换为unicode字符串连入输出字符串
				ptr2 += 5;
				break; /* escape and print */
			}
		}
	}
	*ptr2++ = '\"'; // 尾引号
	*ptr2++ = 0;	// 终结字符
	return out;
}
/* 对一个项调用print_string_ptr (which is useful) */
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
cJSON *cJSON_ParseWithOpts(const char *value, const char **return_parse_end, int require_null_terminated)
{
	// end用于记录解析结束时的位置。
	const char *end = 0;
	// 创建一个新的cJSON对象，用于返回。
	cJSON *c = cJSON_New_Item();
	// 初始化ep为0，ep用于记录解析过程中的错误位置。
	ep = 0;
	if (!c)
		return 0;					   /* 创建失败 */
									   // bug:创建失败后打印错误信息时，ep是空指针
	end = parse_value(c, skip(value)); // 把解析后返回的字符串指针赋值给end。
	if (!end)						   // 空指针说明解析失败
	{
		cJSON_Delete(c); // 释放创建失败的cJSON对象
		return 0;		 // 返回空指针表示解析失败
	}

	/* 如果要求JSON字符串以空字符终止且没有附加的垃圾字符，则跳过后检查空终止符 */
	if (require_null_terminated)
	{
		end = skip(end);
		if (*end) // 如果字符串没有以空字符终止，则释放创建的cJSON对象并返回空指针表示解析失败
		{
			cJSON_Delete(c);
			ep = end;
			return 0;
		}
	}
	// 如果提供了return_parse_end指针，则设置其指向解析结束的位置。
	if (return_parse_end)
		*return_parse_end = end;
	return c;
}
/* cJSON_Parse的默认选项 */
cJSON *cJSON_Parse(const char *value) { return cJSON_ParseWithOpts(value, 0, 0); }

/* 将一个cJSON数据项（实体或结构）渲染成文本形式。 */
char *cJSON_Print(cJSON *item) { return print_value(item, 0, 1, 0); } // 默认调用深度为0
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

/* 将值渲染成字符串。*/
static char *print_value(cJSON *item, int depth, int fmt, printbuffer *p)
{
	char *out = 0; // 用于存储渲染后的字符串
	if (!item)	   // 错误的cJSON对象
		return 0;
	if (p) // 缓冲区非空
	{
		switch ((item->type) & 255) // type取低八位，提高效率
		{
		case cJSON_NULL: // null类型
		{
			out = ensure(p, 5);
			if (out)
				strcpy(out, "null"); // 将字符串"null"复制到缓冲区中
			break;
		} // 下同
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
		case cJSON_Number: // 数字类型
			out = print_number(item, p);
			break;
		case cJSON_String: // 字符串类型
			out = print_string(item, p);
			break;
		case cJSON_Array: // 数组类型
			out = print_array(item, depth, fmt, p);
			break;
		case cJSON_Object: // 对象类型
			out = print_object(item, depth, fmt, p);
			break;
		}
	}
	else // 缓冲区为空
	{
		switch ((item->type) & 255)
		{
		case cJSON_NULL: // 空值类型
			out = cJSON_strdup("null");
			break;
		case cJSON_False: // false类型
			out = cJSON_strdup("false");
			break;
		case cJSON_True: // true类型
			out = cJSON_strdup("true");
			break;
		case cJSON_Number:				 // 数字类型
			out = print_number(item, 0); // 这里没有缓冲区，所以缓冲区指针参数传空指针。下同。
			break;
		case cJSON_String: // 字符串类型
			out = print_string(item, 0);
			break;
		case cJSON_Array: // 数组类型
			out = print_array(item, depth, fmt, 0);
			break;
		case cJSON_Object: // 对象类型
			out = print_object(item, depth, fmt, 0);
			break;
		}
	}
	return out; // 返回渲染后的字符串指针
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

/* 将数组选染成文本 */
static char *print_array(cJSON *item, int depth, int fmt, printbuffer *p)
{
	char **entries;						 // 用于存储每个数组元素的字符串数组
	char *out = 0, *ptr, *ret;			 // out 用于存储输出字符串,ret用于暂存解析的数组元素，ptr用于遍历赋值
	int len = 5;						 // 用于记录输出字符串长度，这里5应该是故意放了个比较大，足以容纳数组元素长度的数字，一般包括左右括号加一个结束标记3就够了
	cJSON *child = item->child;			 // 指向数组的第一个元素
	int numentries = 0, i = 0, fail = 0; // numentries记录数组条目数量，i用来暂存缓存区的偏移量，fail用来标记是否解析失败
	size_t tmplen = 0;					 // 用于记录字符串数组元素的长度

	/* 数组中有几个条目 */
	while (child) // 遍历数组
		numentries++, child = child->next;
	/* 显式处理空数组 */
	if (!numentries)
	{
		if (p)
			out = ensure(p, 3); // 3=左右中括号加一个结束标记
		else
			out = (char *)cJSON_malloc(3);
		if (out)
			strcpy(out, "[]");
		return out;
	}
	// 如果用printbuffer输出
	if (p)
	{
		/* 组成输出数组 */
		i = p->offset;
		ptr = ensure(p, 1);
		if (!ptr)
			return 0;
		*ptr = '[';			   // 左中括号赋值
		p->offset++;		   // 更新缓存区偏移量
		child = item->child;   // 指针复位
		while (child && !fail) // 无失败的情况下遍历数组
		{
			print_value(child, depth + 1, fmt, p); // 递归调用解析数组元素，这里其实已经将解析好的元素存入缓冲区了
			p->offset = update(p);				   // 更新缓存区偏移量
			if (child->next)					   // 还有下一个元素，打印逗号
			{
				len = fmt ? 2 : 1; // 跟据是否格式化为len赋值
				ptr = ensure(p, len + 1);
				if (!ptr)
					return 0;
				*ptr++ = ','; // 逗号
				if (fmt)	  // 格式化输出需要加一个空格
					*ptr++ = ' ';
				*ptr = 0;		  // 结束标记
				p->offset += len; // 更新缓存区偏移量
			}
			child = child->next; // 指向数组下一个元素
		}
		ptr = ensure(p, 2); // 再分配两个字节
		if (!ptr)
			return 0;
		*ptr++ = ']';		   // 右中括号
		*ptr = 0;			   // 结束标记
		out = (p->buffer) + i; // 返回数组起始位置
	}
	else
	{
		/* 分配一个字符串数组 */
		entries = (char **)cJSON_malloc(numentries * sizeof(char *));
		if (!entries)
			return 0;									 // 内存分配失败
		memset(entries, 0, numentries * sizeof(char *)); // 字符串数组初始化
		/* 检索所有结果并存入字符串数组 */
		child = item->child;   // 复位指向数组的第一个元素
		while (child && !fail) // 无失败的情况下遍历数组
		{
			ret = print_value(child, depth + 1, fmt, 0); // 递归调用解析数组元素
			entries[i++] = ret;							 // 为字符串数组赋值
			if (ret)									 // 解析成功，计算长度
				len += strlen(ret) + 2 + (fmt ? 1 : 0);	 // 这里我觉得+1就行了，和上面通过缓冲区分配保持一致，应该不影响结果只是多分配了空间。但是这里每一个元素都多分配了一个元素，一旦数组较大，内存浪费会很严重。
			else										 // 解析失败为fail赋值
				fail = 1;
			child = child->next;
		}

		/* 如果没有失败，尝试为输出字符串分配内存 */
		if (!fail)
			out = (char *)cJSON_malloc(len);
		/* 分配失败，fail置1 */
		if (!out)
			fail = 1;

		/* 处理失败 */
		if (fail)
		{
			/*
				这里释放字符串指针必须要先释放各数组元素（也就是每个字符串），然后再释放字符串数组。
				因为字符串数组个元素间（也就是各字符串）并不是连续的，而存储指向他们的指针是连续的。
				如果直接调用 cJSON_free(entries)，只会释放指针数组本身，而每个字符串的内存依然没有被释放。
			*/
			for (i = 0; i < numentries; i++)
				if (entries[i])
					cJSON_free(entries[i]); // 释放每个字符串
			cJSON_free(entries);			// 释放字符串数组
			return 0;
		}

		/* 构造输出数组 */
		*out = '[';	   // 左中括号
		ptr = out + 1; // 利用临时指针ptr赋值，后面out依然指向字符串首地址
		*ptr = 0;
		for (i = 0; i < numentries; i++)
		{
			tmplen = strlen(entries[i]);	 // 记录每个字符串数组元素的长度
			memcpy(ptr, entries[i], tmplen); // 将字符串数组元素复制到ptr
			ptr += tmplen;					 // 更新ptr，跳过已复制元素的长度
			if (i != numentries - 1)		 // 还有下一个元素，打印逗号
			{
				*ptr++ = ',';
				if (fmt) // 如果需要格式化，则加一个空格
					*ptr++ = ' ';
				*ptr = 0;
			}
			cJSON_free(entries[i]); // 复制完就可以释放内存
		}
		cJSON_free(entries); // 释放字符串数组
		*ptr++ = ']';		 // 右中括号
		*ptr++ = 0;			 // 结束标记
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

/* 把对象渲染成字符串 */
static char *print_object(cJSON *item, int depth, int fmt, printbuffer *p)
{
	char **entries = 0, **names = 0; // 临时数组存储键名，值
	char *out = 0, *ptr, *ret, *str;
	int len = 7, i = 0, j;		  // len临时记录所需长度，默认长度足够的较大值7
	cJSON *child = item->child;	  // 指向对象第一个成员
	int numentries = 0, fail = 0; // 记录对象成员个数，失败标记
	size_t tmplen = 0;
	/* 对象成员计数 */
	while (child)
		numentries++, child = child->next;
	/* 显式处理空对象的情况 */
	if (!numentries) // 处理空对象
	{
		if (p)												 // 利用打印缓冲区
			out = ensure(p, fmt ? depth + 4 : 3);			 // 根据是否格式化分配内存
		else												 // 手动分配内存
			out = (char *)cJSON_malloc(fmt ? depth + 4 : 3); // 非格式化左右大括号加结束标记，格式化多一个换行符加上每层多一个缩进符
		if (!out)
			return 0; // 分配失败
		ptr = out;
		*ptr++ = '{'; // 左大括号
		if (fmt)	  // 格式化
		{
			*ptr++ = '\n';					// 换行
			for (i = 0; i < depth - 1; i++) // 根据层数缩进，为了匹配左括号的位置
				*ptr++ = '\t';
		}
		*ptr++ = '}'; // 右大括号
		*ptr++ = 0;	  // 结束标记
		return out;
	}
	if (p) // 利用打印缓冲区处理非空对象
	{
		i = p->offset;			  // 记录缓冲区偏移量
		len = fmt ? 2 : 1;		  // 非格式化左大括号，格式化多一个换行符
		ptr = ensure(p, len + 1); // ptr指向缓冲区，加上结束标记空间
		if (!ptr)
			return 0; // 分配失败
		*ptr++ = '{'; // 左大括号
		if (fmt)
			*ptr++ = '\n';	 // 换行
		*ptr = 0;			 // 结束标记
		p->offset += len;	 // 更新缓冲区偏移量
		child = item->child; // 指向对象第一个成员
		depth++;			 // 层数加1
		while (child)		 // 遍历当前层的成员
		{
			if (fmt) // 格式化处理
			{
				ptr = ensure(p, depth); // 确保缩进符空间
				if (!ptr)
					return 0;				// 分配失败
				for (j = 0; j < depth; j++) // 根据层数缩进
					*ptr++ = '\t';
				p->offset += depth; // 更新缓冲区偏移量
			}
			print_string_ptr(child->string, p); // 渲染键名
			p->offset = update(p);				// 更新缓冲区偏移量

			len = fmt ? 2 : 1; // 非格式化冒号，格式化多一个缩进符
			ptr = ensure(p, len);
			if (!ptr)
				return 0;
			*ptr++ = ':'; // 冒号
			if (fmt)
				*ptr++ = '\t'; // 缩进
			p->offset += len;  // 更新缓冲区偏移量

			print_value(child, depth, fmt, p); // 渲染键值
			p->offset = update(p);			   // 更新缓冲区偏移量

			len = (fmt ? 1 : 0) + (child->next ? 1 : 0); // 有下一个成员加逗号，格式化多一个换行符
			ptr = ensure(p, len + 1);					 // 确保缓存区空间
			if (!ptr)
				return 0;
			if (child->next)
				*ptr++ = ','; // 逗号
			if (fmt)
				*ptr++ = '\n';	 // 换行
			*ptr = 0;			 // 结束标记
			p->offset += len;	 // 更新缓冲区偏移量
			child = child->next; // 指向下一个成员
		}
		ptr = ensure(p, fmt ? (depth + 1) : 2); // 非格式化右大括号加结束标记，格式化每层多一个缩进符
		if (!ptr)
			return 0;
		if (fmt)
			for (i = 0; i < depth - 1; i++)
				*ptr++ = '\t'; // 根据层数缩进
		*ptr++ = '}';		   // 右大括号
		*ptr = 0;			   // 结束标记
		out = (p->buffer) + i; // 返回起始地址
	}
	else // 手动分配内存处理非空对象
	{
		entries = (char **)cJSON_malloc(numentries * sizeof(char *)); // 为键名字符串数组分配空间
		if (!entries)
			return 0;
		names = (char **)cJSON_malloc(numentries * sizeof(char *)); // 为键值字符串数组分配空间
		if (!names)
		{
			cJSON_free(entries); // 键值字符串数组分配失败，释放键名字符串数组
			return 0;
		}

		// 初始化数组
		memset(entries, 0, sizeof(char *) * numentries);
		memset(names, 0, sizeof(char *) * numentries);

		/* 将所有键值对存入数组 */
		child = item->child; // 指向对象第一个成员
		depth++;			 // 层数加1
		if (fmt)
			len += depth; // 格式化每层多一个缩进符
		while (child)	  // 遍历赋值键值对
		{
			names[i] = str = print_string_ptr(child->string, 0);
			entries[i++] = ret = print_value(child, depth, fmt, 0); // 当键名和键值都已经存储完毕后，i++ 才执行
			if (str && ret)
				len += strlen(ret) + strlen(str) + 2 + (fmt ? 2 + depth : 0); // 键名和键值长度+冒号与逗号+行前缩进，冒号前缩进与换行符
			else
				fail = 1; // fail标记赋值失败
			child = child->next;
		}

		/* 为输出字符串分配空间 */
		if (!fail)
			out = (char *)cJSON_malloc(len);
		if (!out)
			fail = 1;

		/* 处理失败 */
		if (fail)
		{
			for (i = 0; i < numentries; i++) // 释放字符串
			{
				if (names[i])
					cJSON_free(names[i]);
				if (entries[i])
					cJSON_free(entries[i]);
			}
			// 释放字符串数组
			cJSON_free(names);
			cJSON_free(entries);
			return 0;
		}

		/* 构造输出字符串 */
		*out = '{'; // 左大括号
		ptr = out + 1;
		if (fmt)
			*ptr++ = '\n';				 // 换行
		*ptr = 0;						 // 结束标记
		for (i = 0; i < numentries; i++) // 键值对
		{
			if (fmt) // 对齐缩进
				for (j = 0; j < depth; j++)
					*ptr++ = '\t';
			tmplen = strlen(names[i]);	   // 键名长度
			memcpy(ptr, names[i], tmplen); // 拷贝键名
			ptr += tmplen;				   // 指针后移
			*ptr++ = ':';				   // 冒号
			if (fmt)					   // 格式化缩进
				*ptr++ = '\t';
			strcpy(ptr, entries[i]);   // 拷贝键值
			ptr += strlen(entries[i]); // 指针后移
			if (i != numentries - 1)   // 不是最后一个键值对，添加逗号
				*ptr++ = ',';
			if (fmt) // 格式化换行
				*ptr++ = '\n';
			*ptr = 0; // 结束标记
			// 释放键名与键值字符串
			cJSON_free(names[i]);
			cJSON_free(entries[i]);
		}

		// 释放键名与键值字符串数组
		cJSON_free(names);
		cJSON_free(entries);

		if (fmt) // 对齐缩进
			for (i = 0; i < depth - 1; i++)
				*ptr++ = '\t';
		*ptr++ = '}'; // 右大括号
		*ptr++ = 0;	  // 结束标记
	}
	return out;
}

/* 获取数组大小 */
int cJSON_GetArraySize(cJSON *array)
{
	cJSON *c = array->child;
	int i = 0;
	while (c)
		i++, c = c->next;
	return i;
}
/* 获取数组第item个成员 */
cJSON *cJSON_GetArrayItem(cJSON *array, int item)
{
	cJSON *c = array->child; // 指向第一个成员
	while (c && item > 0)	 // 遍历
		item--, c = c->next;
	return c; // 返回第item个成员的指针
}
cJSON *cJSON_GetObjectItem(cJSON *object, const char *string)
{
	cJSON *c = object->child;
	while (c && cJSON_strcasecmp(c->string, string))
		c = c->next;
	return c;
}

/* 链表尾插工具 */
static void suffix_object(cJSON *prev, cJSON *item)
{
	prev->next = item; // 将item链接到prev之后
	item->prev = prev; // 将prev链接到item之前
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

/* 添加项到数组/对象 */
void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
	cJSON *c = array->child; // 指向第一个成员
	if (!item)				 // item为空则直接返回结束执行
		return;
	if (!c) // 这是第一个成员
	{
		array->child = item; // 连入item
	}
	else // 不是第一个成员
	{
		while (c && c->next) // 遍历到最后一个成员
			c = c->next;
		suffix_object(c, item); // 将item链接到c之后
	}
}
/* 通过改造已有cJSON类型的方式向对象内添加新项 */
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
	if (!item)
		return;		  // item为空则直接返回结束执行
	if (item->string) // item的键名不为空，则释放item的string
		cJSON_free(item->string);
	item->string = cJSON_strdup(string); // 修改键名
	cJSON_AddItemToArray(object, item);	 // 把item添加到object
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

/* 用新项替换数组/对象里的旧项 */
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
void cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem) // 替换数组元素
{
	cJSON *c = array->child; // 指向首元素
	while (c && which > 0)	 // 遍历到指定位置，对应数组下标[which]的元素
		c = c->next, which--;
	if (!c) // which越界，则直接返回结束执行
		return;
	newitem->next = c->next;		   // 连接后继
	newitem->prev = c->prev;		   // 连接前驱
	if (newitem->next)				   // 不是最后一个元素
		newitem->next->prev = newitem; // 后继元素的前驱指向新元素
	if (c == array->child)			   // 是第一个元素
		array->child = newitem;		   // array->child指向新首元素
	else							   // 不是第一个元素
		newitem->prev->next = newitem; // 前驱元素的后继指向新元素
	c->next = c->prev = 0;			   // 断开旧元素
	cJSON_Delete(c);				   // 释放旧元素
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

/* 创建基础类型 */
cJSON *cJSON_CreateNull(void) // cJSON创建null
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
		item->type = cJSON_NULL;	// 设置为cJSON_NULL类型
	return item;
}
cJSON *cJSON_CreateTrue(void) // cJSON创建true
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
		item->type = cJSON_True;	// 设置为cJSON_True类型
	return item;
}
cJSON *cJSON_CreateFalse(void) // cJSON创建false
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
		item->type = cJSON_False;	// 设置为cJSON_False类型
	return item;
}
cJSON *cJSON_CreateBool(int b) // cJSON创建bool
{
	cJSON *item = cJSON_New_Item();				   // 创建一个新项
	if (item)									   // 如果创建成功
		item->type = b ? cJSON_True : cJSON_False; // 根据bool值设置类型
	return item;
}
cJSON *cJSON_CreateNumber(double num) // cJSON创建数字
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
	{
		item->type = cJSON_Number; // 设置类型为数字
		item->valuedouble = num;   // 数字精确赋值
		item->valueint = (int)num; // 数字整数赋值
	}
	return item;
}
cJSON *cJSON_CreateString(const char *string) // cJSON创建字符串
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
	{
		item->type = cJSON_String;				  // 设置类型为字符串
		item->valuestring = cJSON_strdup(string); // 字符串项赋值
	}
	return item;
}
cJSON *cJSON_CreateArray(void) // cJSON创建数组
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
		item->type = cJSON_Array;	// 设置类型为数组
	return item;
}
cJSON *cJSON_CreateObject(void) // 构建cJSON对象
{
	cJSON *item = cJSON_New_Item(); // 创建一个新项
	if (item)						// 如果创建成功
		item->type = cJSON_Object;	// 设置类型为对象
	return item;					// 返回指针
}

/* 创建数组: */
cJSON *cJSON_CreateIntArray(const int *numbers, int count) // 构建cJSON整型数组
{
	int i;											// 遍历索引
	cJSON *n = 0, *p = 0, *a = cJSON_CreateArray(); // 创建数组根节点a，遍历指针p，临时存放新建节点指针n
	for (i = 0; a && i < count; i++)
	{
		n = cJSON_CreateNumber(numbers[i]); // 创建数字节点
		if (!i)								// 第一个元素
			a->child = n;					// 插入到数组
		else								// 非第一个元素
			suffix_object(p, n);			// 插入元素
		p = n;								// 更新遍历指针
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
cJSON *cJSON_CreateStringArray(const char **strings, int count) // 构建cJSON字符串数组
{
	int i;											// 遍历索引
	cJSON *n = 0, *p = 0, *a = cJSON_CreateArray(); // 创建数组根节点a，遍历指针p，临时存放新建节点指针n
	for (i = 0; a && i < count; i++)
	{
		n = cJSON_CreateString(strings[i]); // 创建字符串节点
		if (!i)								// 是第一个元素
			a->child = n;					// 连入第一个元素
		else								// 不是第一个元素
			suffix_object(p, n);			// 连接元素
		p = n;								// 遍历指针指向下一个元素
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
