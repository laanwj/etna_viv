; ps program for cubemap_sphere
MUL t1, u0.xxxx, t1, void
TEXLD t2, tex0, t2.xyzz, void, void
;TEXLD t2, tex0, u1, void, void
MUL t1, t1, t2, void
