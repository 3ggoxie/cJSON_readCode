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
		out = cJSON_Print(json); // 将JSON对象渲染为文本// mark:1
		cJSON_Delete(json);		 // 释放JSON对象
		printf("%s\n", out);
		free(out); // 释放输出文本对象
	}
}

/* 读取文件，解析内容，然后重新渲染等操作。*/
void dofile(char *filename)
{
	FILE *f;
	long len;
	char *data;

	f = fopen(filename, "rb");
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = (char *)malloc(len + 1);
	fread(data, 1, len, f);
	fclose(f);
	doit(data);
	free(data);
}

/* 被下方的一些代码用作示例数据类型。*/
struct record
{
	const char *precision;
	double lat, lon;
	const char *address, *city, *state, *zip, *country;
};

/* Create a bunch of objects as demonstration. */
void create_objects()
{
	cJSON *root, *fmt, *img, *thm, *fld;
	char *out;
	int i; /* declare a few. */
	/* Our "days of the week" array: */
	const char *strings[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
	/* Our matrix: */
	int numbers[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
	/* Our "gallery" item: */
	int ids[4] = {116, 943, 234, 38793};
	/* Our array of "records": */
	struct record fields[2] = {
		{"zip", 37.7668, -1.223959e+2, "", "SAN FRANCISCO", "CA", "94107", "US"},
		{"zip", 37.371991, -1.22026e+2, "", "SUNNYVALE", "CA", "94085", "US"}};

	/* Here we construct some JSON standards, from the JSON site. */

	/* Our "Video" datatype: */
	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "name", cJSON_CreateString("Jack (\"Bee\") Nimble"));
	cJSON_AddItemToObject(root, "format", fmt = cJSON_CreateObject());
	cJSON_AddStringToObject(fmt, "type", "rect");
	cJSON_AddNumberToObject(fmt, "width", 1920);
	cJSON_AddNumberToObject(fmt, "height", 1080);
	cJSON_AddFalseToObject(fmt, "interlace");
	cJSON_AddNumberToObject(fmt, "frame rate", 24);

	out = cJSON_Print(root);
	cJSON_Delete(root);
	printf("%s\n", out);
	free(out); /* Print to text, Delete the cJSON, print it, release the string. */

	/* Our "days of the week" array: */
	root = cJSON_CreateStringArray(strings, 7);

	out = cJSON_Print(root);
	cJSON_Delete(root);
	printf("%s\n", out);
	free(out);

	/* Our matrix: */
	root = cJSON_CreateArray();
	for (i = 0; i < 3; i++)
		cJSON_AddItemToArray(root, cJSON_CreateIntArray(numbers[i], 3));

	/*	cJSON_ReplaceItemInArray(root,1,cJSON_CreateString("Replacement")); */

	out = cJSON_Print(root);
	cJSON_Delete(root);
	printf("%s\n", out);
	free(out);

	/* Our "gallery" item: */
	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "Image", img = cJSON_CreateObject());
	cJSON_AddNumberToObject(img, "Width", 800);
	cJSON_AddNumberToObject(img, "Height", 600);
	cJSON_AddStringToObject(img, "Title", "View from 15th Floor");
	cJSON_AddItemToObject(img, "Thumbnail", thm = cJSON_CreateObject());
	cJSON_AddStringToObject(thm, "Url", "http:/*www.example.com/image/481989943");
	cJSON_AddNumberToObject(thm, "Height", 125);
	cJSON_AddStringToObject(thm, "Width", "100");
	cJSON_AddItemToObject(img, "IDs", cJSON_CreateIntArray(ids, 4));

	out = cJSON_Print(root);
	cJSON_Delete(root);
	printf("%s\n", out);
	free(out);

	/* Our array of "records": */

	root = cJSON_CreateArray();
	for (i = 0; i < 2; i++)
	{
		cJSON_AddItemToArray(root, fld = cJSON_CreateObject());
		cJSON_AddStringToObject(fld, "precision", fields[i].precision);
		cJSON_AddNumberToObject(fld, "Latitude", fields[i].lat);
		cJSON_AddNumberToObject(fld, "Longitude", fields[i].lon);
		cJSON_AddStringToObject(fld, "Address", fields[i].address);
		cJSON_AddStringToObject(fld, "City", fields[i].city);
		cJSON_AddStringToObject(fld, "State", fields[i].state);
		cJSON_AddStringToObject(fld, "Zip", fields[i].zip);
		cJSON_AddStringToObject(fld, "Country", fields[i].country);
	}

	/*	cJSON_ReplaceItemInObject(cJSON_GetArrayItem(root,1),"City",cJSON_CreateIntArray(ids,4)); */

	out = cJSON_Print(root);
	cJSON_Delete(root);
	printf("%s\n", out);
	free(out);
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
	doit(text1); // mark:0
	doit(text2);
	doit(text3);
	doit(text4);
	doit(text5);

	/* 解析标准测试文件：*/
	/*	dofile("../../tests/test1"); */
	/*	dofile("../../tests/test2"); */
	/*	dofile("../../tests/test3"); */
	/*	dofile("../../tests/test4"); */
	/*	dofile("../../tests/test5"); */

	/* 一些用于简洁地构建对象的示例代码：*/
	create_objects();

	return 0;
}
