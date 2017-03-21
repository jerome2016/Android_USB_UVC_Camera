#include <stddef.h>
#include <malloc.h>
#include <memory.h>
#include "jpegDecode.h"
#include "huffman.h"
#include "utils.h"
#include "DebugLog.h"

static void idctqtab
        __P((unsigned char *, PREC *));

static void setinput
        __P((struct in *, unsigned char *));

static void decode_mcus
        __P((struct in *, int *, int, struct scan *, int *));

typedef void (*ftopict)(int *out, unsigned char *pic, int width);
static unsigned char *datap = NULL;
static unsigned char quant[4][64];
static struct decHufftbl dhuff[4];
static struct jpgInfo info;
static struct comp comps[MAXCOMP];
static struct scan dscans[MAXCOMP];
static struct in in;

#define dec_huffdc (dhuff + 0)
#define dec_huffac (dhuff + 2)

static unsigned char zig2[64] = {
    0, 2, 3, 9, 10, 20, 21, 35, 14, 16, 25, 31, 39, 46, 50, 57, 5, 7, 12, 18, 23,
    33, 37, 48, 27, 29, 41, 44, 52, 55, 59, 62, 15, 26, 30, 40, 45, 51, 56,
    58, 1, 4, 8, 11, 19, 22, 34, 36, 28, 42, 43, 53, 54, 60, 61, 63, 6, 13,
    17, 24, 32, 38, 47, 49
};

static unsigned char zig[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42, 3, 8, 12, 17, 25, 30,
    41, 43, 9, 11, 18, 24, 31, 40, 44, 53, 10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60, 21, 34, 37, 47, 50, 56, 59, 61, 35, 36,
    48, 49, 57, 58, 62, 63
};

static PREC aaidct[8] = {
    IFIX(0.3535533906),
    IFIX(0.4903926402),
    IFIX(0.4619397663),
    IFIX(0.4157348062),
    IFIX(0.3535533906),
    IFIX(0.2777851165),
    IFIX(0.1913417162),
    IFIX(0.0975451610)
};

static int getbyte(void) {
    return *datap++;
}

static int getword(void) {
    int c1, c2;
    c1 = *datap++;
    c2 = *datap++;
    return c1 << 8 | c2;
}

static int fillbits(struct in *in, int le, unsigned int bi) {
    int b, m;

    if (in->marker) {
        if (le <= 16) {
            in->bits = bi << 16, le += 16;
        }
        return le;
    }

    while (le <= 24) {
        b = *in->p++;
        if (b == 0xff && (m = *in->p++) != 0) {
            if (m == M_EOF) {
                if (in->func && (m = in->func(in->data)) == 0) {
                    continue;
                }
            }
            in->marker = m;
            if (le <= 16) {
                bi = bi << 16, le += 16;
            }
            break;
        }
        bi = bi << 8 | b;
        le += 8;
    }
    in->bits = bi; /* tmp... 2 return values needed */
    return le;
}

static int decReadmarker(struct in *in) {
    int m;

    in->left = fillbits(in, in->left, in->bits);
    if ((m = in->marker) == 0) {
        return 0;
    }
    in->left = 0;
    in->marker = 0;
    return m;
}

static void decMakehuff(
        struct decHufftbl *hu,
        int *hufflen,
        unsigned char *huffvals)
{
    int code, k, i, j, d, x, c, v;

    for (i = 0; i < (1 << DECBITS); i++) {
        hu->llvals[i] = 0;
    }

    /*
     * llvals layout:
     *
     * value v already known, run r, backup u bits:
     *  vvvvvvvvvvvvvvvv 0000 rrrr 1 uuuuuuu
     * value unknown, size b bits, run r, backup u bits:
     *  000000000000bbbb 0000 rrrr 0 uuuuuuu
     * value and size unknown:
     *  0000000000000000 0000 0000 0 0000000
     */
    code = 0;
    k = 0;
    for (i = 0; i < 16; i++, code <<= 1) { /* sizes */

        hu->valptr[i] = k;
        for (j = 0; j < hufflen[i]; j++) {
            hu->vals[k] = *huffvals++;
            if (i < DECBITS) {
                c = code << (DECBITS - 1 - i);
                v = hu->vals[k] & 0x0f; /* size */
                for (d = 1 << (DECBITS - 1 - i); --d >= 0;) {
                    if (v + i < DECBITS) { /* both fit in table */
                        x = d >> (DECBITS - 1 - v - i);
                        if (v && x < (1 << (v - 1))) {
                            x += (-1 << v) + 1;
                        }
                        x = x << 16 | (hu->vals[k] & 0xf0) << 4
                            | (DECBITS - (i + 1 + v)) | 128;
                    } else {
                        x = v << 16 | (hu->vals[k] & 0xf0) << 4
                            | (DECBITS - (i + 1));
                    }
                    hu->llvals[c | d] = x;
                }
            }
            code++;
            k++;
        }
        hu->maxcode[i] = code;
    }
    hu->maxcode[16] = 0x20000; /* always terminate decode */
}

