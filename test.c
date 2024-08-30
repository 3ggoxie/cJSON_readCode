#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

/* 将文本解析为JSON格式，然后再将其渲染回文本形式并打印出来! */
void doit(char *text)
{
	char *out;	 // 存放输出文本
	cJSON *json; // 定义cJSON对象json，用于解析JSON字符串

	json = cJSON_Parse(text); // 解析JSON字符串
	if (!json)
	{
		// 解析失败，打印错误信息
		printf("Error before: [%s]\n", cJSON_GetErrorPtr());
	}
	else // 解析成功，将JSON对象渲染为文本并存储在out变量中
	{
		out = cJSON_Print(json); // 将JSON对象渲染为文本
		cJSON_Delete(json);		 // 释放JSON对象
		printf("%s\n", out);	 // 打印解析后的JSON文本
		free(out);				 // 释放输出文本对象
	}
}

/* 读取文件，解析内容，然后重新渲染等操作。*/
void dofile(char *filename)
{
	FILE *f;	// 文件指针
	long len;	// 文件长度
	char *data; // 用于存储文本

	f = fopen(filename, "rb");		// 以都二进制的方式打开文件
	fseek(f, 0, SEEK_END);			// 文件指示器指向离文件末尾偏移0的位置（也就是末尾）
	len = ftell(f);					// 获取文件指示器的当前位置，即文件长度
	fseek(f, 0, SEEK_SET);			// 将文件指示器指向文件开头
	data = (char *)malloc(len + 1); // 分配内存，注意加上一个字节，用于存储字符串结尾的'\0'字符
	fread(data, 1, len, f);			// 将文件内容读入到data中
	fclose(f);						// 关闭文件流并释放相关资源
	doit(data);						// 解析重构并打印data
	free(data);						// 释放data内存
}

/* 被下方的一些代码用作示例数据类型。*/
struct record
{
	const char *precision;								// 表示精度的字符串，明确地址的范围，如下面的“zip”代表以邮政编码为范围的区域
	double lat, lon;									// 经纬度
	const char *address, *city, *state, *zip, *country; // 地址，城市，州，邮编，国家
};

