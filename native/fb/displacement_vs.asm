; test for vertex texture fetch
; this crashes the blob driver on my Arnova as it doesn't properly set up sampler 8
; so I wonder if this will work when I properly setup that sampler (it does!)
; u11.z   scaling factor applied to displacement
MOV t3.xy__, void, void, u4.wwww
MOV t3.__z_, void, void, u5.wwww
TEXLD t4, tex8, t0.xyyy, void, void
MUL t4.x___, t4.x, u11.z, void   ; apply scaling factor to displacement
MUL t4.xyz_, t4.xxxx, t2.xyzz, void
MOV t4.___w, void, void, u6.wwww
ADD t4, t1, void, t4
MUL t5, u0, t4.xxxx, void
MAD t5, u1, t4.yyyy, t5
MAD t5, u2, t4.zzzz, t5
MAD t4, u3, t4.wwww, t5
MUL t5.xyz_, u4.xyzz, t2.xxxx, void
MAD t5.xyz_, u5.xyzz, t2.yyyy, t5.xyzz
MAD t2.xyz_, u6.xyzz, t2.zzzz, t5.xyzz
MUL t5, u7, t1.xxxx, void
MAD t5, u8, t1.yyyy, t5
MAD t5, u9, t1.zzzz, t5
MAD t1, u10, t1.wwww, t5
RCP t2.___w, void, void, t1.wwww
MAD t1.xyz_, -t1.xyzz, t2.wwww, t3.xyzz
DP3 t3.xyz_, t1.xyzz, t1.xyzz, void
RSQ t3.xyz_, void, void, t3.xxxx
MUL t1.xyz_, t1.xyzz, t3.xyzz, void
DP3 t1.x___, t2.xyzz, t1.xyzz, void
SELECT.LT t1.x___, u6.wwww, t1.xxxx, u6.wwww
MOV t1._yz_, void, void, u6.wwww
MOV t1.___w, void, void, u11.xxxx
ADD t4.__z_, t4.zzzz, void, t4.wwww
MUL t4.__z_, t4.zzzz, u11.yyyy, void