static int readtables(int till, int *isDHT) {
    int m, l, i, j, lq, pq, tq;
    int tc, th, tt;

    for (;;) {

        if (getbyte() != 0xff) {
            return -1;
        }

        if ((m = getbyte()) == till) {
            break;
        }

        switch (m) {
            case 0xc2:
                return 0;

            case M_DQT:
                lq = getword();
                while (lq > 2) {
                    pq = getbyte();
                    tq = pq & 15;

                    if (tq > 3) {
                        return -1;
                    }

                    pq >>= 4;

                    if (pq != 0) {
                        return -1;
                    }

                    for (i = 0; i < 64; i++) {
                        quant[tq][i] = getbyte();
                    }
                    lq -= 64 + 1;
                }
                break;

            case M_DHT:
                l = getword();
                while (l > 2) {
                    int hufflen[16], k;
                    unsigned char huffvals[256];

                    tc = getbyte();
                    th = tc & 15;
                    tc >>= 4;
                    tt = tc * 2 + th;

                    if (tc > 1 || th > 1) {
                        return -1;
                    }

                    for (i = 0; i < 16; i++) {
                        hufflen[i] = getbyte();
                    }

                    l -= 1 + 16;
                    k = 0;

                    for (i = 0; i < 16; i++) {
                        for (j = 0; j < hufflen[i]; j++) {
                            huffvals[k++] = getbyte();
                        }
                        l -= hufflen[i];
                    }
                    decMakehuff(dhuff + tt, hufflen, huffvals);
                }
                *isDHT = 1;
                break;

            case M_DRI:
                l = getword();
                info.dri = getword();
                break;

            default:
                l = getword();
                while (l-- > 2) {
                    getbyte();
                }
                break;
        }
    }

    return 0;
}

static int huffmanInit(void) {

    int tc, th, tt;
    const unsigned char *ptr = JPEGHuffmanTable;
    int i, j, l;
    l = JPG_HUFFMAN_TABLE_LENGTH;

    while (l > 0) {
        int hufflen[16], k;
        unsigned char huffvals[256];

        tc = *ptr++;
        th = tc & 15;
        tc >>= 4;
        tt = tc * 2 + th;
        if (tc > 1 || th > 1) {
            return -ERR_BAD_TABLES;
        }

        for (i = 0; i < 16; i++) {
            hufflen[i] = *ptr++;
        }
        l -= 1 + 16;
        k = 0;
        for (i = 0; i < 16; i++) {
            for (j = 0; j < hufflen[i]; j++) {
                huffvals[k++] = *ptr++;
            }
            l -= hufflen[i];
        }
        decMakehuff(dhuff + tt, hufflen, huffvals);
    }
    return 0;
}

static void decInitscans(void) {
    int i;
    info.nm = info.dri + 1;
    info.rm = M_RST0;
    for (i = 0; i < info.ns; i++) {
        dscans[i].dc = 0;
    }
}

static int decCheckmarker(void) {
    int i;

    if (decReadmarker(&in) != info.rm) {
        return -1;
    }
    info.nm = info.dri;
    info.rm = (info.rm + 1) & ~0x08;
    for (i = 0; i < info.ns; i++) {
        dscans[i].dc = 0;
    }
    return 0;
}

