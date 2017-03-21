#include <stddef.h>
#include "utils.h"
#include "DebugLog.h"

#define CLIP(color) (unsigned char)(((color) > 0xFF) ? 0xff : (((color) < 0) ? 0 : (color)))

int yuv422Toyuv420sp(unsigned char *yuv420, unsigned char *yuv422, int width, int height) {

    if(yuv420 == NULL || yuv422 == NULL){
        return -1;
    }

    int ynum = width * height;
    int i, j, k = 0;

    for(i=0; i<ynum; i++){
        yuv420[i] = yuv422[i * 2];
    }

    for(i=0; i<height; i++){
        if((i % 2) != 0){
            continue;
        }
        for(j=0; j<(width / 2); j++){
            if((4 * j + 1) > (2 * width)) {
                break;
            }
            yuv420[ynum + k * 2 * width / 4 + j] = yuv422[i * 2 * width + 4 * j + 1];
        }
        k++;
    }

    k = 0;

    for(i=0; i<height; i++){
        if((i%2) == 0) {
            continue;
        }
        for(j=0; j<(width / 2); j++){
            if((4 * j + 3) > (2 * width)) {
                break;
            }
            yuv420[ynum + ynum / 4 + k * 2 * width / 4 + j] = yuv422[i * 2 * width + 4 * j + 3];

        }
        k++;
    }
    return 1;
}


void yuyv422toABGRY(int *rgb, unsigned char *src, int width, int height) {

    static int yuv_tbl_ready = 0;
    static int y1192_tbl[256];
    static int v1634_tbl[256];
    static int v833_tbl[256];
    static int u400_tbl[256];
    static int u2066_tbl[256];
    int frameSize = width * height * 2;
    int i;

    if(rgb == NULL || src == NULL){
        LOGE("the param is NULL \n");
        return;
    }

    if(yuv_tbl_ready == 0){
        for(i=0; i<256; i++){
            y1192_tbl[i] = 1192*(i-16);
            if(y1192_tbl[i] < 0){
                y1192_tbl[i] = 0;
            }

            v1634_tbl[i] = 1634*(i-128);
            v833_tbl[i] = 833*(i-128);
            u400_tbl[i] = 400*(i-128);
            u2066_tbl[i] = 2066*(i-128);
        }
        yuv_tbl_ready = 1;
    }

    for(i=0; i<frameSize; i+=4){
        unsigned char y1, y2, u, v;
        y1 = src[i];
        u = src[i+1];
        y2 = src[i+2];
        v = src[i+3];

        int y1192_1 = y1192_tbl[y1];
        int r1 = (y1192_1 + v1634_tbl[v])>>10;
        int g1 = (y1192_1 - v833_tbl[v] - u400_tbl[u])>>10;
        int b1 = (y1192_1 + u2066_tbl[u])>>10;

        int y1192_2=y1192_tbl[y2];
        int r2 = (y1192_2 + v1634_tbl[v])>>10;
        int g2 = (y1192_2 - v833_tbl[v] - u400_tbl[u])>>10;
        int b2 = (y1192_2 + u2066_tbl[u])>>10;

        r1 = r1 > 255 ? 255 : r1 < 0 ? 0 : r1;
        g1 = g1 > 255 ? 255 : g1 < 0 ? 0 : g1;
        b1 = b1 > 255 ? 255 : b1 < 0 ? 0 : b1;
        r2 = r2 > 255 ? 255 : r2 < 0 ? 0 : r2;
        g2 = g2 > 255 ? 255 : g2 < 0 ? 0 : g2;
        b2 = b2 > 255 ? 255 : b2 < 0 ? 0 : b2;

        *rgb++ = 0xff000000 | b1<<16 | g1<<8 | r1;
        *rgb++ = 0xff000000 | b2<<16 | g2<<8 | r2;
    }
}

void yuv420pto422(int * out, unsigned char *pic, int width) {

    int j, k;
    unsigned char *pic0, *pic1;
    int *outy, *outu, *outv;
    int outy1 = 0;
    int outy2 = 8;

    pic0 = pic;
    pic1 = pic + width;
    outy = out;
    outu = out + 64 * 4;
    outv = out + 64 * 5;

    for (j = 0; j < 8; j++) {
        for (k = 0; k < 8; k++) {
            if (k == 4) {
                outy1 += 56;
                outy2 += 56;
            }
            *pic0++ = CLIP(outy[outy1]);
            *pic0++ = CLIP(128 + *outu);
            *pic0++ = CLIP(outy[outy1 + 1]);
            *pic0++ = CLIP(128 + *outv);
            *pic1++ = CLIP(outy[outy2]);
            *pic1++ = CLIP(128 + *outu);
            *pic1++ = CLIP(outy[outy2 + 1]);
            *pic1++ = CLIP(128 + *outv);
            outy1 += 2;
            outy2 += 2;
            outu++;
            outv++;
        }

        if (j == 3) {
            outy = out + 128;
        } else {
            outy += 16;
        }
        outy1 = 0;
        outy2 = 8;
        pic0 += 2 * (width - 16);
        pic1 += 2 * (width - 16);
    }
}

