; ps for particle_system demo
; t1.xy should be marked as pointcoord x/y
; uniform inputs
; u0  u_color
ADD t1._y__, u1.xxxx, void, -t1.yyyy
TEXLD t1, tex0, t1.xyyy, void, void
MUL t1, u0, t1, void
MUL t1.___w, t1.wwww, t2.xxxx, void