inline static void idct(int *in, int *out, int *quant, long off, int max) {

    long t0, t1, t2, t3, t4, t5, t6, t7;	// t ;
    long tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    long tmp[64], *tmpp;
    int i, j, te;
    unsigned char *zig2p;

    t0 = off;

    if (max == 1) {
        t0 += in[0] * quant[0];
        for (i = 0; i < 64; i++) {
            out[i] = ITOINT(t0);
        }
        return;
    }
    zig2p = zig2;
    tmpp = tmp;

    for (i = 0; i < 8; i++) {
        j = *zig2p++;
        t0 += in[j] * (long) quant[j];
        j = *zig2p++;
        t5 = in[j] * (long) quant[j];
        j = *zig2p++;
        t2 = in[j] * (long) quant[j];
        j = *zig2p++;
        t7 = in[j] * (long) quant[j];
        j = *zig2p++;
        t1 = in[j] * (long) quant[j];
        j = *zig2p++;
        t4 = in[j] * (long) quant[j];
        j = *zig2p++;
        t3 = in[j] * (long) quant[j];
        j = *zig2p++;
        t6 = in[j] * (long) quant[j];

        if ((t1 | t2 | t3 | t4 | t5 | t6 | t7) == 0) {

            tmpp[0 * 8] = t0;
            tmpp[1 * 8] = t0;
            tmpp[2 * 8] = t0;
            tmpp[3 * 8] = t0;
            tmpp[4 * 8] = t0;
            tmpp[5 * 8] = t0;
            tmpp[6 * 8] = t0;
            tmpp[7 * 8] = t0;

            tmpp++;
            t0 = 0;
            continue;
        }
        //IDCT;
        tmp0 = t0 + t1;
        t1 = t0 - t1;
        tmp2 = t2 - t3;
        t3 = t2 + t3;
        tmp2 = IMULT(tmp2, IC4) - t3;
        tmp3 = tmp0 + t3;
        t3 = tmp0 - t3;
        tmp1 = t1 + tmp2;
        tmp2 = t1 - tmp2;
        tmp4 = t4 - t7;
        t7 = t4 + t7;
        tmp5 = t5 + t6;
        t6 = t5 - t6;
        tmp6 = tmp5 - t7;
        t7 = tmp5 + t7;
        tmp5 = IMULT(tmp6, IC4);
        tmp6 = IMULT((tmp4 + t6), S22);
        tmp4 = IMULT(tmp4, (C22 - S22)) + tmp6;
        t6 = IMULT(t6, (C22 + S22)) - tmp6;
        t6 = t6 - t7;
        t5 = tmp5 - t6;
        t4 = tmp4 - t5;

        tmpp[0 * 8] = tmp3 + t7;	//t0;
        tmpp[1 * 8] = tmp1 + t6;	//t1;
        tmpp[2 * 8] = tmp2 + t5;	//t2;
        tmpp[3 * 8] = t3 + t4;	//t3;
        tmpp[4 * 8] = t3 - t4;	//t4;
        tmpp[5 * 8] = tmp2 - t5;	//t5;
        tmpp[6 * 8] = tmp1 - t6;	//t6;
        tmpp[7 * 8] = tmp3 - t7;	//t7;
        tmpp++;
        t0 = 0;
    }

    for (i = 0, j = 0; i < 8; i++) {
        t0 = tmp[j + 0];
        t1 = tmp[j + 1];
        t2 = tmp[j + 2];
        t3 = tmp[j + 3];
        t4 = tmp[j + 4];
        t5 = tmp[j + 5];
        t6 = tmp[j + 6];
        t7 = tmp[j + 7];

        if ((t1 | t2 | t3 | t4 | t5 | t6 | t7) == 0) {
            te = ITOINT(t0);
            out[j + 0] = te;
            out[j + 1] = te;
            out[j + 2] = te;
            out[j + 3] = te;
            out[j + 4] = te;
            out[j + 5] = te;
            out[j + 6] = te;
            out[j + 7] = te;
            j += 8;
            continue;
        }
        //IDCT;
        tmp0 = t0 + t1;
        t1 = t0 - t1;
        tmp2 = t2 - t3;
        t3 = t2 + t3;
        tmp2 = IMULT(tmp2, IC4) - t3;
        tmp3 = tmp0 + t3;
        t3 = tmp0 - t3;
        tmp1 = t1 + tmp2;
        tmp2 = t1 - tmp2;
        tmp4 = t4 - t7;
        t7 = t4 + t7;
        tmp5 = t5 + t6;
        t6 = t5 - t6;
        tmp6 = tmp5 - t7;
        t7 = tmp5 + t7;
        tmp5 = IMULT(tmp6, IC4);
        tmp6 = IMULT((tmp4 + t6), S22);
        tmp4 = IMULT(tmp4, (C22 - S22)) + tmp6;
        t6 = IMULT(t6, (C22 + S22)) - tmp6;
        t6 = t6 - t7;
        t5 = tmp5 - t6;
        t4 = tmp4 - t5;

        out[j + 0] = ITOINT(tmp3 + t7);
        out[j + 1] = ITOINT(tmp1 + t6);
        out[j + 2] = ITOINT(tmp2 + t5);
        out[j + 3] = ITOINT(t3 + t4);
        out[j + 4] = ITOINT(t3 - t4);
        out[j + 5] = ITOINT(tmp2 - t5);
        out[j + 6] = ITOINT(tmp1 - t6);
        out[j + 7] = ITOINT(tmp3 - t7);
        j += 8;
    }
}