/* 创建一些对象用于演示 */
void create_objects()
{
	cJSON *root, *fmt, *img, *thm, *fld;
	char *out;
	int i; /* declare a few. */
	/* 一周七天的字符串数组: */
	const char *strings[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
	/* 一个3x3矩阵: */
	int numbers[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
	/* 图片展示组件: */
	int ids[4] = {116, 943, 234, 38793};
	/* records数组: */
	struct record fields[2] = {
		{"zip", 37.7668, -1.223959e+2, "", "SAN FRANCISCO", "CA", "94107", "US"},
		{"zip", 37.371991, -1.22026e+2, "", "SUNNYVALE", "CA", "94085", "US"}};

	/* 在这里我们构建一些来自 JSON 网站的标准 JSON 示例。 */

	/* 视频数据类型: */
	root = cJSON_CreateObject();													  // 创建新对象的根节点
	cJSON_AddItemToObject(root, "name", cJSON_CreateString("Jack (\"Bee\") Nimble")); // 改造一个cJSON字符串的方式添加第一个成员
	cJSON_AddItemToObject(root, "format", fmt = cJSON_CreateObject());				  // 添加值为对象类型的新项
	cJSON_AddStringToObject(fmt, "type", "rect");									  // 添加一个字符串类型的新项
	cJSON_AddNumberToObject(fmt, "width", 1920);									  // 添加一个数字类型的新项
	cJSON_AddNumberToObject(fmt, "height", 1080);									  // 添加一个数字类型的新项
	cJSON_AddFalseToObject(fmt, "interlace");										  // 添加一个布尔类型的新项
	cJSON_AddNumberToObject(fmt, "frame rate", 24);									  // 添加一个数字类型的新项

	out = cJSON_Print(root); // 接取渲染后的文本
	cJSON_Delete(root);		 // 删除对象
	printf("%s\n", out);	 // 打印文本
	free(out);				 /* 渲染成文本, 删除cJSON对象, 打印它, 释放字符串内存 */

	/* 构建一周七天的字符串数组 */
	root = cJSON_CreateStringArray(strings, 7); // 根据字符串数组创建一个cJSON字符串数组
	/* cJSON_ReplaceItemInArray(root, 1, cJSON_CreateString("Replacement")); // 将数组root的第2个元素替换为字符串"Replacement" */

	out = cJSON_Print(root); // 接取渲染后的文本
	cJSON_Delete(root);		 // 删除数组
	printf("%s\n", out);	 // 打印文本
	free(out);				 // 释放文本内存

	/* 构建一个3x3矩阵 */
	root = cJSON_CreateArray();											 // 构建矩阵根节点
	for (i = 0; i < 3; i++)												 // 遍历构建行数组
		cJSON_AddItemToArray(root, cJSON_CreateIntArray(numbers[i], 3)); // 通过cJSON_AddItemToArray将行数组拼成矩阵

	out = cJSON_Print(root); // 接取渲染后的文本
	cJSON_Delete(root);		 // 删除矩阵
	printf("%s\n", out);	 // 打印文本
	free(out);				 // 释放文本内存

	/* 构建图片展示组件 */
	root = cJSON_CreateObject();												   // 创建图片展示组件根节点
	cJSON_AddItemToObject(root, "Image", img = cJSON_CreateObject());			   // 添加值为对象类型的新项Image
	cJSON_AddNumberToObject(img, "Width", 800);									   // 添加对象Image的第一个值为数字类型的新项Width
	cJSON_AddNumberToObject(img, "Height", 600);								   // 数字类型的新项Height
	cJSON_AddStringToObject(img, "Title", "View from 15th Floor");				   // 字符串类型的新项Title
	cJSON_AddItemToObject(img, "Thumbnail", thm = cJSON_CreateObject());		   // 添加值值为对象类型的新项Thumbnail
	cJSON_AddStringToObject(thm, "Url", "http:/*www.example.com/image/481989943"); // 添加对象Thumbnail的第一个值为字符串类型的新项Url
	cJSON_AddNumberToObject(thm, "Height", 125);								   // 数字类型的新项Height
	cJSON_AddStringToObject(thm, "Width", "100");								   // 字符串类型的新项Width
	cJSON_AddItemToObject(img, "IDs", cJSON_CreateIntArray(ids, 4));			   // 数组类型的新项IDs

	out = cJSON_Print(root); // 接收渲染后的文本
	cJSON_Delete(root);		 // 删除图片展示组件
	printf("%s\n", out);	 // 打印文本
	free(out);				 // 释放文本内存

	/* 实例化records数组: */

	root = cJSON_CreateArray(); // 创建数组根节点
	for (i = 0; i < 2; i++)		// 遍历数组
	{
		cJSON_AddItemToArray(root, fld = cJSON_CreateObject());			// 添加数组元素对象fld
		cJSON_AddStringToObject(fld, "precision", fields[i].precision); // 添加对象fld的precision成员
		cJSON_AddNumberToObject(fld, "Latitude", fields[i].lat);		// 添加对象fld的lat成员
		cJSON_AddNumberToObject(fld, "Longitude", fields[i].lon);		// 添加对象fld的lon成员
		cJSON_AddStringToObject(fld, "Address", fields[i].address);		// 添加对象fld的address成员
		cJSON_AddStringToObject(fld, "City", fields[i].city);			// 添加对象fld的city成员
		cJSON_AddStringToObject(fld, "State", fields[i].state);			// 添加对象fld的state成员
		cJSON_AddStringToObject(fld, "Zip", fields[i].zip);				//	添加对象fld的zip成员
		cJSON_AddStringToObject(fld, "Country", fields[i].country);		// 添加对象fld的country成员
	}

	cJSON_ReplaceItemInObject(cJSON_GetArrayItem(root, 1), "City", cJSON_CreateIntArray(ids, 4)); // 把第2个数组对象中的city成员的键值替换为数组ids/*  */

	out = cJSON_Print(root); // 接收渲染后的文本
	cJSON_Delete(root);		 // 删除records数组
	printf("%s\n", out);	 // 打印文本
	free(out);				 // 释放文本内存
}

int main(int argc, const char *argv[])
{
	/* 一组JSON文本 */
	char text1[] = "{\n\"name\": \"Jack (\\\"Bee\\\") Nimble\", \n\"format\": {\"type\":       \"rect\", \n\"width\":      1920, \n\"height\":     1080, \n\"interlace\":  false,\"frame rate\": 24\n}\n}";
	char text2[] = "[\"Sunday\", \"Monday\", \"Tuesday\", \"Wednesday\", \"Thursday\", \"Friday\", \"Saturday\"]";
	char text3[] = "[\n    [0, -1, 0],\n    [1, 0, 0],\n    [0, 0, 1]\n	]\n";
	char text4[] = "{\n		\"Image\": {\n			\"Width\":  800,\n			\"Height\": 600,\n			\"Title\":  \"View from 15th Floor\",\n			\"Thumbnail\": {\n				\"Url\":    \"http:/*www.example.com/image/481989943\",\n				\"Height\": 125,\n				\"Width\":  \"100\"\n			},\n			\"IDs\": [116, 943, 234, 38793]\n		}\n	}";
	char text5[] = "[\n	 {\n	 \"precision\": \"zip\",\n	 \"Latitude\":  37.7668,\n	 \"Longitude\": -122.3959,\n	 \"Address\":   \"\",\n	 \"City\":      \"SAN FRANCISCO\",\n	 \"State\":     \"CA\",\n	 \"Zip\":       \"94107\",\n	 \"Country\":   \"US\"\n	 },\n	 {\n	 \"precision\": \"zip\",\n	 \"Latitude\":  37.371991,\n	 \"Longitude\": -122.026020,\n	 \"Address\":   \"\",\n	 \"City\":      \"SUNNYVALE\",\n	 \"State\":     \"CA\",\n	 \"Zip\":       \"94085\",\n	 \"Country\":   \"US\"\n	 }\n	 ]";

	/* 对于每个 JSON 文本块，先进行解析，然后再将其重新构建: */
	doit(text1); // 解析重构并打印text1
	/* doit(text2);
	doit(text3);
	doit(text4);
	doit(text5); */

	/* 解析标准测试文件：*/
	dofile("../../tests/test1"); // 解析重构文件并打印
	/* dofile("../../tests/test2");
	dofile("../../tests/test3");
	dofile("../../tests/test4");
	dofile("../../tests/test5"); */

	/* 一些用于简洁地构建对象的示例代码：*/
	create_objects();

	return 0;
}
