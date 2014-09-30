#ifndef _polymost_h_
# define _polymost_h_

#ifdef USE_OPENGL

#include "glbuild.h"
#include "hightile.h"

typedef struct { char r, g, b, a; } coltype;

extern int32_t rendmode;
extern float gtang;
extern float glox1, gloy1;
extern float gxyaspect, grhalfxdown10x;
extern float gcosang, gsinang, gcosang2, gsinang2;
extern float gchang, gshang, gctang, gstang, gvisibility;

struct glfiltermodes {
	const char *name;
	int32_t min,mag;
};
#define NUMGLFILTERMODES 6
extern struct glfiltermodes glfiltermodes[NUMGLFILTERMODES];

//void phex(char v, char *s);
void uploadtexture(int32_t doalloc, int32_t xsiz, int32_t ysiz, int32_t intexfmt, int32_t texfmt, coltype *pic, int32_t tsizx, int32_t tsizy, int32_t dameth);
void polymost_drawsprite(int32_t snum);
void polymost_drawmaskwall(int32_t damaskwallcnt);
void polymost_dorotatesprite(int32_t sx, int32_t sy, int32_t z, int16_t a, int16_t picnum,
                             int8_t dashade, char dapalnum, int32_t dastat, uint8_t daalpha, int32_t cx1, int32_t cy1, int32_t cx2, int32_t cy2, int32_t uniqid);
void polymost_fillpolygon(int32_t npoints);
void polymost_initosdfuncs(void);
void polymost_drawrooms(void);

void polymost_glinit(void);
void polymost_glreset(void);

enum {
    INVALIDATE_ALL,
    INVALIDATE_ART
};

void gltexinvalidate(int32_t dapicnum, int32_t dapalnum, int32_t dameth);
void gltexinvalidatetype(int32_t type);
int32_t polymost_printext256(int32_t xpos, int32_t ypos, int16_t col, int16_t backcol, const char *name, char fontsize);

extern float curpolygonoffset;

extern float shadescale;
extern int32_t shadescale_unbounded;
extern float alphahackarray[MAXTILES];

extern int32_t r_usenewshading;
extern int32_t r_usetileshades;
extern int32_t r_npotwallmode;

extern int16_t globalpicnum;
extern int32_t globalpal;

// Compare with polymer_eligible_for_artmap()
static inline int32_t eligible_for_tileshades(int32_t picnum, int32_t pal)
{
    return (!usehightile || !hicfindsubst(picnum, pal)) &&
        (!usemodels || md_tilehasmodel(picnum, pal) < 0);
}

static inline float getshadefactor(int32_t shade)
{
    int32_t shadebound = (shadescale_unbounded || shade>=numshades) ? numshades : numshades-1;
    float clamped_shade = min(max(shade*shadescale, 0), shadebound);

    // 8-bit tiles, i.e. non-hightiles and non-models, don't get additional
    // glColor() shading with r_usetileshades!
    if (getrendermode() == REND_POLYMOST && r_usetileshades &&
            eligible_for_tileshades(globalpicnum, globalpal))
        return 1.f;

    return ((float)(numshades-clamped_shade))/(float)numshades;
}

#define POLYMOST_CHOOSE_FOG_PAL(fogpal, pal) \
    ((fogpal) ? (fogpal) : (pal))
static inline int32_t get_floor_fogpal(const sectortype *sec)
{
    return POLYMOST_CHOOSE_FOG_PAL(sec->fogpal, sec->floorpal);
}
static inline int32_t get_ceiling_fogpal(const sectortype *sec)
{
    return POLYMOST_CHOOSE_FOG_PAL(sec->fogpal, sec->ceilingpal);
}
static inline int32_t fogpal_shade(const sectortype *sec, int32_t shade)
{
    // When fogging is due to sector[].fogpal, don't make the fog parameters
    // depend on the shade of the object.
    return sec->fogpal ? 0 : shade;
}

static inline int check_nonpow2(int32_t x)
{
    return (x > 1 && (x&(x-1)));
}

