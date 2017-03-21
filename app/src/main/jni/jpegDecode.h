#ifndef __JPEG_DECODE_H__
#define __JPEG_DECODE_H__

#define ERR_NO_SOI (1)
#define ERR_NOT_8BIT (2)
#define ERR_HEIGHT_MISMATCH (3)
#define ERR_WIDTH_MISMATCH (4)
#define ERR_BAD_WIDTH_OR_HEIGHT (5)
#define ERR_TOO_MANY_COMPPS (6)
#define ERR_ILLEGAL_HV (7)
#define ERR_QUANT_TABLE_SELECTOR (8)
#define ERR_NOT_YCBCR_221111 (9)
#define ERR_UNKNOWN_CID_IN_SCAN (10)
#define ERR_NOT_SEQUENTIAL_DCT (11)
#define ERR_WRONG_MARKER (12)
#define ERR_NO_EOI (13)
#define ERR_BAD_TABLES (14)
#define ERR_DEPTH_MISMATCH (15)

#define M_SOI	(0xd8)
#define M_APP0	(0xe0)
#define M_DQT	(0xdb)
#define M_SOF0	(0xc0)
#define M_DHT   (0xc4)
#define M_DRI	(0xdd)
#define M_SOS	(0xda)
#define M_RST0	(0xd0)
#define M_EOI	(0xd9)
#define M_COM	(0xfe)

/* special markers */
#define M_BADHUFF	(-1)
#define M_EOF		(0x80)

#define DECBITS (10)		/* seems to be the optimum */

#define MAXCOMP (4)

#define ISHIFT (11)
#define ITOINT(a) ((a) >> ISHIFT)
#define IMULT(a, b) (((a) * (b)) >> ISHIFT)
#define IFIX(a) ((int)((a) * (1 << ISHIFT) + .5))

#undef PREC
#define PREC int
#define S22 ((PREC)IFIX(2 * 0.382683432))
#define C22 ((PREC)IFIX(2 * 0.923879532))
#define IC4 ((PREC)IFIX(1 / 0.707106781))

struct jpegDecdata {
    int dcts[6 * 64 + 16];
    int out[64 * 6];
    int dquant[3][64];
};

struct decHufftbl {
    int maxcode[17];
    int valptr[16];
    unsigned char vals[256];
    unsigned int llvals[1 << DECBITS];
};

struct jpgInfo {
    int nc; /* number of components */
    int ns; /* number of scans */
    int dri; /* restart interval */
    int nm; /* mcus til next marker */
    int rm; /* next restart marker */
};

struct comp {
    int cid;
    int hv;
    int tq;
};

union hufftblp {
    struct decHufftbl *dhuff;
    struct encHufftbl *ehuff;
};

struct scan {
    int dc; /* old dc value */

    union hufftblp hudc;
    union hufftblp huac;
    int next; /* when to switch to next scan */

    int cid; /* component id */
    int hv; /* horiz/vert, copied from comp */
    int tq; /* quant tbl, copied from comp */
};

struct in {
    unsigned char *p;
    unsigned int bits;
    int left;
    int marker;
    int (*func)__P((void *));
    void *data;
};

int jpegDecode(unsigned char **pic, unsigned char *buf, int *width, int *height);

#endif
