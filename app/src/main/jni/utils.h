
#ifndef __UTILS_H__
#define __UTILS_H__

int yuv422Toyuv420sp(unsigned char *yuv420, unsigned char *yuv422, int width, int height);
void yuyv422toABGRY(int *rgb, unsigned char *src, int width, int height);


void yuv420pto422(int * out, unsigned char *pic, int width);
void yuv422pto422(int * out, unsigned char *pic, int width);
void yuv400pto422(int * out, unsigned char *pic, int width);
void yuv444pto422(int * out, unsigned char *pic, int width);

#endif