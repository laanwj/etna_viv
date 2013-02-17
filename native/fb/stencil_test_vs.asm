; very basic vertex shader
; z = (z+w)/2
; 1 temporary register used (t0)
ADD t0.__z_, t0.zzzz, void, t0.wwww
MUL t0.__z_, t0.zzzz, u0.xxxx, void
