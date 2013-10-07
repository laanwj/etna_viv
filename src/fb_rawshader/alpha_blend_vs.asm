; VS used for `alpha_blend` test

; compute output position using model-view-projection
MUL t4, u0, t0.xxxx, void
MAD t4, u1, t0.yyyy, t4
MAD t4, u2, t0.zzzz, t4
MAD t4, u3, t0.wwww, t4

; compute output color
MOV t0, void, void, t2 ; t0 = vertex color
MUL t0, t0, u12, void  ; multiply in material color

; transform z of output vertex
ADD t4.__z_, t4.zzzz, void, t4.wwww  ; z = z + w
MUL t4.__z_, t4.zzzz, u11.yyyy, void ; z = (z + w) * 0.5