static void idctqtab(unsigned char *qin, PREC *qout) {
    int i, j;
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            qout[zig[i * 8 + j]] = qin[zig[i * 8 + j]] * IMULT(aaidct[i], aaidct[j]);
        }
    }
}

static void setinput(struct in *in, unsigned char *p) {
    in->p = p;
    in->left = 0;
    in->bits = 0;
    in->marker = 0;
}

#define LEBI_DCL	int le, bi
#define LEBI_GET(in)	(le = in->left, bi = in->bits)
#define LEBI_PUT(in)	(in->left = le, in->bits = bi)

#define GETBITS(in, n) (\
    (le < (n) ? le = fillbits(in, le, bi), bi = in->bits : 0), \
    (le -= (n)), \
    bi >> le & ((1 << (n)) - 1) \
    )

#define UNGETBITS(in, n) (le += (n))

static int dec_rec2(struct in *in, struct decHufftbl *hu, int *runp, int c, int i) {
    LEBI_DCL;
    LEBI_GET(in);

    if (i) {
        UNGETBITS(in, i & 127);
        *runp = i >> 8 & 15;
        i >>= 16;
    } else {
        for (i = DECBITS; (c = ((c << 1) | GETBITS(in, 1))) >= (hu->maxcode[i]); i++);

        if (i >= 16) {
            in->marker = M_BADHUFF;
            return 0;
        }
        i = hu->vals[hu->valptr[i] + c - hu->maxcode[i - 1] * 2];
        *runp = i >> 4;
        i &= 15;
    }

    if (i == 0) { /* sigh, 0xf0 is 11 bit */
        LEBI_PUT(in);
        return 0;
    }
    /* receive part */
    c = GETBITS(in, i);

    if (c < (1 << (i - 1))) {
        c += (-1 << i) + 1;
    }
    LEBI_PUT(in);
    return c;
}

#define DEC_REC(in, hu, r, i)	 ( \
    r = GETBITS(in, DECBITS), \
    i = hu->llvals[r], i & 128 ? (UNGETBITS(in, i & 127),\
    r = i >> 8 & 15, i >> 16) : (LEBI_PUT(in),\
    i = dec_rec2(in, hu, &r, r, i), LEBI_GET(in),i))

static void decode_mcus(struct in *in, int *dct, int n, struct scan *sc, int *maxp) {

    struct decHufftbl *hu;
    int i, r, t;
    LEBI_DCL;

    memset(dct, 0, n * 64 * sizeof(*dct));
    LEBI_GET(in);

    while (n-- > 0) {

        hu = sc->hudc.dhuff;
        *dct++ = (sc->dc += DEC_REC(in, hu, r, t));
        hu = sc->huac.dhuff;
        i = 63;

        while (i > 0) {
            t = DEC_REC(in, hu, r, t);
            if (t == 0 && r == 0) {
                dct += i;
                break;
            }
            dct += r;
            *dct++ = t;
            i -= r + 1;
        }

        *maxp++ = 64 - i;

        if (n == sc->next) {
            sc++;
        }
    }
    LEBI_PUT(in);
}

