; fs for particle system demo 
; different from the others in that it uses branching, and a .SAT modifier
; uniform inputs
;  u0.x    u_time
;  u0.yzw  u_centerPosition
; uniform constants
;  u1.x 1.0
;  u1.y -1000.0
;  u1.z 0.0
;  u1.w 40.0
;  u2.x 0.5
; outputs
;  t0  v_lifetime
;  t1  position
;  t2  pointsize
 BRANCH.GT void, u0.xxxx, t0.xxxx, label_5
 MAD t1.xyz_, u0.xxxx, t2.xyzz, t1.xyzz
 ADD t1.xyz_, t1.xyzz, void, u0.yzww
 MOV t1.___w, void, void, u1.xxxx
 BRANCH void, void, void, label_6
label_5:
 MOV t1, void, void, u1.yyzz
label_6:
 RCP t0.x___, void, void, t0.xxxx
 MOV t2, void, void, u0
 MAD t0.x___, -t2.xxxx, t0.xxxx, u1.xxxx
 MOV.SAT t0.x___, void, void, t0.xxxx
 MUL t0._y__, t0.xxxx, t0.xxxx, void
 MUL t2.x___, t0.yyyy, u1.wwww, void
 ADD t1.__z_, t1.zzzz, void, t1.wwww
 MUL t1.__z_, t1.zzzz, u2.xxxx, void
