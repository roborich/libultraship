#pragma once

typedef int Mtx_t[4][4];
typedef union {
    Mtx_t m;
    struct {
        unsigned short int intPart[4][4];
        unsigned short int fracPart[4][4];
    };
    long long int forc_structure_alignment;
} MtxS;

typedef float MtxF_t[4][4];
typedef union {
    MtxF_t mf;
    struct {
        float xx, yx, zx, wx, xy, yy, zy, wy, xz, yz, zz, wz, xw, yw, zw, ww;
    };
} MtxF;

// GBI_FLOATS: the whole GBI is float (vertices included).
// GBI_FLOAT_MTX (CMake option): only matrices are float (MtxF); vertices stay s16. The s16.16 fixed-point Mtx
// wraps any translation >= 32768, which caps the world at +/-32767 units even when everything else is float.
// The game writes floats straight into Mtx (guMtxF2L is a copy) and the interpreter reads them back verbatim;
// Matrix resources stored fixed-point are unpacked at load (MatrixFactory).
#if defined(GBI_FLOATS) || defined(GBI_FLOAT_MTX)
typedef MtxF Mtx;
#else
typedef MtxS Mtx;
#endif
