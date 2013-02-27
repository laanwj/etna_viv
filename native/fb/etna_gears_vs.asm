; etna_gears vs
;
; uniforms
;   u0-u3   normal_matrix
;   u4      lightsource_position
;   u5      material_color
;   u6-u9   modelviewprojection matrix
;
; constants
;   u10.x   0.0
;   u10.y   0.5
;
; inputs
;   t0      normal
;   t1      position
;
; output
;   t0      color (4 comps)
;   t1      position (4 comps)
;
MUL t2, u0, t0.xxxx, void
MAD t2, u1, t0.yyyy, t2
MAD t0, u2, t0.zzzz, t2
ADD t0, t0, void, u3
DP3 t2.xyz_, t0.xyzz, t0.xyzz, void
RSQ t2.xyz_, void, void, t2.xxxx
MUL t0.xyz_, t0.xyzz, t2.xyzz, void
DP3 t2.xyz_, u4.xyzz, u4.xyzz, void
RSQ t2.xyz_, void, void, t2.xxxx
MUL t2.xyz_, u4.xyzz, t2.xyzz, void
DP3 t0.x___, t0.xyzz, t2.xyzz, void
SELECT.LT t0.x___, t0.xxxx, u10.xxxx, t0.xxxx
MUL t0, t0.xxxx, u5, void
MUL t2, u6, t1.xxxx, void
MAD t2, u7, t1.yyyy, t2
MAD t1, u8, t1.zzzz, t2
ADD t1, t1, void, u9
ADD t1.__z_, t1.zzzz, void, t1.wwww
MUL t1.__z_, t1.zzzz, u10.yyyy, void