// Are we using the mode that uploads non-power-of-two wall textures like they
// render in classic?
static inline int polymost_is_npotmode(void)
{
    // The glinfo.texnpot check is so we don't have to deal with that case in
    // gloadtile_art().
    return glinfo.texnpot &&
        // r_npotwallmode is NYI for hightiles. We require r_hightile off
        // because in calc_ypanning(), the repeat would be multiplied by a
        // factor even if no modified texture were loaded.
        !usehightile &&
#ifdef NEW_MAP_FORMAT
        g_loadedMapVersion < 10 &&
#endif
        r_npotwallmode;
}

// Flags of the <dameth> argument of various functions
enum {
    DAMETH_CLAMPED = 4,

    DAMETH_WALL = 32,  // signals a texture for a wall (for r_npotwallmode)

    DAMETH_NOCOMPRESS = 4096,
    DAMETH_HI = 8192,
};

// DAMETH_CLAMPED -> PTH_CLAMPED conversion
#define TO_PTH_CLAMPED(dameth) ((((dameth)&4))>>2)

// Do we want a NPOT-y-as-classic texture for this <dameth> and <ysiz>?
static inline int polymost_want_npotytex(int32_t dameth, int32_t ysiz)
{
    return getrendermode() != REND_POLYMER &&  // r_npotwallmode NYI in Polymer
        polymost_is_npotmode() && (dameth&DAMETH_WALL) && check_nonpow2(ysiz);
}

// pthtyp pth->flags bits
enum {
    PTH_CLAMPED = 1,
    PTH_HIGHTILE = 2,
    PTH_SKYBOX = 4,
    PTH_HASALPHA = 8,
    PTH_HASFULLBRIGHT = 16,
    PTH_NPOTWALL = DAMETH_WALL,  // r_npotwallmode=1 generated texture

    PTH_INVALIDATED = 128,
};

typedef struct pthtyp_t
{
    struct pthtyp_t *next;
    uint32_t glpic;
    int16_t picnum;
    char palnum;
    char shade;
    char effects;
    char flags;      // 1 = clamped (dameth&4), 2 = hightile, 4 = skybox face, 8 = hasalpha, 16 = hasfullbright, 128 = invalidated
    char skyface;
    hicreplctyp *hicr;

    uint16_t sizx, sizy;
    float scalex, scaley;
    struct pthtyp_t *ofb; // only fullbright
} pthtyp;

extern void gloadtile_art(int32_t,int32_t,int32_t,int32_t,pthtyp *,int32_t);
extern int32_t gloadtile_hi(int32_t,int32_t,int32_t,hicreplctyp *,int32_t,pthtyp *,int32_t,char);

extern int32_t globalnoeffect;
extern int32_t drawingskybox;
extern int32_t hicprecaching;
extern float gyxscale, gxyaspect, ghalfx, grhalfxdown10;

extern char ptempbuf[MAXWALLSB<<1];

static inline void polymost_setupdetailtexture(int32_t *texunits, int32_t tex)
{
    bglActiveTextureARB(++*texunits);

    bglEnable(GL_TEXTURE_2D);
    bglBindTexture(GL_TEXTURE_2D, tex);

    bglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
    bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE);

    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);

    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);

    bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);

    bglTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 2.0f);

    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
}

static inline void polymost_setupglowtexture(int32_t *texunits, int32_t tex)
{
    bglActiveTextureARB(++*texunits);

    bglEnable(GL_TEXTURE_2D);
    bglBindTexture(GL_TEXTURE_2D, tex);

    bglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_ARB);
    bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_INTERPOLATE_ARB);

    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);

    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_TEXTURE);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR);

    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE2_RGB_ARB, GL_TEXTURE);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND2_RGB_ARB, GL_ONE_MINUS_SRC_ALPHA);

    bglTexEnvf(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
    bglTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB);
    bglTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);

    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    bglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
}
#include "texcache.h"

#endif

#endif