void yuv422pto422(int * out, unsigned char *pic, int width) {

    int j, k;
    unsigned char *pic0, *pic1;
    int *outy, *outu, *outv;
    int outy1 = 0;
    int outy2 = 8;
    int outu1 = 0;
    int outv1 = 0;

    pic0 = pic;
    pic1 = pic + width;
    outy = out;
    outu = out + 64 * 4;
    outv = out + 64 * 5;

    for (j = 0; j < 4; j++) {
        for (k = 0; k < 8; k++) {
            if (k == 4) {
                outy1 += 56;
                outy2 += 56;
            }
            *pic0++ = CLIP(outy[outy1]);
            *pic0++ = CLIP(128 + outu[outu1]);
            *pic0++ = CLIP(outy[outy1 + 1]);
            *pic0++ = CLIP(128 + outv[outv1]);
            *pic1++ = CLIP(outy[outy2]);
            *pic1++ = CLIP(128 + outu[outu1 + 8]);
            *pic1++ = CLIP(outy[outy2 + 1]);
            *pic1++ = CLIP(128 + outv[outv1 + 8]);
            outv1 += 1;
            outu1 += 1;
            outy1 += 2;
            outy2 += 2;
        }

        outy += 16;
        outu += 8;
        outv += 8;
        outv1 = 0;
        outu1 = 0;
        outy1 = 0;
        outy2 = 8;
        pic0 += 2 * (width - 16);
        pic1 += 2 * (width - 16);
    }
}

void yuv400pto422(int * out, unsigned char *pic, int width) {

    int j, k;
    unsigned char *pic0, *pic1;
    int *outy;
    int outy1 = 0;
    int outy2 = 8;
    pic0 = pic;
    pic1 = pic + width;
    outy = out;

    for (j = 0; j < 4; j++) {
        for (k = 0; k < 4; k++) {
            *pic0++ = CLIP(outy[outy1]);
            *pic0++ = 128;
            *pic0++ = CLIP(outy[outy1 + 1]);
            *pic0++ = 128;
            *pic1++ = CLIP(outy[outy2]);
            *pic1++ = 128;
            *pic1++ = CLIP(outy[outy2 + 1]);
            *pic1++ = 128;
            outy1 += 2;
            outy2 += 2;
        }
        outy += 16;
        outy1 = 0;
        outy2 = 8;
        pic0 += 2 * (width - 8);
        pic1 += 2 * (width - 8);
    }
}

void yuv444pto422(int * out, unsigned char *pic, int width) {

    int j, k;
    unsigned char *pic0, *pic1;
    int *outy, *outu, *outv;
    int outy1 = 0;
    int outy2 = 8;
    int outu1 = 0;
    int outv1 = 0;

    pic0 = pic;
    pic1 = pic + width;
    outy = out;
    outu = out + 64 * 4; // Ooops where did i invert ??
    outv = out + 64 * 5;
    for (j = 0; j < 4; j++) {
        for (k = 0; k < 4; k++) {
            *pic0++ = CLIP(outy[outy1]);
            *pic0++ = CLIP(128 + outu[outu1]);
            *pic0++ = CLIP(outy[outy1 + 1]);
            *pic0++ = CLIP(128 + outv[outv1]);
            *pic1++ = CLIP(outy[outy2]);
            *pic1++ = CLIP(128 + outu[outu1 + 8]);
            *pic1++ = CLIP(outy[outy2 + 1]);
            *pic1++ = CLIP(128 + outv[outv1 + 8]);
            outv1 += 2;
            outu1 += 2;
            outy1 += 2;
            outy2 += 2;
        }
        outy += 16;
        outu += 16;
        outv += 16;
        outv1 = 0;
        outu1 = 0;
        outy1 = 0;
        outy2 = 8;
        pic0 += 2 * (width - 8);
        pic1 += 2 * (width - 8);
    }
}