int jpegDecode(unsigned char **pic, unsigned char *buf, int *width, int *height){

    struct jpegDecdata *decdata;
    int i, j, m, tac, tdc;
    int intwidth, intheight;
    int mcusx, mcusy, mx, my;
    int ypitch, xpitch, bpp, pitch, x, y;
    int mb;
    int max[6];
    ftopict convert;
    int err = 0;
    int isInitHuffman = 0;
    decdata = (struct jpegDecdata *) malloc(sizeof(struct jpegDecdata));

    if (!decdata) {
        err = -1;
        goto error;
    }

    if (buf == NULL) {
        err = -1;
        goto error;
    }
    datap = buf;

    if (getbyte() != 0xff) {
        err = ERR_NO_SOI;
        goto error;
    }

    if (getbyte() != M_SOI) {
        err = ERR_NO_SOI;
        goto error;
    }
    if (readtables(M_SOF0, &isInitHuffman)) {
        err = ERR_BAD_TABLES;
        goto error;
    }
    getword();
    i = getbyte();
    if (i != 8) {
        err = ERR_NOT_8BIT;
        goto error;
    }

    intheight = getword();
    intwidth = getword();

    if ((intheight & 7) || (intwidth & 7)) {
        err = ERR_BAD_WIDTH_OR_HEIGHT;
        goto error;
    }

    info.nc = getbyte();

    if (info.nc > MAXCOMP) {
        err = ERR_TOO_MANY_COMPPS;
        goto error;
    }

    for (i = 0; i < info.nc; i++) {
        int h, v;
        comps[i].cid = getbyte();
        comps[i].hv = getbyte();
        v = comps[i].hv & 15;
        h = comps[i].hv >> 4;
        comps[i].tq = getbyte();
        if (h > 3 || v > 3) {
            err = ERR_ILLEGAL_HV;
            goto error;
        }
        if (comps[i].tq > 3) {
            err = ERR_QUANT_TABLE_SELECTOR;
            goto error;
        }
    }

    if (readtables(M_SOS, &isInitHuffman)) {
        err = ERR_BAD_TABLES;
        goto error;
    }
    getword();
    info.ns = getbyte();

    if (!info.ns) {
        LOGD("info ns %d/n", info.ns);
        err = ERR_NOT_YCBCR_221111;
        goto error;
    }

    for (i = 0; i < info.ns; i++) {
        dscans[i].cid = getbyte();
        tdc = getbyte();
        tac = tdc & 15;
        tdc >>= 4;
        if (tdc > 1 || tac > 1) {
            err = ERR_QUANT_TABLE_SELECTOR;
            goto error;
        }

        for (j = 0; j < info.nc; j++) {
            if (comps[j].cid == dscans[i].cid) {
                break;
            }
        }
        if (j == info.nc) {
            err = ERR_UNKNOWN_CID_IN_SCAN;
            goto error;
        }
        dscans[i].hv = comps[j].hv;
        dscans[i].tq = comps[j].tq;
        dscans[i].hudc.dhuff = dec_huffdc + tdc;
        dscans[i].huac.dhuff = dec_huffac + tac;
    }

    i = getbyte();
    j = getbyte();
    m = getbyte();

    if (i != 0 || j != 63 || m != 0) {
        LOGD("hmm FW error,not seq DCT ??\n");
    }

    if (!isInitHuffman) {
        if (huffmanInit() < 0) {
            return -ERR_BAD_TABLES;
        }
    }

    if (intwidth != *width || intheight != *height || *pic == NULL) {
        *width = intwidth;
        *height = intheight;
        // BytesperPixel 2 yuyv , 3 rgb24
        *pic = (unsigned char *) realloc((unsigned char *) *pic,
                                         (size_t) intwidth * (intheight + 8) * 2);
    }

    //LOGD("hv=%d", dscans[0].hv);

    switch (dscans[0].hv) {
        case 0x22: // 411
            mb = 6;
            mcusx = *width >> 4;
            mcusy = *height >> 4;
            bpp = 2;
            xpitch = 16 * bpp;
            pitch = *width * bpp; // YUYV out
            ypitch = 16 * pitch;
            convert = yuv420pto422;
            break;
        case 0x21: //422
            // printf("find 422 %dx%d\n",*width,*height);
            mb = 4;
            mcusx = *width >> 4;
            mcusy = *height >> 3;
            bpp = 2;
            xpitch = 16 * bpp;
            pitch = *width * bpp; // YUYV out
            ypitch = 8 * pitch;
            convert = yuv422pto422;
            break;
        case 0x11: //444
            mcusx = *width >> 3;
            mcusy = *height >> 3;
            bpp = 2;
            xpitch = 8 * bpp;
            pitch = *width * bpp; // YUYV out
            ypitch = 8 * pitch;
            if (info.ns == 1) {
                mb = 1;
                convert = yuv400pto422;
            } else {
                mb = 3;
                convert = yuv444pto422;
            }
            break;
        default:
            err = ERR_NOT_YCBCR_221111;
            goto error;
            break;
    }

    idctqtab(quant[dscans[0].tq], decdata->dquant[0]);
    idctqtab(quant[dscans[1].tq], decdata->dquant[1]);
    idctqtab(quant[dscans[2].tq], decdata->dquant[2]);
    setinput(&in, datap);
    decInitscans();

    dscans[0].next = 2;
    dscans[1].next = 1;
    dscans[2].next = 0; /* 4xx encoding */

    for (my = 0, y = 0; my < mcusy; my++, y += ypitch) {
        for (mx = 0, x = 0; mx < mcusx; mx++, x += xpitch) {
            if (info.dri && !--info.nm) {
                if (decCheckmarker()) {
                    err = ERR_WRONG_MARKER;
                    goto error;
                }
            }
            switch (mb) {
                case 6: {
                    decode_mcus(&in, decdata->dcts, mb, dscans, max);
                    idct(decdata->dcts, decdata->out, decdata->dquant[0],
                         IFIX(128.5), max[0]);
                    idct(decdata->dcts + 64, decdata->out + 64, decdata->dquant[0],
                         IFIX(128.5), max[1]);
                    idct(decdata->dcts + 128, decdata->out + 128,
                         decdata->dquant[0], IFIX(128.5), max[2]);
                    idct(decdata->dcts + 192, decdata->out + 192,
                         decdata->dquant[0], IFIX(128.5), max[3]);
                    idct(decdata->dcts + 256, decdata->out + 256,
                         decdata->dquant[1], IFIX(0.5), max[4]);
                    idct(decdata->dcts + 320, decdata->out + 320,
                         decdata->dquant[2], IFIX(0.5), max[5]);

                }
                    break;

                case 4: {
                    decode_mcus(&in, decdata->dcts, mb, dscans, max);
                    idct(decdata->dcts, decdata->out, decdata->dquant[0],
                         IFIX(128.5), max[0]);
                    idct(decdata->dcts + 64, decdata->out + 64, decdata->dquant[0],
                         IFIX(128.5), max[1]);
                    idct(decdata->dcts + 128, decdata->out + 256,
                         decdata->dquant[1], IFIX(0.5), max[4]);
                    idct(decdata->dcts + 192, decdata->out + 320,
                         decdata->dquant[2], IFIX(0.5), max[5]);

                }
                    break;

                case 3: {
                    decode_mcus(&in, decdata->dcts, mb, dscans, max);
                    idct(decdata->dcts, decdata->out, decdata->dquant[0],
                         IFIX(128.5), max[0]);
                    idct(decdata->dcts + 64, decdata->out + 256, decdata->dquant[1],
                         IFIX(0.5), max[4]);
                    idct(decdata->dcts + 128, decdata->out + 320,
                         decdata->dquant[2], IFIX(0.5), max[5]);
                }
                    break;

                case 1: {
                    decode_mcus(&in, decdata->dcts, mb, dscans, max);
                    idct(decdata->dcts, decdata->out, decdata->dquant[0],
                         IFIX(128.5), max[0]);
                }
                    break;

            } // switch enc411
            convert(decdata->out, *pic + y + x, pitch);
        }
    }

    m = decReadmarker(&in);
    if (m != M_EOI) {
        err = ERR_NO_EOI;
        goto error;
    }
    if (decdata) {
        free(decdata);
        decdata = NULL;
    }
    return 0;

error:
    if (decdata) {
        free(decdata);
        decdata = NULL;
    }
    return err;
}